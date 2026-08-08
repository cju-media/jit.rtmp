/// @file
/// jit.rtmp.receive~ - Run an RTMP server that receives a published stream as
/// Jitter video + MSP audio.
///
/// Uses libavformat's native RTMP protocol in "listen" mode - i.e. this object
/// *is* the RTMP server. Point an encoder (OBS, ffmpeg, another Max patch
/// running jit.rtmp.send~, etc.) at the configured @url and this object
/// accepts the incoming connection, demuxes the FLV container, decodes H.264
/// video into a Jitter matrix and AAC (or whatever's actually in the stream)
/// audio into a multichannel signal outlet (stereo by default; see the
/// @channels attribute). All network I/O and decoding happens on a
/// dedicated background thread so Max's audio and scheduler threads never
/// block; after a publisher disconnects the server goes back to listening for
/// the next one, until 'stop' is sent.

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include "c74_min.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace c74::min;

namespace {

// Generous cap on the outlet's channel count. RTMP/FLV audio is essentially
// always mono or stereo in practice (AAC/MP3 in real-world encoders), but
// libavcodec can decode true multichannel AAC (5.1, 7.1, ...) or PCM, so we
// leave headroom for that rather than hardcoding stereo. The ring buffer
// below is sized against this constant (fixed stride) rather than the
// currently-configured channel count so that changing the @channels
// attribute never needs to resize/reallocate its storage while the decoder
// thread or audio thread might concurrently be touching it - only the
// (small, atomic) *active* channel count changes at runtime.
constexpr int kMaxAudioChannels = 16;
constexpr int kRingCapacityPow2 = 1 << 18; // frames of interleaved multichannel storage
constexpr int kRingMask         = kRingCapacityPow2 - 1;

// ---------------------------------------------------------------------------
// Lock-free single-producer/single-consumer ring buffer for interleaved
// multichannel audio (doubles), fixed at a kMaxAudioChannels stride.
// Producer = decoder thread. Consumer = Max audio thread. No locks, no
// allocation on the hot path.
// ---------------------------------------------------------------------------
class audio_ring {
public:
    audio_ring() { m_data.resize((size_t)kRingCapacityPow2 * kMaxAudioChannels, 0.0); }

    // Called from the decoder thread. Never blocks. Drops the oldest audio if
    // the consumer has fallen too far behind (bounded latency instead of an
    // ever-growing backlog). `channel_count` is however many channels the
    // resampler is currently producing (<= kMaxAudioChannels); `interleaved`
    // holds that many samples per frame.
    void write(const double* interleaved, size_t frame_count, int channel_count) {
        size_t w = m_write.load(std::memory_order_relaxed);
        size_t r = m_read.load(std::memory_order_acquire);
        size_t free_frames = kRingCapacityPow2 - (w - r);

        if (frame_count > free_frames) {
            size_t drop = frame_count - free_frames;
            m_read.store(r + drop, std::memory_order_release);
        }

        for (size_t i = 0; i < frame_count; ++i) {
            size_t idx  = (w + i) & kRingMask;
            double* dst = &m_data[(size_t)idx * kMaxAudioChannels];
            for (int c = 0; c < channel_count; ++c)
                dst[c] = interleaved[(size_t)i * channel_count + c];
        }
        m_write.store(w + frame_count, std::memory_order_release);
    }

    // Called from the audio thread only.
    size_t available_frames() const {
        size_t w = m_write.load(std::memory_order_acquire);
        size_t r = m_read.load(std::memory_order_relaxed);
        return w - r;
    }

    // Reads up to frame_count frames into `out_count` de-interleaved channel
    // arrays (out_count <= kMaxAudioChannels; entries beyond what the
    // producer is currently writing just read stale/zeroed data, which is
    // harmless - see the caller in operator()). Returns frames actually
    // read; caller is responsible for zero-filling the rest (silence) on
    // underrun.
    size_t read(double* const* out, int out_count, size_t frame_count) {
        size_t avail = available_frames();
        size_t n     = std::min(avail, frame_count);
        size_t r     = m_read.load(std::memory_order_relaxed);

        for (size_t i = 0; i < n; ++i) {
            size_t idx        = (r + i) & kRingMask;
            const double* src = &m_data[(size_t)idx * kMaxAudioChannels];
            for (int c = 0; c < out_count; ++c)
                out[c][i] = src[c];
        }
        m_read.store(r + n, std::memory_order_release);
        return n;
    }

    void reset() {
        m_read.store(0);
        m_write.store(0);
    }

private:
    std::vector<double> m_data;
    std::atomic<size_t> m_read { 0 };
    std::atomic<size_t> m_write { 0 };
};

// Deferred status/error messages need to cross from the decoder thread back
// to Max's main thread. We stash the latest messages here and nudge a
// c74::min::queue<> (qelem) to drain them on the main thread.
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

class jit_rtmp_receive;

namespace {
// Raw Max C-level trampoline for the "multichanneloutputs" class method (see
// jit_rtmp_receive::maxclass_setup below for why this can't be expressed
// through min-api's higher-level message<>/attribute<> DSL - Max calls this
// directly, by this exact name, on the class, while compiling the DSP chain,
// to ask how many channels the multichannel outlet at `index` should have).
long jit_rtmp_receive_multichanneloutputs(c74::max::t_object* x, long index);
} // namespace

class jit_rtmp_receive : public object<jit_rtmp_receive>, public vector_operator<> {
public:
    MIN_DESCRIPTION { "Run an RTMP server and receive a published stream as a Jitter matrix + MSP audio." };
    MIN_TAGS { "audio, video, streaming, rtmp" };
    MIN_AUTHOR { "Cameron Johnston" };
    MIN_RELATED { "jit.rtmp.send~, jit.grab, jit.qt.record" };

    inlet<>  in_messages { this, "(anything) start / stop / url / etc." };
    outlet<> out_matrix  { this, "(jit_matrix) decoded video frame" };
    outlet<> out_audio   { this, "(multichannelsignal) decoded audio", "multichannelsignal" };
    outlet<> out_status  { this, "(anything) status / error messages" };

    jit_rtmp_receive(const atoms& args = {}) {
        if (!args.empty())
            url = args[0];
        m_status_queue_ptr = std::make_unique<queue<>>(this, MIN_FUNCTION {
            drain_status();
            return atoms {};
        });
        // Nudged once per decoded video frame from the decoder thread; coalesces
        // automatically if frames arrive faster than the main thread services it.
        m_video_queue_ptr = std::make_unique<queue<>>(this, MIN_FUNCTION {
            emit_video_frame();
            return atoms {};
        });
    }

    ~jit_rtmp_receive() {
        stop_server();
    }

    // -----------------------------------------------------------------
    // Attributes
    // -----------------------------------------------------------------
    attribute<symbol> url { this, "url", "",
        description {
            "RTMP URL to listen on for an incoming publish, e.g. rtmp://0.0.0.0:1935/live/STREAM-KEY "
            "(or rtmp://localhost/live/STREAM-KEY - port defaults to 1935 if omitted). This object *is* "
            "the server: point an encoder at this URL and it will connect here directly. Host/port set "
            "the local listen address; the app/streamkey portion of the path is not validated by the "
            "built-in listener - any client that connects to this host/port and publishes is accepted."
        }
    };

    attribute<int> out_width { this, "width", 0,
        description { "Force output matrix width. 0 = use the incoming stream's native width." }
    };

    attribute<int> out_height { this, "height", 0,
        description { "Force output matrix height. 0 = use the incoming stream's native height." }
    };

    attribute<int> listen_timeout { this, "timeout", -1,
        description { "Seconds to wait for an incoming connection before giving up. -1 = wait indefinitely." }
    };

    attribute<int, threadsafe::no, limit::clamp> channels { this, "channels", 2,
        range { 1, kMaxAudioChannels },
        description {
            "Number of channels on the multichannel audio outlet. The incoming audio is up/down-mixed "
            "to this many channels with libswresample's default channel layout for the count (1 = mono, "
            "2 = stereo, 6 = 5.1, etc.), regardless of how many channels the publisher actually sends - "
            "so the common case (an ordinary stereo or mono publisher) needs no configuration at all. "
            "Like any other MC channel count, this is fixed for the life of the current DSP chain: "
            "changing it takes effect the next time DSP is turned on/recompiled, not immediately."
        }
    };

    // -----------------------------------------------------------------
    // Raw Max class method: called once when the class is created (not per
    // instance) - this is min-api's sanctioned escape hatch for class-level
    // Max C API calls that have no higher-level min-api equivalent. Here we
    // register "multichanneloutputs", which Max calls on this class during
    // every DSP compile to ask how many channels out_audio should carry.
    // This is the exact mechanism the C SDK's own mc.pack~/mc.rotate~ use to
    // drive a multichannel outlet's width (see max-sdk/source/mc/mc.pack~).
    // -----------------------------------------------------------------
    message<> maxclass_setup { this, "maxclass_setup",
        MIN_FUNCTION {
            c74::max::t_class* c = args[0];
            c74::max::class_addmethod(c, (c74::max::method)jit_rtmp_receive_multichanneloutputs,
                                       "multichanneloutputs", c74::max::A_CANT, 0);
            return {};
        }
    };

    // Called by jit_rtmp_receive_multichanneloutputs() below.
    int output_channels() const { return channels; }

    // -----------------------------------------------------------------
    // Messages
    // -----------------------------------------------------------------
    message<> start { this, "start", "Start listening for an incoming RTMP publish at the configured url.",
        MIN_FUNCTION {
            start_server();
            return {};
        }
    };

    message<> listen { this, "listen", "Alias for 'start'.",
        MIN_FUNCTION {
            start_server();
            return {};
        }
    };

    message<> stop { this, "stop", "Stop the server (disconnects any current publisher).",
        MIN_FUNCTION {
            stop_server();
            return {};
        }
    };

    // -----------------------------------------------------------------
    // Audio: called on Max's audio thread. Must never block or allocate.
    // -----------------------------------------------------------------
    void operator()(audio_bundle input, audio_bundle output) {
        long frame_count = output.frame_count();
        int  chan_count  = (int)std::min<long>(output.channel_count(), kMaxAudioChannels);
        if (chan_count < 1 || frame_count <= 0)
            return;

        double* chans[kMaxAudioChannels];
        for (int c = 0; c < chan_count; ++c)
            chans[c] = output.samples(c);

        size_t got = 0;
        if (m_streaming.load(std::memory_order_relaxed))
            got = m_audio_ring.read(chans, chan_count, (size_t)frame_count);

        // Silence-fill anything the ring didn't have available (stream not
        // started yet, no audio track, or a momentary underrun).
        for (int c = 0; c < chan_count; ++c)
            for (size_t i = got; i < (size_t)frame_count; ++i)
                chans[c][i] = 0.0;
    }

private:
    // === Latest decoded video frame (decoder thread writes; main thread reads via qelem) ===
    std::mutex           m_video_mutex;
    std::vector<uint8_t> m_latest_video_data; // tightly packed ARGB, m_latest_video_w * m_latest_video_h * 4
    long                  m_latest_video_w = 0, m_latest_video_h = 0;

    // === Internal jit_matrix we own + register (main thread only - never touched from the decoder thread) ===
    c74::max::t_object* m_matrix   = nullptr;
    symbol               m_matrix_name { "" };
    long                 m_matrix_w = 0, m_matrix_h = 0;

    // === Audio ring buffer (lock-free) ===
    audio_ring m_audio_ring;
    int        m_target_samplerate = 48000; // captured from samplerate() at 'start' time
    int        m_active_channels   = 2;     // captured from the 'channels' attribute at 'start' time

    // === Decoder thread & FFmpeg state (owned exclusively by that thread while running) ===
    std::thread       m_server_thread;
    std::atomic<bool> m_streaming { false };
    std::atomic<bool> m_stop_requested { false };

    AVFormatContext* m_fmt_ctx       = nullptr;
    AVCodecContext*  m_video_dec_ctx = nullptr;
    AVCodecContext*  m_audio_dec_ctx = nullptr;

    SwsContext*   m_sws_ctx = nullptr;
    long          m_sws_src_w = 0, m_sws_src_h = 0, m_sws_dst_w = 0, m_sws_dst_h = 0;
    AVPixelFormat m_sws_src_fmt = AV_PIX_FMT_NONE;

    SwrContext*     m_swr_ctx = nullptr;
    int             m_swr_src_rate      = 0;
    AVSampleFormat  m_swr_src_samplefmt = AV_SAMPLE_FMT_NONE;
    AVChannelLayout m_swr_src_layout {};

    status_mailbox           m_mailbox;
    std::unique_ptr<queue<>> m_status_queue_ptr;
    std::unique_ptr<queue<>> m_video_queue_ptr;

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
    // Jitter matrix management - main thread only. Object creation/
    // registration/freeing touches Max's global object registry, which is
    // not safe to call from a background thread (mirrors jit.rtmp.send~'s
    // handling of its internal jit.gl.asyncread helper).
    // -----------------------------------------------------------------
    bool ensure_video_matrix(long w, long h) {
        if (w <= 0 || h <= 0)
            return false;

        if (!m_matrix) {
            c74::max::t_jit_matrix_info info;
            c74::max::jit_matrix_info_default(&info);
            info.type       = c74::max::_jit_sym_char;
            info.planecount = 4;
            info.dimcount   = 2;
            info.dim[0]     = w;
            info.dim[1]     = h;

            m_matrix = (c74::max::t_object*)c74::max::jit_object_new(c74::max::_jit_sym_jit_matrix, &info);
            if (!m_matrix) {
                post_status("error could not create internal jit_matrix");
                return false;
            }
            c74::max::t_symbol* name = c74::max::jit_symbol_unique();
            c74::max::jit_object_method(m_matrix, c74::max::_jit_sym_register, name);
            m_matrix_name = symbol(name);
            m_matrix_w    = w;
            m_matrix_h    = h;
            return true;
        }

        if (w != m_matrix_w || h != m_matrix_h) {
            c74::max::t_jit_matrix_info info;
            c74::max::jit_object_method(m_matrix, c74::max::_jit_sym_getinfo, &info);
            info.dim[0] = w;
            info.dim[1] = h;
            c74::max::jit_object_method(m_matrix, c74::max::_jit_sym_setinfo, &info);
            m_matrix_w = w;
            m_matrix_h = h;
        }
        return true;
    }

    void teardown_video_matrix() {
        if (m_matrix) {
            c74::max::jit_object_unregister(m_matrix);
            c74::max::jit_object_free(m_matrix);
            m_matrix = nullptr;
        }
        m_matrix_name = symbol("");
        m_matrix_w = m_matrix_h = 0;
    }

    // Called on the main thread by m_video_queue_ptr. Copies out the latest
    // converted frame, (re)sizes/creates our matrix as needed, writes the
    // pixel data in respecting the matrix's own row stride, and sends
    // "jit_matrix <name>" out the outlet - exactly what any other Jitter
    // video source object does.
    void emit_video_frame() {
        std::vector<uint8_t> local;
        long w = 0, h = 0;
        {
            std::lock_guard<std::mutex> lock(m_video_mutex);
            if (m_latest_video_data.empty())
                return;
            local.swap(m_latest_video_data);
            w = m_latest_video_w;
            h = m_latest_video_h;
        }

        if (!ensure_video_matrix(w, h))
            return;

        long savelock = (long)c74::max::jit_object_method(m_matrix, c74::max::_jit_sym_lock, 1);

        c74::max::t_jit_matrix_info info;
        char* bp = nullptr;
        c74::max::jit_object_method(m_matrix, c74::max::_jit_sym_getinfo, &info);
        c74::max::jit_object_method(m_matrix, c74::max::_jit_sym_getdata, &bp);

        if (bp) {
            size_t row_bytes = (size_t)w * 4;
            for (long y = 0; y < h; ++y)
                std::memcpy(bp + y * info.dimstride[1], local.data() + (size_t)y * row_bytes, row_bytes);
        }

        c74::max::jit_object_method(m_matrix, c74::max::_jit_sym_lock, (void*)savelock);

        out_matrix.send(symbol("jit_matrix"), m_matrix_name);
    }

    // -----------------------------------------------------------------
    // Video decode - decoder thread only.
    // -----------------------------------------------------------------
    void convert_and_publish_video_frame(AVFrame* frame) {
        long src_w = frame->width, src_h = frame->height;
        if (src_w <= 0 || src_h <= 0)
            return;
        AVPixelFormat src_fmt = (AVPixelFormat)frame->format;

        long dst_w = (out_width  > 0) ? (long)out_width  : src_w;
        long dst_h = (out_height > 0) ? (long)out_height : src_h;

        if (!m_sws_ctx || src_w != m_sws_src_w || src_h != m_sws_src_h || src_fmt != m_sws_src_fmt
            || dst_w != m_sws_dst_w || dst_h != m_sws_dst_h) {
            if (m_sws_ctx) {
                sws_freeContext(m_sws_ctx);
                m_sws_ctx = nullptr;
            }
            m_sws_ctx = sws_getContext(
                (int)src_w, (int)src_h, src_fmt,
                (int)dst_w, (int)dst_h, AV_PIX_FMT_ARGB,
                SWS_BILINEAR, nullptr, nullptr, nullptr
            );
            m_sws_src_w = src_w; m_sws_src_h = src_h; m_sws_src_fmt = src_fmt;
            m_sws_dst_w = dst_w; m_sws_dst_h = dst_h;
            if (!m_sws_ctx) {
                post_status("error could not create video scaler");
                return;
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_video_mutex);
            m_latest_video_data.resize((size_t)dst_w * (size_t)dst_h * 4);
            uint8_t* dst_data[4]     = { m_latest_video_data.data(), nullptr, nullptr, nullptr };
            int      dst_linesize[4] = { (int)dst_w * 4, 0, 0, 0 };
            sws_scale(m_sws_ctx, frame->data, frame->linesize, 0, (int)src_h, dst_data, dst_linesize);
            m_latest_video_w = dst_w;
            m_latest_video_h = dst_h;
        }

        if (m_video_queue_ptr)
            m_video_queue_ptr->set();
    }

    void decode_video_packet(AVPacket* pkt) {
        if (avcodec_send_packet(m_video_dec_ctx, pkt) < 0)
            return;
        AVFrame* frame = av_frame_alloc();
        while (avcodec_receive_frame(m_video_dec_ctx, frame) == 0)
            convert_and_publish_video_frame(frame);
        av_frame_free(&frame);
    }

    // -----------------------------------------------------------------
    // Audio decode - decoder thread only.
    // -----------------------------------------------------------------
    void resample_and_enqueue_audio(AVFrame* frame) {
        bool need_rebuild = !m_swr_ctx
            || frame->sample_rate != m_swr_src_rate
            || frame->format != m_swr_src_samplefmt
            || av_channel_layout_compare(&frame->ch_layout, &m_swr_src_layout) != 0;

        if (need_rebuild) {
            if (m_swr_ctx)
                swr_free(&m_swr_ctx);

            AVChannelLayout out_layout;
            av_channel_layout_default(&out_layout, m_active_channels);

            int ret = swr_alloc_set_opts2(
                &m_swr_ctx,
                &out_layout, AV_SAMPLE_FMT_DBL, m_target_samplerate,
                &frame->ch_layout, (AVSampleFormat)frame->format, frame->sample_rate,
                0, nullptr
            );
            av_channel_layout_uninit(&out_layout);

            if (ret < 0 || !m_swr_ctx || swr_init(m_swr_ctx) < 0) {
                post_status("error could not initialize audio resampler");
                return;
            }
            m_swr_src_rate      = frame->sample_rate;
            m_swr_src_samplefmt = (AVSampleFormat)frame->format;
            av_channel_layout_uninit(&m_swr_src_layout);
            av_channel_layout_copy(&m_swr_src_layout, &frame->ch_layout);
        }

        int out_max = (int)av_rescale_rnd(frame->nb_samples, m_target_samplerate, frame->sample_rate, AV_ROUND_UP) + 32;
        std::vector<double> interleaved((size_t)out_max * m_active_channels);
        uint8_t* out_ptr = (uint8_t*)interleaved.data();

        int converted = swr_convert(m_swr_ctx, &out_ptr, out_max, (const uint8_t**)frame->data, frame->nb_samples);
        if (converted > 0)
            m_audio_ring.write(interleaved.data(), (size_t)converted, m_active_channels);
    }

    void decode_audio_packet(AVPacket* pkt) {
        if (avcodec_send_packet(m_audio_dec_ctx, pkt) < 0)
            return;
        AVFrame* frame = av_frame_alloc();
        while (avcodec_receive_frame(m_audio_dec_ctx, frame) == 0)
            resample_and_enqueue_audio(frame);
        av_frame_free(&frame);
    }

    // -----------------------------------------------------------------
    // Per-connection decoder setup/teardown - decoder thread only.
    // -----------------------------------------------------------------
    bool open_video_decoder(int stream_idx) {
        AVStream*      st    = m_fmt_ctx->streams[stream_idx];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!codec) {
            post_status("error no decoder available for incoming video codec");
            return false;
        }
        m_video_dec_ctx = avcodec_alloc_context3(codec);
        if (!m_video_dec_ctx)
            return false;
        avcodec_parameters_to_context(m_video_dec_ctx, st->codecpar);
        m_video_dec_ctx->pkt_timebase = st->time_base;
        if (avcodec_open2(m_video_dec_ctx, codec, nullptr) < 0) {
            post_status("error could not open video decoder");
            avcodec_free_context(&m_video_dec_ctx);
            return false;
        }
        return true;
    }

    bool open_audio_decoder(int stream_idx) {
        AVStream*      st    = m_fmt_ctx->streams[stream_idx];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!codec) {
            post_status("error no decoder available for incoming audio codec");
            return false;
        }
        m_audio_dec_ctx = avcodec_alloc_context3(codec);
        if (!m_audio_dec_ctx)
            return false;
        avcodec_parameters_to_context(m_audio_dec_ctx, st->codecpar);
        m_audio_dec_ctx->pkt_timebase = st->time_base;
        if (avcodec_open2(m_audio_dec_ctx, codec, nullptr) < 0) {
            post_status("error could not open audio decoder");
            avcodec_free_context(&m_audio_dec_ctx);
            return false;
        }
        return true;
    }

    void close_decoders() {
        if (m_video_dec_ctx)
            avcodec_free_context(&m_video_dec_ctx);
        if (m_audio_dec_ctx)
            avcodec_free_context(&m_audio_dec_ctx);
        if (m_sws_ctx) {
            sws_freeContext(m_sws_ctx);
            m_sws_ctx = nullptr;
        }
        m_sws_src_w = m_sws_src_h = m_sws_dst_w = m_sws_dst_h = 0;
        m_sws_src_fmt = AV_PIX_FMT_NONE;
        if (m_swr_ctx)
            swr_free(&m_swr_ctx);
        av_channel_layout_uninit(&m_swr_src_layout);
        m_swr_src_rate      = 0;
        m_swr_src_samplefmt = AV_SAMPLE_FMT_NONE;
    }

    void close_input() {
        if (m_fmt_ctx)
            avformat_close_input(&m_fmt_ctx);
    }

    static int interrupt_cb(void* ctx) {
        auto* self = static_cast<jit_rtmp_receive*>(ctx);
        return self->m_stop_requested.load(std::memory_order_relaxed) ? 1 : 0;
    }

    // -----------------------------------------------------------------
    // Server lifecycle
    // -----------------------------------------------------------------
    void start_server() {
        if (m_streaming.load()) {
            post_status("error already listening/streaming - send 'stop' first");
            return;
        }
        std::string listen_url = url.get().c_str();
        if (listen_url.empty()) {
            post_status("error no url set - set the url attribute first, e.g. @url rtmp://0.0.0.0:1935/live/STREAMKEY");
            return;
        }

        m_stop_requested.store(false);
        m_target_samplerate = samplerate() > 0 ? (int)std::lround(samplerate()) : 48000;
        m_active_channels   = std::clamp((int)channels, 1, kMaxAudioChannels);
        m_audio_ring.reset();
        m_streaming.store(true);

        int timeout_s = listen_timeout;
        m_server_thread = std::thread([this, listen_url, timeout_s]() {
            server_thread_main(listen_url, timeout_s);
        });
    }

    void stop_server() {
        if (!m_streaming.load() && !m_server_thread.joinable())
            return;

        m_stop_requested.store(true);
        if (m_server_thread.joinable())
            m_server_thread.join();
        m_streaming.store(false);

        // Main-thread only from here down (Jitter object registry).
        teardown_video_matrix();

        std::lock_guard<std::mutex> lock(m_video_mutex);
        m_latest_video_data.clear();
        m_latest_video_w = m_latest_video_h = 0;
    }

    // -----------------------------------------------------------------
    // Decoder thread: listens for a publisher, streams until it disconnects
    // (or 'stop' is sent), then goes back to listening for the next one.
    // -----------------------------------------------------------------
    void server_thread_main(const std::string& listen_url, int timeout_s) {
        avformat_network_init();

        while (!m_stop_requested.load(std::memory_order_relaxed)) {
            m_fmt_ctx = avformat_alloc_context();
            if (!m_fmt_ctx) {
                post_status("error could not allocate format context");
                break;
            }
            m_fmt_ctx->interrupt_callback.callback = interrupt_cb;
            m_fmt_ctx->interrupt_callback.opaque   = this;

            AVDictionary* opts = nullptr;
            av_dict_set(&opts, "listen", "1", 0);
            if (timeout_s >= 0)
                av_dict_set(&opts, "timeout", std::to_string(timeout_s).c_str(), 0);

            post_status("status listening on " + listen_url);

            int ret = avformat_open_input(&m_fmt_ctx, listen_url.c_str(), nullptr, &opts);
            av_dict_free(&opts);

            if (ret < 0) {
                if (!m_stop_requested.load(std::memory_order_relaxed)) {
                    char errbuf[AV_ERROR_MAX_STRING_SIZE] = { 0 };
                    av_strerror(ret, errbuf, sizeof(errbuf));
                    post_status(std::string("error could not listen on ") + listen_url + ": " + errbuf);
                }
                if (m_fmt_ctx)
                    avformat_free_context(m_fmt_ctx);
                m_fmt_ctx = nullptr;
                break; // a bind/url failure won't fix itself by retrying
            }

            post_status("status client connected");

            if (avformat_find_stream_info(m_fmt_ctx, nullptr) < 0) {
                post_status("error could not read stream info from incoming publish");
                close_input();
                continue; // go back and accept another client
            }

            int video_idx = av_find_best_stream(m_fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            int audio_idx = av_find_best_stream(m_fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

            if (video_idx < 0 && audio_idx < 0) {
                post_status("error incoming publish has no audio or video stream");
                close_input();
                continue;
            }

            bool have_video = video_idx >= 0 && open_video_decoder(video_idx);
            bool have_audio = audio_idx >= 0 && open_audio_decoder(audio_idx);

            if (video_idx >= 0 && !have_video)
                post_status("error could not open video decoder - continuing without video");
            if (audio_idx >= 0 && !have_audio)
                post_status("error could not open audio decoder - continuing without audio");

            if (!have_video && !have_audio) {
                post_status("error no usable audio or video stream - waiting for next publisher");
                close_decoders();
                close_input();
                continue;
            }

            post_status("status live");
            m_audio_ring.reset();

            AVPacket* pkt = av_packet_alloc();
            while (!m_stop_requested.load(std::memory_order_relaxed)) {
                int r = av_read_frame(m_fmt_ctx, pkt);
                if (r < 0) {
                    av_packet_unref(pkt);
                    break; // EOF / disconnect / interrupted -> accept the next publisher
                }
                if (have_video && pkt->stream_index == video_idx)
                    decode_video_packet(pkt);
                else if (have_audio && pkt->stream_index == audio_idx)
                    decode_audio_packet(pkt);
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);

            post_status("status client disconnected");
            close_decoders();
            close_input();
            // loop back and listen again, unless stop was requested
        }

        m_streaming.store(false);
        post_status("status stopped");
    }
};

namespace {
long jit_rtmp_receive_multichanneloutputs(c74::max::t_object* x, long /*index*/) {
    // out_audio is our only multichannel outlet, so `index` (which outlet is
    // being asked about) is irrelevant here - always report the same width.
    auto* self = c74::min::wrapper_find_self<jit_rtmp_receive>(x);
    return self->m_min_object.output_channels();
}
} // namespace

MIN_EXTERNAL(jit_rtmp_receive);
