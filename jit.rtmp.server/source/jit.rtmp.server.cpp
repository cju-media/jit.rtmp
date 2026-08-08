/// @file
/// jit.rtmp.server - Launch and manage a bundled MediaMTX (https://github.com/bluenviron/mediamtx)
/// media server as a background process, so a stream published into it (e.g. by
/// jit.rtmp.send~) can be pulled by any number of independent players - VLC, a
/// Raspberry Pi, another Max patch's jit.rtmp.receive~ - at once. No separate
/// server install: a real MediaMTX binary + default config ship inside this
/// external's own .mxo (Contents/Resources), fetched and bundled at build time
/// (see CMakeLists.txt) rather than committed to the repo.
///
/// This object owns none of the audio/video path itself - it's purely a
/// subprocess manager. Point jit.rtmp.send~'s @url at this server's RTMP port
/// (default rtmp://127.0.0.1:1935/<app>/<key>) and point players at the same
/// URL with your machine's actual network address.

#include "c74_min.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <crt_externs.h> // _NSGetEnviron() - avoids relying on `environ` being visible from a loadable bundle
#include <dlfcn.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace c74::min;

namespace {

// Deferred status/log/error messages need to cross from the manager thread
// back to Max's main thread. We stash them here and nudge a c74::min::queue<>
// (qelem) to drain them on the main thread. Same pattern as jit.rtmp.send~/receive~.
class status_mailbox {
public:
    void post(std::string msg) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.push_back(std::move(msg));
    }

    std::vector<std::string> drain() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::string> out;
        out.swap(m_pending);
        return out;
    }

private:
    std::mutex               m_mutex;
    std::vector<std::string> m_pending;
};

} // namespace

class jit_rtmp_server : public object<jit_rtmp_server> {
public:
    MIN_DESCRIPTION {
        "Launch a bundled MediaMTX server so an RTMP publish (e.g. from jit.rtmp.send~) can be "
        "pulled by any number of players (VLC, another jit.rtmp.receive~, a Raspberry Pi, ...) at "
        "once - no separate server install needed."
    };
    MIN_TAGS { "streaming, rtmp, server" };
    MIN_AUTHOR { "Cameron Johnston" };
    MIN_RELATED { "jit.rtmp.send~, jit.rtmp.receive~" };

    inlet<>  in_messages { this, "(anything) start / stop / restart" };
    outlet<> out_status  { this, "(anything) status / log / error messages" };

    jit_rtmp_server(const atoms& args = {}) {
        m_status_queue_ptr = std::make_unique<queue<>>(this, MIN_FUNCTION {
            drain_status();
            return atoms {};
        });
    }

    ~jit_rtmp_server() {
        stop_server();
    }

    // -----------------------------------------------------------------
    // Attributes
    // -----------------------------------------------------------------
    attribute<symbol> mediamtx_path { this, "mediamtx_path", "",
        description { "Override path to a mediamtx binary. Leave empty to use the one bundled inside this external." }
    };

    attribute<symbol> config { this, "config", "",
        description {
            "Override path to a mediamtx YAML config file. Leave empty to use the default one bundled "
            "inside this external (RTMP on port 1935 plus HLS/WebRTC/SRT/RTSP - see mediamtx's own docs "
            "to customize: https://github.com/bluenviron/mediamtx)."
        }
    };

    // -----------------------------------------------------------------
    // Messages
    // -----------------------------------------------------------------
    message<> start { this, "start", "Launch the (bundled, unless overridden) mediamtx server.",
        MIN_FUNCTION {
            start_server();
            return {};
        }
    };

    message<> stop { this, "stop", "Stop the server.",
        MIN_FUNCTION {
            stop_server();
            return {};
        }
    };

    message<> restart { this, "restart", "Stop then start the server.",
        MIN_FUNCTION {
            stop_server();
            start_server();
            return {};
        }
    };

private:
    std::thread       m_manager_thread;
    std::atomic<bool> m_running { false };
    std::atomic<bool> m_stop_requested { false };

    status_mailbox           m_mailbox;
    std::unique_ptr<queue<>> m_status_queue_ptr;

    void post_status(const std::string& msg) {
        m_mailbox.post(msg);
        if (m_status_queue_ptr)
            m_status_queue_ptr->set();
    }

    void drain_status() {
        for (auto& msg : m_mailbox.drain())
            out_status.send(symbol(msg));
    }

    // A function with a stable address inside this external's own loaded
    // binary, purely so dladdr() can tell us the on-disk path *we* were
    // loaded from (i.e. our own .mxo), to find our bundled Resources.
    static void anchor_symbol() {}

    std::string bundle_resources_dir() {
        Dl_info info {};
        if (dladdr((void*)&jit_rtmp_server::anchor_symbol, &info) == 0 || !info.dli_fname)
            return "";
        std::string self_path = info.dli_fname; // .../jit.rtmp.server.mxo/Contents/MacOS/jit.rtmp.server
        size_t      pos       = self_path.find("/Contents/MacOS/");
        if (pos == std::string::npos)
            return "";
        return self_path.substr(0, pos) + "/Contents/Resources";
    }

    std::string resolve_mediamtx_path() {
        std::string override_path = mediamtx_path.get().c_str();
        if (!override_path.empty())
            return override_path;
        std::string dir = bundle_resources_dir();
        if (dir.empty()) {
            post_status("error could not determine this external's own bundle location - set @mediamtx_path manually");
            return "";
        }
        return dir + "/mediamtx";
    }

    std::string resolve_config_path() {
        std::string override_cfg = config.get().c_str();
        if (!override_cfg.empty())
            return override_cfg;
        std::string dir = bundle_resources_dir();
        return dir.empty() ? "" : (dir + "/mediamtx.yml");
    }

    // -----------------------------------------------------------------
    // Lifecycle - both messages only ever touch atomics and (briefly) join
    // the manager thread; the actual process spawn/monitor/teardown all
    // happens on that background thread so Max's main thread never blocks
    // on network or process I/O.
    // -----------------------------------------------------------------
    void start_server() {
        if (m_running.load()) {
            post_status("error mediamtx is already running - send 'stop' first");
            return;
        }

        std::string bin = resolve_mediamtx_path();
        if (bin.empty())
            return; // resolve_mediamtx_path() already posted a clear error

        if (access(bin.c_str(), X_OK) != 0) {
            post_status("error mediamtx binary not found or not executable at " + bin
                        + " - reinstall the jit.rtmp package, or set @mediamtx_path");
            return;
        }

        std::string cfg = resolve_config_path();

        if (m_manager_thread.joinable())
            m_manager_thread.join(); // previous cycle fully wound down before starting a new one

        m_stop_requested.store(false);
        m_running.store(true);
        post_status("status starting mediamtx");
        m_manager_thread = std::thread([this, bin, cfg]() {
            manager_thread_main(bin, cfg);
        });
    }

    void stop_server() {
        if (!m_running.load() && !m_manager_thread.joinable())
            return; // nothing to do - silent no-op, matching jit.rtmp.send~/receive~'s convention

        m_stop_requested.store(true);
        if (m_manager_thread.joinable())
            m_manager_thread.join(); // bounded to ~1s SIGTERM grace + SIGKILL - see manager_thread_main()
    }

    // -----------------------------------------------------------------
    // Owns the child process end-to-end: spawn, stream its stdout/stderr
    // back as "log ..." status messages, detect either a requested stop
    // (SIGTERM -> brief grace -> SIGKILL) or an unprompted exit/crash, and
    // reap it. Both paths converge on the same "the child's stdout closed"
    // signal, so crash handling and orderly shutdown share one code path.
    // -----------------------------------------------------------------
    void manager_thread_main(const std::string& bin, const std::string& cfg) {
        int stdout_pipe[2];
        if (pipe(stdout_pipe) != 0) {
            post_status("error could not create pipe for mediamtx");
            m_running.store(false);
            return;
        }

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
        posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(bin.c_str()));
        if (!cfg.empty())
            argv.push_back(const_cast<char*>(cfg.c_str())); // mediamtx takes its config path as a bare positional arg
        argv.push_back(nullptr);

        pid_t pid = -1;
        int   rc  = posix_spawn(&pid, bin.c_str(), &actions, nullptr, argv.data(), *_NSGetEnviron());
        posix_spawn_file_actions_destroy(&actions);
        close(stdout_pipe[1]); // parent doesn't need the write end - the child has its dup'd copies

        if (rc != 0) {
            close(stdout_pipe[0]);
            post_status(std::string("error could not launch mediamtx: ") + strerror(rc));
            m_running.store(false);
            return;
        }

        int log_fd = stdout_pipe[0];
        post_status(std::string("status mediamtx running (pid ") + std::to_string(pid) + ")");

        bool                                   sigterm_sent = false;
        std::chrono::steady_clock::time_point  term_deadline;
        std::string                            linebuf;
        char                                   chunk[4096];

        for (;;) {
            struct pollfd pfd { log_fd, POLLIN, 0 };
            int pr = poll(&pfd, 1, 100); // 100ms tick - keeps log forwarding snappy and stop_requested checks prompt

            if (pr > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
                ssize_t n = read(log_fd, chunk, sizeof(chunk));
                if (n > 0) {
                    linebuf.append(chunk, (size_t)n);
                    size_t pos;
                    while ((pos = linebuf.find('\n')) != std::string::npos) {
                        post_status("log " + linebuf.substr(0, pos));
                        linebuf.erase(0, pos + 1);
                    }
                }
                else {
                    break; // EOF or read error - mediamtx's stdout/stderr closed, i.e. it has exited
                }
            }

            if (!sigterm_sent && m_stop_requested.load(std::memory_order_relaxed)) {
                kill(pid, SIGTERM);
                sigterm_sent  = true;
                term_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            }
            if (sigterm_sent && std::chrono::steady_clock::now() >= term_deadline) {
                post_status("status mediamtx not responding to SIGTERM - force killing");
                kill(pid, SIGKILL);
                term_deadline = std::chrono::steady_clock::now() + std::chrono::hours(24); // don't repeat every tick
            }
        }

        if (!linebuf.empty())
            post_status("log " + linebuf);
        close(log_fd);

        int status = 0;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status))
            post_status("status mediamtx stopped (exit code " + std::to_string(WEXITSTATUS(status)) + ")");
        else if (WIFSIGNALED(status))
            // The expected path for a normal 'stop' (we SIGTERM/SIGKILL it ourselves above), not a failure.
            post_status("status mediamtx stopped (signal " + std::to_string(WTERMSIG(status)) + ")");
        else
            post_status("status mediamtx stopped");

        m_running.store(false);
    }
};

MIN_EXTERNAL(jit_rtmp_server);
