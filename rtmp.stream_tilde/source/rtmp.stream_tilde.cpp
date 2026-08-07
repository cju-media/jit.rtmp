/// @file
/// rtmp.stream~ - Stream MSP audio + Jitter video to an RTMP server.
///
/// Encodes incoming audio (stereo signal inlets) with AAC and incoming video
/// (jit_matrix messages) with H.264, muxes them into FLV, and pushes the result
/// to an RTMP URL via libavformat. All encoding/network I/O happens on a
/// dedicated background thread so Max's audio and scheduler threads never block.

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libavutil/channel_layout.h>
#include <libavutil/audio_fifo.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include "c74_min.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace c74::min;

namespace {

constexpr int kAudioChannels    = 2;
constexpr int kRingCapacityPow2 = 1 << 18; // frames per channel worth of interleaved storage slots (see below)
constexpr int kRingMask         = kRingCapacityPow2 - 1;

// ---------------------------------------------------------------------------
// Lock-free single-producer/single-consumer ring buffer for interleaved
// stereo audio (doubles). Producer = Max audio thread. Consumer = encoder
// thread. No locks, no allocation on the hot path.
// ---------------------------------------------------------------------------
class audio_ring {
public:
    audio_ring() { m_data.resize(kRingCapacityPow2 * kAudioChannels, 0.0); }

    // Called from the audio thread. Never blocks. Drops audio if the
    // consumer has fallen too far behind (better than an unbounded stream
    // going out of sync / running out of memory).
    void write(double* const* channels, long frame_count) {
        size_t w = m_write.load(std::memory_order_relaxed);
        size_t r = m_read.load(std::memory_order_acquire);
        size_t free_frames = kRingCapacityPow2 - (w - r);

        if ((size_t)frame_count > free_frames) {
            // Overflow: drop the oldest data by advancing the read pointer.
            // This keeps latency bounded instead of growing without limit.
            size_t drop = frame_count - free_frames;
            m_read.store(r + drop, std::memory_order_release);
        }

        for (long i = 0; i < frame_count; ++i) {
            size_t idx = (w + (size_t)i) & kRingMask;
            for (int c = 0; c < kAudioChannels; ++c) {
                m_data[idx * kAudioChannels + c] = channels[c][i];
            }
        }
        m_write.store(w + (size_t)frame_count, std::memory_order_release);
    }

    // Called from the encoder thread only.
    size_t available_frames() const {
        size_t w = m_write.load(std::memory_order_acquire);
        size_t r = m_read.load(std::memory_order_relaxed);
        return w - r;
    }

    // Reads up to frame_count frames (interleaved) into dst. Returns frames actually read.
    size_t read(double* dst_interleaved, size_t frame_count) {
        size_t avail = available_frames();
        size_t n     = std::min(avail, frame_count);
        size_t r     = m_read.load(std::memory_order_relaxed);

        for (size_t i = 0; i < n; ++i) {
            size_t idx = (r + i) & kRingMask;
            for (int c = 0; c < kAudioChannels; ++c) {
                dst_interleaved[i * kAudioChannels + c] = m_data[idx * kAudioChannels + c];
            }
        }
        m_read.store(r + n, std::memory_order_release);
        return n;
    }

    void reset() {
        m_read.store(0);
        m_write.store(0);
    }

private:
    std::vector<double>     m_data;
    std::atomic<size_t>     m_read { 0 };
    std::atomic<size_t>     m_write { 0 };
};

// Small helper: deferred status/error messages need to cross from the encoder
// thread back to Max's main thread. We stash the latest message here and
// nudge a c74::min::queue<> (qelem) to drain it on the main thread.
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

class rtmp_stream : public object<rtmp_stream>, public mc_operator<> {
public:
    MIN_DESCRIPTION { "Stream MSP audio and Jitter video to an RTMP server as H.264/AAC/FLV." };
    MIN_TAGS { "audio, video, streaming, rtmp" };
    MIN_AUTHOR { "Cameron Johnston" };
    MIN_RELATED { "jit.record, adstatus, jit.qt.record" };

    inlet<>  in_messages { this, "(anything) jit_matrix / jit_gl_texture / start / stop / url / etc." };
    inlet<>  in_audio_mc { this, "(multichannelsignal) audio in - any channel count, downmixed to stereo for the stream" };
    outlet<> out_status  { this, "(anything) status / error messages" };

    rtmp_stream(const atoms& args = {}) {
        if (!args.empty())
            url = args[0];
        m_status_queue_ptr = std::make_unique<queue<>>(this, MIN_FUNCTION {
            drain_status();
            return atoms {};
        });
        // Polls the internal jit.gl.asyncread helper's own registered readback
        // matrix directly (see poll_gl_asyncread_frame()) rather than relying on
        // its outlet - confirmed live that the internal hidden patch connection
        // never actually delivers anything, so we don't depend on it at all now.
        m_gl_poll_timer_ptr = std::make_unique<timer<>>(this, MIN_FUNCTION {
            poll_gl_asyncread_frame();
            if (m_gl_asyncread_box && m_streaming.load(std::memory_order_relaxed))
                m_gl_poll_timer_ptr->delay(1000.0 / std::max(1.0, (double)fps));
            return atoms {};
        });
    }

    ~rtmp_stream() {
        stop_streaming();
    }

    // -----------------------------------------------------------------
    // Attributes
    // -----------------------------------------------------------------
    attribute<symbol> url { this, "url", "",
        description { "RTMP URL to publish to, e.g. rtmp://a.rtmp.youtube.com/live2/STREAM-KEY" }
    };

    attribute<int> out_width { this, "width", 1280,
        description { "Output video width in pixels." }
    };

    attribute<int> out_height { this, "height", 720,
        description { "Output video height in pixels." }
    };

    attribute<number> fps { this, "fps", 30.0,
        description { "Output video frame rate." }
    };

    attribute<int> video_kbps { this, "video_kbps", 4000,
        description { "Video encoder target bitrate in kbit/s." }
    };

    attribute<int> audio_kbps { this, "audio_kbps", 160,
        description { "Audio (AAC) encoder target bitrate in kbit/s." }
    };

    attribute<int> audio_samplerate { this, "samplerate", 48000,
        description { "Sample rate (Hz) the audio is encoded at. Input is resampled to this rate." }
    };

    attribute<bool> hwaccel { this, "hwaccel", false,
        description {
            "Use hardware-accelerated H.264 encoding (VideoToolbox) instead of libx264. "
            "Defaults off: VideoToolbox has shown RTMP/FLV compatibility issues in testing "
            "(see README) that libx264 doesn't have. Try turning this on once the libx264 "
            "path is confirmed working end-to-end, if you want the lower CPU usage."
        }
    };

    attribute<int> keyframe_interval { this, "keyframe_interval", 60,
        description { "Keyframe (GOP) interval in frames. 0 = derive from fps * 2." }
    };

    attribute<symbol> gl_context { this, "gl_context", "",
        description {
            "Name of a jit.gl context (e.g. a jit.world's @name) to stream directly, "
            "grabbing its whole composited output every render pass via an internal "
            "jit.gl.asyncread. No patch cord needed for this - set this and 'start'. "
            "Leave empty to feed video via jit_matrix / jit_gl_texture messages instead."
        }
    };

    // -----------------------------------------------------------------
    // Messages
    // -----------------------------------------------------------------
    message<> start { this, "start", "Connect and begin streaming to the configured url.",
        MIN_FUNCTION {
            start_streaming();
            return {};
        }
    };

    message<> stop { this, "stop", "Stop streaming and disconnect.",
        MIN_FUNCTION {
            stop_streaming();
            return {};
        }
    };

    message<> jit_matrix { this, "jit_matrix", "Receive a video frame from Jitter.",
        MIN_FUNCTION {
            handle_jit_matrix(args);
            return {};
        }
    };

    // A jit.gl.* object connected directly to the inlet sends this instead of
    // jit_matrix. We don't read GL textures ourselves - we lazily instantiate a
    // hidden internal jit.gl.asyncread (the same object you'd patch in by hand)
    // and point it at the named texture; it does the real (async, non-stalling)
    // GPU->CPU readback and hands us back an ordinary jit_matrix, which is fed
    // straight into the existing handle_jit_matrix() pipeline below.
    message<> jit_gl_texture { this, "jit_gl_texture", "Receive a video frame from a jit.gl texture.",
        MIN_FUNCTION {
            handle_gl_texture(args);
            return {};
        }
    };

    // The internal jit.gl.asyncread helper, once created, keeps reading its GL
    // context/texture continuously for the rest of the streaming session,
    // completely decoupled from whether a patch cord is still actually connected
    // to us - there's no signal that tells us "the user moved on". If you're
    // switching between a GL source and a jit_matrix source live, send this
    // first (before/instead of relying on us to notice) to stop it, or you'll
    // get both sources fighting over the same output frame (visible flashing).
    message<> gl_disconnect { this, "gl_disconnect", "Stop and tear down the internal jit.gl.asyncread helper, if one exists.",
        MIN_FUNCTION {
            teardown_gl_asyncread_helper();
            post_status("status internal jit.gl.asyncread disconnected");
            return {};
        }
    };

    // -----------------------------------------------------------------
    // Audio: called on Max's audio thread. Must never block or allocate.
    //
    // in_audio_mc is MC-capable and carries any number of channels; the stream
    // itself is always stereo AAC (what every real RTMP consumer expects), so
    // we downmix here: 1 channel duplicates to L/R, 2+ channels alternate
    // (even index -> L, odd index -> R), each side averaged over however many
    // channels actually land on it, to avoid gain buildup when more than 2
    // channels are connected.
    // -----------------------------------------------------------------
    void operator()(audio_bundle input, audio_bundle output) {
        if (!m_streaming.load(std::memory_order_relaxed))
            return;

        long ch_count    = input.channel_count();
        long frame_count = input.frame_count();

        m_downmix_l.assign(frame_count, 0.0);
        m_downmix_r.assign(frame_count, 0.0);
        std::vector<double>& l_buf = m_downmix_l;
        std::vector<double>& r_buf = m_downmix_r;

        if (ch_count <= 0) {
            // nothing connected - write silence so the ring buffer/encoder still advances
        }
        else if (ch_count == 1) {
            double* mono = input.samples(0);
            for (long i = 0; i < frame_count; ++i)
                l_buf[i] = r_buf[i] = mono[i];
        }
        else {
            long l_n = 0, r_n = 0;
            for (long c = 0; c < ch_count; ++c) {
                double* s = input.samples(c);
                double* dst = (c % 2 == 0) ? l_buf.data() : r_buf.data();
                for (long i = 0; i < frame_count; ++i)
                    dst[i] += s[i];
                if (c % 2 == 0) l_n++; else r_n++;
            }
            if (l_n > 1) for (long i = 0; i < frame_count; ++i) l_buf[i] /= l_n;
            if (r_n > 1) for (long i = 0; i < frame_count; ++i) r_buf[i] /= r_n;
        }

        double* chans[kAudioChannels] = { l_buf.data(), r_buf.data() };
        m_audio_ring.write(chans, frame_count);
    }

private:
    // === Video conversion state (touched by main/Jitter thread + protected by mutex for encoder thread reads) ===
    std::mutex m_video_mutex;
    AVFrame*   m_latest_video_frame = nullptr; // owned; always a fully-converted YUV420P frame at output resolution
    SwsContext* m_sws_ctx            = nullptr;
    long        m_sws_src_w = 0, m_sws_src_h = 0;
    AVPixelFormat m_sws_src_fmt = AV_PIX_FMT_NONE;

    // === Internal jit.gl.asyncread helper (GL texture readback), main/Jitter thread only ===
    c74::max::t_object* m_gl_asyncread_box = nullptr;
    symbol          m_gl_asyncread_texture { "" };
    symbol          m_gl_asyncread_out_name { "" }; // name of its internally-registered readback matrix; see poll_gl_asyncread_frame()

    // === Audio ring buffer (lock-free) ===
    audio_ring m_audio_ring;

    // Scratch downmix buffers for operator() (audio thread only). Sized lazily
    // on first use per DSP compile; std::vector::assign doesn't reallocate once
    // already the right size, so this is a one-time cost, not a per-block one.
    std::vector<double> m_downmix_l, m_downmix_r;

    // === Encoder thread & FFmpeg state (owned exclusively by encoder thread once started) ===
    std::thread       m_encoder_thread;
    std::atomic<bool> m_streaming { false };
    std::atomic<bool> m_stop_requested { false };

    AVFormatContext* m_fmt_ctx      = nullptr;
    AVCodecContext*  m_video_ctx    = nullptr;
    AVCodecContext*  m_audio_ctx    = nullptr;
    AVStream*        m_video_stream = nullptr;
    AVStream*        m_audio_stream = nullptr;
    SwrContext*      m_swr_ctx      = nullptr;
    AVAudioFifo*     m_audio_fifo   = nullptr;

    status_mailbox              m_mailbox;
    std::unique_ptr<queue<>>    m_status_queue_ptr;
    std::unique_ptr<timer<>>    m_gl_poll_timer_ptr;

    void post_status(const std::string& msg) {
        m_mailbox.post(msg);
        if (m_status_queue_ptr)
            m_status_queue_ptr->set();
    }

    void drain_status() {
        for (auto& msg : m_mailbox.drain())
            out_status.send(symbol(msg));
    }

    // -----------------------------------------------------------------
    // Video: receive + convert a jit_matrix. Runs on whatever thread
    // delivered the message (main / Jitter scheduler) - not the audio thread.
    // -----------------------------------------------------------------
    int m_jit_matrix_calls_logged = 0;

    void handle_jit_matrix(const atoms& args) {
        if (args.empty())
            return;

        // A real jit_matrix message (this is only ever called for those - our
        // own internal GL polling calls convert_and_store_frame() directly,
        // bypassing this) means the user has switched to a matrix source.
        // Auto-disconnect the GL helper so it stops fighting over the shared
        // output frame - the two sources are mutually exclusive by design.
        // Switching back to GL still works normally afterward (a new
        // jit_gl_texture message or @gl_context recreates it on demand).
        if (m_gl_asyncread_box) {
            teardown_gl_asyncread_helper();
            post_status("status jit_matrix arrived - auto-disconnected internal jit.gl.asyncread");
        }

        if (m_jit_matrix_calls_logged < 5) {
            m_jit_matrix_calls_logged++;
            post_status(std::string("status jit_matrix received: ") + (args[0].type() == message_type::symbol_argument
                                                                        ? std::string(symbol(args[0]))
                                                                        : std::string("<non-symbol arg>")));
        }

        symbol mname  = args[0];
        void*  matrix = c74::max::jit_object_findregistered(mname);
        if (!matrix || !c74::max::jit_object_method(matrix, c74::max::_jit_sym_class_jit_matrix))
            return;

        long savelock = (long)c74::max::jit_object_method(matrix, c74::max::_jit_sym_lock, 1);

        c74::max::t_jit_matrix_info info;
        char* bp = nullptr;
        c74::max::jit_object_method(matrix, c74::max::_jit_sym_getinfo, &info);
        c74::max::jit_object_method(matrix, c74::max::_jit_sym_getdata, &bp);

        if (bp && info.type == c74::max::_jit_sym_char && (info.planecount == 4 || info.planecount == 3)) {
            convert_and_store_frame(info, (unsigned char*)bp);
        }
        else if (bp) {
            post_status("error jit_matrix must be char type with 3 or 4 planes (use jit.matrix 4 char WxH)");
        }

        c74::max::jit_object_method(matrix, c74::max::_jit_sym_lock, (void*)savelock);
    }

    // Allocates a solid-black YUV420P frame at the configured output resolution.
    // Caller must hold m_video_mutex. Returns nullptr on allocation failure.
    AVFrame* create_black_frame() {
        AVFrame* frame = av_frame_alloc();
        frame->format = AV_PIX_FMT_YUV420P;
        frame->width  = out_width;
        frame->height = out_height;
        if (av_frame_get_buffer(frame, 32) < 0) {
            av_frame_free(&frame);
            return nullptr;
        }
        // Limited-range black: Y=16, U=V=128.
        std::memset(frame->data[0], 16, (size_t)frame->linesize[0] * frame->height);
        std::memset(frame->data[1], 128, (size_t)frame->linesize[1] * (frame->height / 2));
        std::memset(frame->data[2], 128, (size_t)frame->linesize[2] * (frame->height / 2));
        return frame;
    }

    void convert_and_store_frame(const c74::max::t_jit_matrix_info& info, unsigned char* data) {
        if (!m_streaming.load(std::memory_order_relaxed))
            return;

        long src_w = info.dim[0];
        long src_h = (info.dimcount > 1) ? info.dim[1] : 1;
        if (src_w <= 0 || src_h <= 0)
            return;

        AVPixelFormat src_fmt = (info.planecount == 4) ? AV_PIX_FMT_ARGB : AV_PIX_FMT_RGB24;
        int dst_w = out_width, dst_h = out_height;

        // (Re)build the scaler if the source format/size changed.
        if (!m_sws_ctx || src_w != m_sws_src_w || src_h != m_sws_src_h || src_fmt != m_sws_src_fmt) {
            if (m_sws_ctx) {
                sws_freeContext(m_sws_ctx);
                m_sws_ctx = nullptr;
            }
            m_sws_ctx = sws_getContext(
                (int)src_w, (int)src_h, src_fmt,
                dst_w, dst_h, AV_PIX_FMT_YUV420P,
                SWS_BILINEAR, nullptr, nullptr, nullptr
            );
            m_sws_src_w   = src_w;
            m_sws_src_h   = src_h;
            m_sws_src_fmt = src_fmt;
            if (!m_sws_ctx) {
                post_status("error could not create video scaler");
                return;
            }
        }

        AVFrame* frame = av_frame_alloc();
        frame->format  = AV_PIX_FMT_YUV420P;
        frame->width   = dst_w;
        frame->height  = dst_h;
        if (av_frame_get_buffer(frame, 32) < 0) {
            av_frame_free(&frame);
            return;
        }

        const uint8_t* src_slices[1] = { data };
        int            src_stride[1] = { (int)info.dimstride[1] };

        sws_scale(m_sws_ctx, src_slices, src_stride, 0, (int)src_h, frame->data, frame->linesize);

        {
            std::lock_guard<std::mutex> lock(m_video_mutex);
            if (m_latest_video_frame)
                av_frame_free(&m_latest_video_frame);
            m_latest_video_frame = frame;
        }
    }

    // -----------------------------------------------------------------
    // GL texture input: delegate the actual GPU->CPU readback to a hidden,
    // internally-instantiated jit.gl.asyncread rather than touching OpenGL
    // ourselves. This reuses Cycling'74's own (closed-source) implementation,
    // so we inherit correct GL-context/thread handling and PBO double-
    // buffering for free instead of reimplementing it against undocumented
    // internals. We read its output by polling its own registered readback
    // matrix directly (see poll_gl_asyncread_frame()), not via its outlet.
    // -----------------------------------------------------------------
    int m_gl_texture_calls_logged = 0;

    void handle_gl_texture(const atoms& args) {
        if (m_gl_texture_calls_logged < 3) {
            m_gl_texture_calls_logged++;
            post_status(std::string("status jit_gl_texture message received (streaming=")
                        + (m_streaming.load() ? "1" : "0") + ", args_empty=" + (args.empty() ? "1" : "0") + ")");
        }
        if (!m_streaming.load(std::memory_order_relaxed) || args.empty())
            return;

        symbol texname = args[0];

        if (!ensure_gl_asyncread_helper())
            return;

        if (texname != m_gl_asyncread_texture) {
            c74::max::object_attr_setsym(m_gl_asyncread_box, c74::max::gensym("texture"), texname);
            m_gl_asyncread_texture = texname;
        }
        // @automatic 1 (set at creation) keeps it reading on every render pass;
        // no explicit bang needed here.
    }

    // Lazily create (once per streaming session) a hidden jit.gl.asyncread
    // instance in our own patcher, configured to output matrices, and wire
    // its outlet 0 directly into our own inlet 0 with a hidden patchline -
    // equivalent to the user patching `jit.gl.asyncread -> rtmp.stream~` by
    // hand, just done for them. Returns false (and posts a status message)
    // if any step fails, e.g. because we can't resolve our owning patcher.
    //
    // If drawto_context is non-empty, the helper targets that named jit.gl
    // context directly (@drawto) and reads its whole composited framebuffer
    // every render pass with no incoming texture message required - this is
    // the "just stream my jit.world" path driven by the @gl_context attribute.
    // Otherwise it's left targeting whatever @texture handle_gl_texture() sets.
    bool ensure_gl_asyncread_helper(const std::string& drawto_context = "") {
        if (m_gl_asyncread_box)
            return true;

        c74::max::t_object* patcher_obj = patcher();
        if (!patcher_obj) {
            post_status("error could not locate owning patcher for internal jit.gl.asyncread");
            return false;
        }

        // @automatic 0: we drive capture ourselves with an explicit "bang" every poll
        // tick (see poll_gl_asyncread_frame()). Having both @automatic's own
        // Jitter-render-tied triggering AND our independent timer both firing reads
        // against the same async PBO double-buffer is a plausible way to actually
        // corrupt its internal state rather than just leave it stale - reported live:
        // starting a stream from a texture went from "just black" to "cannot stream
        // at all" the moment the explicit bang was added alongside @automatic 1.
        // One trigger source only, now.
        std::string spec = "@maxclass jit.gl.asyncread @matrixoutput 1 @automatic 0";
        if (!drawto_context.empty())
            spec += " @drawto " + drawto_context;

        m_gl_asyncread_box = c74::max::newobject_sprintf(patcher_obj, "%s", spec.c_str());
        if (!m_gl_asyncread_box) {
            post_status("error could not instantiate internal jit.gl.asyncread - is Jitter installed?");
            return false;
        }
        c74::max::jbox_set_hidden(m_gl_asyncread_box, 1);

        // Confirmed live: the "hiddenconnect" patchline trick never actually
        // delivered the helper's output to us (zero jit_matrix messages ever
        // arrived, and every video packet was identical - just our black seed
        // frame, unchanged). Rather than debug an undocumented internal message
        // further, poll the helper's own registered readback matrix directly -
        // jit.gl.asyncread's "out_name" attribute (documented) gives us its name,
        // and we read it with the exact same jit_object_findregistered/lock/
        // getinfo/getdata sequence handle_jit_matrix already uses successfully.
        c74::max::t_symbol* out_name_sym = c74::max::object_attr_getsym(m_gl_asyncread_box, c74::max::gensym("out_name"));
        m_gl_asyncread_out_name = out_name_sym ? symbol(out_name_sym) : symbol("");

        post_status(std::string("status internal jit.gl.asyncread created, spec=[") + spec
                    + "], out_name=" + std::string(m_gl_asyncread_out_name));

        if (m_gl_asyncread_out_name == symbol("")) {
            post_status("error internal jit.gl.asyncread has no out_name - matrixoutput may not have taken effect");
            return false;
        }

        if (m_gl_poll_timer_ptr)
            m_gl_poll_timer_ptr->delay(1000.0 / std::max(1.0, (double)fps));

        return true;
    }

    int m_gl_poll_frames_logged = 0;

    // Called periodically by m_gl_poll_timer_ptr (main/scheduler thread) while
    // the helper is alive. Reads its internally-registered readback matrix
    // directly - see the comment in ensure_gl_asyncread_helper() for why.
    void poll_gl_asyncread_frame() {
        if (!m_gl_asyncread_box || m_gl_asyncread_out_name == symbol(""))
            return;

        // We drive capture exclusively via this explicit "bang" (documented in
        // jit.gl.asyncread's maxref) - @automatic is off (see ensure_gl_asyncread_helper())
        // to avoid two independent trigger sources racing the same async PBO
        // double-buffer, which is what broke streaming outright the one time both
        // were active at once. Originally added because a texture connected from
        // the start of a session stayed black indefinitely under @automatic alone,
        // but only started working once an unrelated jit_matrix message came
        // through first - consistent with @automatic's capture being tied to
        // Jitter's own render/scheduler tick rather than firing reliably on its
        // own from a cold start.
        c74::max::object_method(m_gl_asyncread_box, c74::max::gensym("bang"));

        void* matrix = c74::max::jit_object_findregistered(m_gl_asyncread_out_name);
        if (!matrix || !c74::max::jit_object_method(matrix, c74::max::_jit_sym_class_jit_matrix))
            return;

        long savelock = (long)c74::max::jit_object_method(matrix, c74::max::_jit_sym_lock, 1);

        c74::max::t_jit_matrix_info info;
        char* bp = nullptr;
        c74::max::jit_object_method(matrix, c74::max::_jit_sym_getinfo, &info);
        c74::max::jit_object_method(matrix, c74::max::_jit_sym_getdata, &bp);

        if (bp && info.dim[0] > 0 && (info.dimcount < 2 || info.dim[1] > 0)) {
            if (m_gl_poll_frames_logged < 3) {
                m_gl_poll_frames_logged++;
                post_status(std::string("status gl poll frame: type=") + std::string(symbol(info.type))
                            + " planecount=" + std::to_string(info.planecount) + " dim=" + std::to_string(info.dim[0])
                            + "x" + std::to_string(info.dimcount > 1 ? info.dim[1] : 1));
            }
            if (bp && info.type == c74::max::_jit_sym_char && (info.planecount == 4 || info.planecount == 3))
                convert_and_store_frame(info, (unsigned char*)bp);
        }

        c74::max::jit_object_method(matrix, c74::max::_jit_sym_lock, (void*)savelock);
    }

    void teardown_gl_asyncread_helper() {
        if (m_gl_asyncread_box) {
            c74::max::object_free(m_gl_asyncread_box);
            m_gl_asyncread_box = nullptr;
        }
        m_gl_asyncread_texture = symbol("");
        m_gl_asyncread_out_name = symbol(""); // m_gl_poll_timer_ptr's lambda checks m_gl_asyncread_box before rescheduling, so it stops on its own
    }

    // -----------------------------------------------------------------
    // Stream lifecycle
    // -----------------------------------------------------------------
    void start_streaming() {
        if (m_streaming.load()) {
            post_status("error already streaming - send 'stop' first");
            return;
        }
        if (!c74::max::sys_getdspstate()) {
            // A real Max console error (no print object / patch cord needed to see it) -
            // this is very easy to miss otherwise: with DSP off, operator() never runs,
            // so audio is silently never encoded (confirmed live: this alone was enough
            // to make the whole stream appear to never actually play on the far end,
            // not just be silent - see git log).
            cerr << "rtmp.stream~: DSP is off - turn on audio (e.g. click an ezdac~, or "
                 << "send \"start\" to a dac~) before sending 'start'. Refusing to start." << endl;
            post_status("error DSP is off - turn on audio before starting");
            return;
        }
        std::string dest = url.get().c_str();
        if (dest.empty()) {
            post_status("error no url set - set the url attribute first, e.g. @url rtmp://...");
            return;
        }

        m_stop_requested.store(false);
        m_audio_ring.reset();
        m_streaming.store(true); // set before ensure_gl_asyncread_helper() since handle_gl_texture() checks it

        // Seed a black frame immediately so the video stream always has *something*
        // to encode from the very first tick, regardless of whether/when a real video
        // source shows up. Otherwise, with no source connected yet (or patch cables
        // being moved around live), the video stream gets zero packets, which risks
        // stalling FFmpeg's interleaved writer for BOTH streams, not just leaving
        // video blank - the same class of problem the DSP-off bug turned out to be.
        // Any real frame that arrives afterward replaces this the normal way.
        {
            std::lock_guard<std::mutex> lock(m_video_mutex);
            if (!m_latest_video_frame)
                m_latest_video_frame = create_black_frame();
        }

        std::string ctx = gl_context.get().c_str();
        if (!ctx.empty())
            ensure_gl_asyncread_helper(ctx);

        m_encoder_thread = std::thread([this, dest]() {
            encoder_thread_main(dest);
        });
        post_status("status connecting");
    }

    void stop_streaming() {
        if (!m_streaming.load() && !m_encoder_thread.joinable())
            return;

        m_stop_requested.store(true);
        m_streaming.store(false);

        if (m_encoder_thread.joinable())
            m_encoder_thread.join();

        // Main-thread only (touches the Max object/patcher API) - stop_streaming()
        // is only ever called from a message handler or our destructor, both main thread.
        teardown_gl_asyncread_helper();

        std::lock_guard<std::mutex> lock(m_video_mutex);
        if (m_latest_video_frame)
            av_frame_free(&m_latest_video_frame);
        if (m_sws_ctx) {
            sws_freeContext(m_sws_ctx);
            m_sws_ctx = nullptr;
        }
    }

    static int interrupt_cb(void* ctx) {
        auto* self = static_cast<rtmp_stream*>(ctx);
        return self->m_stop_requested.load(std::memory_order_relaxed) ? 1 : 0;
    }

    // -----------------------------------------------------------------
    // Encoder thread: owns the AVFormatContext exclusively. Does all
    // encoding, muxing and network I/O. Never touches Max's audio path.
    // -----------------------------------------------------------------
    void encoder_thread_main(const std::string& dest) {
        int ret = 0;

        avformat_network_init();

        if (avformat_alloc_output_context2(&m_fmt_ctx, nullptr, "flv", dest.c_str()) < 0 || !m_fmt_ctx) {
            post_status("error could not create output context for " + dest);
            m_streaming.store(false);
            return;
        }

        m_fmt_ctx->interrupt_callback.callback = interrupt_cb;
        m_fmt_ctx->interrupt_callback.opaque   = this;

        if (!open_video_encoder() || !open_audio_encoder()) {
            cleanup_ffmpeg();
            m_streaming.store(false);
            return;
        }

        prime_video_encoder_extradata();

        if (!(m_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            ret = avio_open2(&m_fmt_ctx->pb, dest.c_str(), AVIO_FLAG_WRITE, &m_fmt_ctx->interrupt_callback, nullptr);
            if (ret < 0) {
                post_status("error could not open connection to " + dest);
                cleanup_ffmpeg();
                m_streaming.store(false);
                return;
            }
        }

        if (avformat_write_header(m_fmt_ctx, nullptr) < 0) {
            post_status("error could not write header (check url / server)");
            cleanup_ffmpeg();
            m_streaming.store(false);
            return;
        }

        post_status("status live");

        run_encode_loop();

        // Flush encoders
        flush_encoder(m_video_ctx, m_video_stream);
        flush_encoder(m_audio_ctx, m_audio_stream);
        av_write_trailer(m_fmt_ctx);

        cleanup_ffmpeg();
        post_status("status stopped");
    }

    // Some H.264 encoders - notably h264_videotoolbox - don't populate
    // AVCodecContext::extradata (SPS/PPS) until *after* they've actually
    // encoded a frame, unlike libx264 which has it immediately after
    // avcodec_open2(). We copy extradata into the stream's codecpar (via
    // avcodec_parameters_from_context in open_video_encoder) before any
    // frame exists, so for those encoders it comes up empty. FLV needs a
    // valid AVC sequence header written right after avformat_write_header,
    // or players receive bytes but can never decode a single video frame -
    // exactly "the stream shows as live/receiving but never plays".
    //
    // Fix: warm up the encoder with a throwaway frame (if one's available
    // yet) *before* writing the header, discard its output, then refresh
    // codecpar now that extradata should be populated. Force the next real
    // frame to be a fresh keyframe afterward so the bitstream we actually
    // transmit still starts clean.
    void prime_video_encoder_extradata() {
        using clock = std::chrono::steady_clock;
        auto deadline = clock::now() + std::chrono::seconds(3);

        AVFrame* warmup = nullptr;
        while (clock::now() < deadline && !m_stop_requested.load(std::memory_order_relaxed)) {
            {
                std::lock_guard<std::mutex> lock(m_video_mutex);
                if (m_latest_video_frame) {
                    warmup = av_frame_alloc();
                    av_frame_ref(warmup, m_latest_video_frame);
                }
            }
            if (warmup)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        if (!warmup) {
            post_status(std::string("status video priming: no frame arrived within 3s (extradata_size=")
                        + std::to_string(m_video_ctx->extradata_size) + ", codec=" + m_video_ctx->codec->name + ")");
            return; // no video arrived yet; proceed without priming (fine for encoders that don't need it, e.g. libx264)
        }

        int received_packets = 0;
        // Use a pts strictly before the real stream's counter (which starts at 0 in
        // run_encode_loop) - sending two frames with the same pts=0 back to back
        // is a non-monotonic-timestamp violation most encoders are strict about,
        // and could corrupt the DTS/PTS actually written for the real stream even
        // though this warmup packet itself is discarded and never reaches the wire.
        warmup->pts = -1;
        if (avcodec_send_frame(m_video_ctx, warmup) == 0) {
            AVPacket* pkt = av_packet_alloc();
            while (avcodec_receive_packet(m_video_ctx, pkt) == 0) {
                received_packets++;
                av_packet_unref(pkt); // discard - the muxer isn't open yet
            }
            av_packet_free(&pkt);
        }
        av_frame_free(&warmup);

        m_force_next_video_keyframe = true;
        avcodec_parameters_from_context(m_video_stream->codecpar, m_video_ctx); // refresh with now-populated extradata

        post_status(std::string("status video priming: codec=") + m_video_ctx->codec->name
                    + " warmup_packets=" + std::to_string(received_packets)
                    + " extradata_size=" + std::to_string(m_video_ctx->extradata_size));
    }

    bool open_video_encoder() {
        const AVCodec* codec = nullptr;
        if (hwaccel)
            codec = avcodec_find_encoder_by_name("h264_videotoolbox");
        if (!codec)
            codec = avcodec_find_encoder_by_name("libx264");
        if (!codec)
            codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!codec) {
            post_status("error no H.264 encoder available");
            return false;
        }

        m_video_stream = avformat_new_stream(m_fmt_ctx, nullptr);
        if (!m_video_stream)
            return false;

        m_video_ctx = avcodec_alloc_context3(codec);
        m_video_ctx->width     = out_width;
        m_video_ctx->height    = out_height;
        m_video_ctx->pix_fmt   = AV_PIX_FMT_YUV420P;
        double fps_val         = std::max(1.0, (double)fps);
        m_video_ctx->time_base = av_d2q(1.0 / fps_val, 100000);
        m_video_ctx->framerate = av_d2q(fps_val, 100000);
        m_video_ctx->bit_rate  = (int64_t)video_kbps * 1000;
        m_video_ctx->gop_size  = (keyframe_interval > 0) ? (int)keyframe_interval : (int)std::lround(fps_val * 2.0);
        m_video_ctx->max_b_frames = 0; // low latency

        if (m_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            m_video_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        AVDictionary* opts = nullptr;
        if (std::strcmp(codec->name, "libx264") == 0) {
            av_dict_set(&opts, "preset", "veryfast", 0);
            av_dict_set(&opts, "tune", "zerolatency", 0);
        }

        if (avcodec_open2(m_video_ctx, codec, &opts) < 0) {
            av_dict_free(&opts);
            post_status(std::string("error could not open video encoder ") + codec->name);
            return false;
        }
        av_dict_free(&opts);

        if (avcodec_parameters_from_context(m_video_stream->codecpar, m_video_ctx) < 0)
            return false;
        m_video_stream->time_base = m_video_ctx->time_base;

        return true;
    }

    bool open_audio_encoder() {
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!codec) {
            post_status("error no AAC encoder available");
            return false;
        }

        m_audio_stream = avformat_new_stream(m_fmt_ctx, nullptr);
        if (!m_audio_stream)
            return false;

        m_audio_ctx = avcodec_alloc_context3(codec);
        m_audio_ctx->sample_rate    = audio_samplerate;
        m_audio_ctx->sample_fmt     = AV_SAMPLE_FMT_FLTP;
        av_channel_layout_default(&m_audio_ctx->ch_layout, kAudioChannels);
        m_audio_ctx->bit_rate       = (int64_t)audio_kbps * 1000;
        m_audio_ctx->time_base      = AVRational { 1, audio_samplerate };

        if (m_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            m_audio_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (avcodec_open2(m_audio_ctx, codec, nullptr) < 0) {
            post_status("error could not open AAC encoder");
            return false;
        }

        if (avcodec_parameters_from_context(m_audio_stream->codecpar, m_audio_ctx) < 0)
            return false;
        m_audio_stream->time_base = m_audio_ctx->time_base;

        // Resampler: Max delivers interleaved double at the DSP samplerate;
        // the AAC encoder wants planar float at audio_samplerate.
        AVChannelLayout in_layout;
        av_channel_layout_default(&in_layout, kAudioChannels);
        double dsp_sr = samplerate(); // from vector_operator<> base - current DSP samplerate
        int    in_sr  = dsp_sr > 0 ? (int)std::lround(dsp_sr) : audio_samplerate.get();

        m_source_samplerate = in_sr;

        int ret = swr_alloc_set_opts2(
            &m_swr_ctx,
            &m_audio_ctx->ch_layout, AV_SAMPLE_FMT_FLTP, audio_samplerate,
            &in_layout, AV_SAMPLE_FMT_DBL, in_sr,
            0, nullptr
        );
        av_channel_layout_uninit(&in_layout);
        if (ret < 0 || !m_swr_ctx || swr_init(m_swr_ctx) < 0) {
            post_status("error could not initialize audio resampler");
            return false;
        }

        m_audio_fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, kAudioChannels, 1);
        return m_audio_fifo != nullptr;
    }

    int m_source_samplerate = 48000;
    bool m_force_next_video_keyframe = false; // set by prime_video_encoder_extradata(), consumed in run_encode_loop()

    void flush_encoder(AVCodecContext* ctx, AVStream* stream) {
        if (!ctx)
            return;
        avcodec_send_frame(ctx, nullptr);
        AVPacket* pkt = av_packet_alloc();
        while (avcodec_receive_packet(ctx, pkt) == 0) {
            write_packet(ctx, stream, pkt);
        }
        av_packet_free(&pkt);
    }

    int m_video_packets_logged = 0;
    int m_audio_packets_logged = 0;

    void write_packet(AVCodecContext* ctx, AVStream* stream, AVPacket* pkt) {
        pkt->stream_index = stream->index;
        av_packet_rescale_ts(pkt, ctx->time_base, stream->time_base);

        bool is_video = (stream == m_video_stream);
        int& logged = is_video ? m_video_packets_logged : m_audio_packets_logged;
        bool should_log = logged < 5;
        int64_t pts = pkt->pts, dts = pkt->dts;
        int size = pkt->size;
        bool key = (pkt->flags & AV_PKT_FLAG_KEY) != 0;

        int ret = av_interleaved_write_frame(m_fmt_ctx, pkt); // pkt is consumed/unref'd by this call regardless of result

        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE] = { 0 };
            av_strerror(ret, errbuf, sizeof(errbuf));
            post_status(std::string("error av_interleaved_write_frame failed (") + (is_video ? "video" : "audio")
                        + "): " + errbuf);
        }
        else if (should_log) {
            logged++;
            post_status(std::string("status ") + (is_video ? "video" : "audio") + " packet #" + std::to_string(logged)
                        + " pts=" + std::to_string(pts) + " dts=" + std::to_string(dts) + " size=" + std::to_string(size)
                        + (is_video ? (std::string(" key=") + (key ? "1" : "0")) : ""));
        }
    }

    // -----------------------------------------------------------------
    // Main encode/mux loop - single thread services both audio and video
    // so calls into m_fmt_ctx are never concurrent.
    // -----------------------------------------------------------------
    void run_encode_loop() {
        using clock = std::chrono::steady_clock;

        double frame_dur_s   = 1.0 / std::max(1.0, (double)fps);
        auto   frame_dur     = std::chrono::duration<double>(frame_dur_s);
        auto   next_video_tick = clock::now();
        int64_t video_pts_counter = 0;
        int64_t audio_samples_written = 0;

        const int aac_frame_size = m_audio_ctx->frame_size > 0 ? m_audio_ctx->frame_size : 1024;

        std::vector<double> audio_scratch(4096 * kAudioChannels);
        AVPacket* pkt = av_packet_alloc();

        while (!m_stop_requested.load(std::memory_order_relaxed)) {
            bool did_work = false;

            // ---- Audio: drain whatever is available from the ring buffer ----
            size_t avail = m_audio_ring.available_frames();
            if (avail > 0) {
                size_t to_read = std::min(avail, audio_scratch.size() / kAudioChannels);
                size_t got     = m_audio_ring.read(audio_scratch.data(), to_read);
                if (got > 0) {
                    const uint8_t* in_data[1] = { (const uint8_t*)audio_scratch.data() };
                    // swr needs a temp planar float output buffer sized for worst-case resample ratio
                    int out_max = (int)av_rescale_rnd(got, audio_samplerate, m_source_samplerate, AV_ROUND_UP) + 32;

                    uint8_t** converted = nullptr;
                    av_samples_alloc_array_and_samples(&converted, nullptr, kAudioChannels, out_max, AV_SAMPLE_FMT_FLTP, 0);

                    int converted_count = swr_convert(m_swr_ctx, converted, out_max, in_data, (int)got);
                    if (converted_count > 0) {
                        av_audio_fifo_write(m_audio_fifo, (void**)converted, converted_count);
                    }
                    av_freep(&converted[0]);
                    av_freep(&converted);
                    did_work = true;
                }
            }

            while (av_audio_fifo_size(m_audio_fifo) >= aac_frame_size) {
                AVFrame* aframe = av_frame_alloc();
                aframe->nb_samples = aac_frame_size;
                aframe->format     = AV_SAMPLE_FMT_FLTP;
                av_channel_layout_copy(&aframe->ch_layout, &m_audio_ctx->ch_layout);
                aframe->sample_rate = audio_samplerate;
                av_frame_get_buffer(aframe, 0);

                av_audio_fifo_read(m_audio_fifo, (void**)aframe->data, aac_frame_size);
                aframe->pts = audio_samples_written;
                audio_samples_written += aac_frame_size;

                if (avcodec_send_frame(m_audio_ctx, aframe) == 0) {
                    while (avcodec_receive_packet(m_audio_ctx, pkt) == 0)
                        write_packet(m_audio_ctx, m_audio_stream, pkt);
                }
                av_frame_free(&aframe);
                did_work = true;
            }

            // ---- Video: emit at a steady cadence, reusing the latest frame if none is new ----
            auto now = clock::now();
            if (now >= next_video_tick) {
                AVFrame* to_encode = nullptr;
                {
                    std::lock_guard<std::mutex> lock(m_video_mutex);
                    if (m_latest_video_frame) {
                        to_encode = av_frame_alloc();
                        av_frame_ref(to_encode, m_latest_video_frame);
                    }
                }
                if (to_encode) {
                    to_encode->pts = video_pts_counter++;
                    if (m_force_next_video_keyframe) {
                        // AV_FRAME_FLAG_KEY is not actually respected as a "force a
                        // keyframe here" request by libx264 (confirmed live: every
                        // packet, including this one, still came out key=0). The
                        // real, documented mechanism is pict_type.
                        to_encode->pict_type = AV_PICTURE_TYPE_I;
                        to_encode->flags |= AV_FRAME_FLAG_KEY;
                        m_force_next_video_keyframe = false;
                    }
                    if (avcodec_send_frame(m_video_ctx, to_encode) == 0) {
                        while (avcodec_receive_packet(m_video_ctx, pkt) == 0)
                            write_packet(m_video_ctx, m_video_stream, pkt);
                    }
                    av_frame_free(&to_encode);
                }
                next_video_tick += std::chrono::duration_cast<clock::duration>(frame_dur);
                if (next_video_tick < now) // don't try to catch up after a stall
                    next_video_tick = now + std::chrono::duration_cast<clock::duration>(frame_dur);
                did_work = true;
            }

            if (!did_work)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        av_packet_free(&pkt);
    }

    void cleanup_ffmpeg() {
        if (m_swr_ctx) { swr_free(&m_swr_ctx); }
        if (m_audio_fifo) { av_audio_fifo_free(m_audio_fifo); m_audio_fifo = nullptr; }
        if (m_video_ctx) { avcodec_free_context(&m_video_ctx); }
        if (m_audio_ctx) { avcodec_free_context(&m_audio_ctx); }
        if (m_fmt_ctx) {
            if (m_fmt_ctx->pb && !(m_fmt_ctx->oformat->flags & AVFMT_NOFILE))
                avio_closep(&m_fmt_ctx->pb);
            avformat_free_context(m_fmt_ctx);
            m_fmt_ctx = nullptr;
        }
        m_video_stream = nullptr;
        m_audio_stream = nullptr;
    }
};

MIN_EXTERNAL(rtmp_stream);
