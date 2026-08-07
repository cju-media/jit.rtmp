# rtmp.stream~

A Max external that encodes live MSP audio + Jitter video and pushes it to an
RTMP server (YouTube, Twitch, a self-hosted `nginx-rtmp`/MediaMTX server, etc.)
directly from a Max patch, as an H.264/AAC/FLV stream.

Built with [Min-API](https://github.com/Cycling74/min-api) (C++) and
[FFmpeg](https://ffmpeg.org)'s `libavformat`/`libavcodec`/`libswscale`/`libswresample`.

## Status

**Confirmed working end-to-end on YouTube**: `jit.grab` video + `adc~` audio,
both playing back correctly. Getting here surfaced three real, distinct bugs
(see git log for full detail on each):

1. VideoToolbox doesn't populate H.264 extradata (SPS/PPS) until after it's
   encoded a frame, so the FLV header written at stream start had none -
   fixed by priming the encoder before `avformat_write_header`.
2. The forced-keyframe mechanism used the wrong field
   (`AV_FRAME_FLAG_KEY` isn't respected by libx264 as a force-keyframe
   request) - every transmitted packet was a non-keyframe, so nothing was
   ever decodable. Fixed via `pict_type = AV_PICTURE_TYPE_I`.
3. **DSP simply wasn't running** in testing - `operator()` never fires with
   audio off, so audio was silently never encoded, and that alone was enough
   to keep the whole stream from ever playing, not just leave it silent.
   `start` now refuses to run (with a real console error) if DSP is off -
   see the `start` message below.
4. The internal `jit.gl.asyncread` helper for the `@gl_context` GL path
   never actually delivered any output via the `"hiddenconnect"` wiring it
   originally relied on - fixed by polling its `out_name` matrix directly
   instead. See "GL texture input" below.

The `out_name`-polling fix for `@gl_context` is confirmed working live. The
`jit_gl_texture` patch-cable path (multi-pass/offscreen textures) shares the
same polling fix but hasn't been live-tested on its own yet.

**MC audio input is new and not yet live-tested** - the object now inherits
`mc_operator<>` instead of `vector_operator<>` and the right inlet accepts
any channel count, downmixed to stereo (see "How it works" below). Confirmed
compiling/linking and loading without crashing; needs a live audio test next.

## How it works

- **Inlets**: left inlet is messages only (`jit_matrix`, `jit_gl_texture`,
  `start`, `stop`, `url ...`, attributes, etc. — no audio). Right inlet is a
  single MC (multichannel) audio inlet, accepting any number of channels
  through one cable.
- **Outlet**: status/error messages (`status connecting`, `status live`,
  `status stopped`, `error ...`).
- **Audio path**: the object inherits Min-API's `mc_operator<>`, which makes
  the audio inlet MC-capable — `audio_bundle::channel_count()` reflects
  however many channels are actually connected, dynamically. Since RTMP/FLV
  audio is essentially always expected to be stereo AAC by real-world
  consumers (YouTube, Twitch, etc.), `operator()` downmixes on the audio
  thread before anything else happens: 1 channel duplicates to L/R; 2+
  channels alternate (even index → L, odd index → R), each side averaged
  over however many landed on it to avoid gain buildup. The resulting stereo
  pair is copied into a lock-free SPSC ring buffer (no locks/allocation on
  the realtime thread). The encoder thread drains it, resamples to the
  target rate with `libswresample`, and encodes fixed-size AAC frames via an
  `AVAudioFifo`.
- **Video path**: the `jit_matrix` message handler (fires on whatever thread
  Jitter delivers on — not the audio thread) locks the matrix, reads it with
  `libswscale` straight into a YUV420P frame at the configured output
  resolution, and swaps it into a "latest frame" slot under a small mutex.
  Only char ARGB (4-plane) or RGB (3-plane) matrices are supported — convert
  upstream with e.g. `jit.matrix 4 char WxH` if your source is float.
- **GL texture input**: rather than reading OpenGL textures ourselves (would
  mean reverse-engineering undocumented Jitter GL internals and juggling GL
  context/thread affinity), the object instantiates a hidden internal
  `jit.gl.asyncread` — the same object/behavior you'd patch in by hand. An
  earlier version tried to wire its outlet to our inlet with the internal
  `"hiddenconnect"` patcher message; confirmed live that this never actually
  delivered anything (zero messages ever arrived). Instead we poll its
  documented `out_name` attribute — the name of its own internally-registered
  readback matrix — on a timer, and read that matrix directly with the exact
  same code path used for `jit_matrix` messages. Two ways to feed it:
  - `@gl_context <name>` attribute — names a `jit.gl` context (e.g. a
    `jit.world`'s `@name`) to grab whole-framebuffer frames from continuously,
    every render pass. No patch cord needed for video at all; just set this
    and `start`.
  - `jit_gl_texture <name>` message — sent automatically by a `jit.gl.*`
    object patched into the left inlet when it outputs to a named texture
    (multi-pass/offscreen pipelines). The helper's `@texture` is retargeted
    whenever the name changes.
- **Encoder thread**: a single background `std::thread` owns the
  `AVFormatContext` exclusively (FFmpeg's muxer isn't safe to call from two
  threads at once). Each loop iteration drains available audio, and on a
  steady per-frame timer pulls the latest video frame (re-emitting the last
  frame if nothing new arrived, to keep the output at a constant frame rate
  the way a real streaming encoder should). Never blocks Max's own threads —
  network I/O and encoding happen entirely here, and an `AVIOInterruptCB` lets
  `stop` abort a hung connection promptly.
- Video defaults to `h264_videotoolbox` (hardware) when `@hwaccel 1` (the
  default); falls back to `libx264` / whatever H.264 encoder FFmpeg finds.
  Audio is always the built-in FFmpeg AAC encoder.

## Attributes

| Attribute | Default | Description |
|---|---|---|
| `@url` | `""` | RTMP URL, e.g. `rtmp://a.rtmp.youtube.com/live2/KEY` |
| `@width` / `@height` | 1280 / 720 | Output video resolution |
| `@fps` | 30 | Output frame rate |
| `@video_kbps` | 4000 | Video bitrate (kbit/s) |
| `@audio_kbps` | 160 | Audio (AAC) bitrate (kbit/s) |
| `@samplerate` | 48000 | Audio encode sample rate (input is resampled to this) |
| `@hwaccel` | 1 | Use VideoToolbox hardware H.264 encoding |
| `@keyframe_interval` | 60 | GOP size in frames |
| `@gl_context` | `""` | Name of a `jit.gl` context to stream directly (see GL texture input above) |

## Messages

- `start` — connect and begin streaming (requires `@url` to be set first,
  and Max's DSP/audio engine to be running - `start` checks `sys_getdspstate()`
  and refuses with a console error, `rtmp.stream~: DSP is off - ...`, if not;
  visible even with nothing patched to the status outlet)
- `stop` — stop streaming and disconnect
- `jit_matrix <name>` — normally sent automatically by whatever Jitter object
  you patch into the left inlet
- `jit_gl_texture <name>` — normally sent automatically by a `jit.gl.*`
  object patched into the left inlet; not needed if you're using `@gl_context`

## Building

Requires: Xcode command line tools, CMake, and FFmpeg (`brew install ffmpeg`).

```bash
cd /Users/c/Documents/Programming/RTMP-Max-Output
cmake -S . -B build
cmake --build build
```

The built external lands at `build/externals/rtmp.stream~.mxo` and is
symlinked into `~/Documents/Max 9/Packages/RTMP-Max-Output/externals`.

**Important**: Max only loads a given external's compiled code once per app
launch. Reopening the patch (or even closing/reopening the object box) after
a rebuild does *not* pick up the new code — it keeps running whatever was
loaded first. **Fully quit and relaunch Max** after every rebuild, or you'll
see stale-code symptoms like `"<attr>" is not a valid attribute argument`
for attributes that very much do exist in the source you just built.

## Known limitations / follow-ups

- **Distribution**: the built `.mxo` links against Homebrew's FFmpeg dylibs by
  absolute path (`/opt/homebrew/opt/ffmpeg/...`). Fine for your own machine;
  anyone else opening this patch needs `brew install ffmpeg` too, or the
  dylibs need to be bundled into the `.mxo` and re-pathed with
  `install_name_tool`/`dylibbundler` before sharing.
- **Stereo only**: audio is hardcoded to 2 channels in, matching typical
  stream targets. Extending to N channels is straightforward if needed later.
- **Video format**: only char ARGB/RGB `jit_matrix` input is handled today.
- **GL path**: the original design relied on an internal `"hiddenconnect"`
  patcher message to wire the helper's outlet to our inlet - confirmed live
  against a `jit.world` via `@gl_context` that this never worked (zero
  messages ever arrived, every video packet was just our unchanging black
  seed frame). Replaced with polling the helper's own `out_name` matrix
  directly on a timer instead - **that fix has not been re-verified live
  yet**, for either `@gl_context` or the patch-cable `jit_gl_texture` path.
  If the poll ever comes up empty, you'll see `error internal
  jit.gl.asyncread has no out_name - matrixoutput may not have taken effect`
  on the status outlet.
- Audio and matrix-driven video are confirmed working end to end on YouTube.
