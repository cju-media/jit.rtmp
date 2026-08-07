# rtmp.stream~

A Max external that encodes live MSP audio + Jitter video and pushes it to an
RTMP server (YouTube, Twitch, a self-hosted `nginx-rtmp`/MediaMTX server, etc.)
directly from a Max patch, as an H.264/AAC/FLV stream.

Built with [Min-API](https://github.com/Cycling74/min-api) (C++) and
[FFmpeg](https://ffmpeg.org)'s `libavformat`/`libavcodec`/`libswscale`/`libswresample`.

## Status

Live-tested against YouTube: connects and goes live (`status connecting` →
`status live`), with `jit.grab` feeding video via `jit_matrix`. Found and
fixed a real bug along the way where YouTube received data and reported the
stream as live but never actually showed video - a VideoToolbox
extradata-timing issue (see `prime_video_encoder_extradata()` in the source
and the git log for the full explanation). **That fix has not yet been
re-verified live** - that's the next thing to confirm. Audio has not been
separately confirmed working yet either. The `@gl_context`/`jit_gl_texture`
GL-texture path is implemented the same way as the matrix path but hasn't
been live-tested at all yet.

## How it works

- **Inlets**: left inlet takes the left audio channel *and* doubles as the
  message inlet (`jit_matrix`, `start`, `stop`, `url ...`, etc.); right inlet
  is the right audio channel.
- **Outlet**: status/error messages (`status connecting`, `status live`,
  `status stopped`, `error ...`).
- **Audio path**: `operator()` on Max's audio thread copies each vector into a
  lock-free SPSC ring buffer (no locks/allocation on the realtime thread). The
  encoder thread drains it, resamples to the target rate with `libswresample`,
  and encodes fixed-size AAC frames via an `AVAudioFifo`.
- **Video path**: the `jit_matrix` message handler (fires on whatever thread
  Jitter delivers on — not the audio thread) locks the matrix, reads it with
  `libswscale` straight into a YUV420P frame at the configured output
  resolution, and swaps it into a "latest frame" slot under a small mutex.
  Only char ARGB (4-plane) or RGB (3-plane) matrices are supported — convert
  upstream with e.g. `jit.matrix 4 char WxH` if your source is float.
- **GL texture input**: rather than reading OpenGL textures ourselves (would
  mean reverse-engineering undocumented Jitter GL internals and juggling GL
  context/thread affinity), the object instantiates a hidden internal
  `jit.gl.asyncread` — the same object/behavior you'd patch in by hand — and
  wires its matrix output straight into the `jit_matrix` handler above via a
  hidden patch cord. Two ways to feed it:
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

- `start` — connect and begin streaming (requires `@url` to be set first)
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
- **GL path is the least verified part of this object.** It's built on real,
  confirmed SDK primitives (`newobject_sprintf`, `jbox_set_hidden`,
  `object_attr_setsym`, and the `"connect"`/`"hiddenconnect"` patcher messages
  - the latter is a real symbol in the SDK's `jpatcher_syms.c` but isn't in
  the public header docs, so the exact calling convention is inferred from
  precedent rather than a documented prototype) and `jit.gl.asyncread`'s own
  documented attributes (`@texture`, `@matrixoutput`, `@drawto`,
  `@automatic`), but none of it has been exercised against an actual
  rendering `jit.world`/`jit.gl.*` chain yet. If the hidden-connect call
  doesn't behave as expected, you'll see `error could not wire internal
  jit.gl.asyncread to this object` on the status outlet — report that back
  and it'll need a fix.
- **Not yet tested live** — see Status above.
