# rtmp.stream~

A [Max](https://cycling74.com/products/max) external that streams live MSP audio and Jitter video straight out of a patch to an RTMP server — YouTube, Twitch, a self-hosted [nginx-rtmp](https://github.com/arut/nginx-rtmp-module)/[MediaMTX](https://github.com/bluenviron/mediamtx) server, or anything else that speaks RTMP — as a standard H.264/AAC/FLV stream. No intermediate screen-capture app, no virtual audio cable — Max talks to the encoder directly.

Built with [Min-API](https://github.com/Cycling74/min-api) (C++) and [FFmpeg](https://ffmpeg.org)'s `libavformat` / `libavcodec` / `libswscale` / `libswresample`.

## Features

- **Audio**: an MC (multichannel) inlet accepts any number of channels through one cable and downmixes to stereo AAC — route however many stems/mics/buses you want without managing individual mono cords.
- **Video**: two independent input paths, usable interchangeably in the same session —
  - **`jit_matrix`** — connect any classic Jitter object (`jit.grab`, `jit.movie`, `jit.matrix`, etc.) directly to the inlet.
  - **GL textures/contexts** — stream a `jit.gl.*` / `jit.world` render chain, either by naming a context with `@gl_context` (grabs the whole composited framebuffer automatically, no cable needed) or by patching a texture-outputting object in directly. Handled by an internally-managed `jit.gl.asyncread`, so you get correct async GPU→CPU readback without touching OpenGL yourself.
  - Switching from GL to a matrix source mid-stream is handled automatically; switching back just works.
- **Hardware or software H.264 encoding** — VideoToolbox (`@hwaccel 1`) or libx264, your choice.
- Everything (encoding, muxing, network I/O) runs on a dedicated background thread, so nothing here ever blocks Max's audio or scheduler threads.

## Requirements

- macOS (Apple Silicon or Intel), Xcode Command Line Tools, [CMake](https://cmake.org)
- Max 8+ with the [Max SDK](https://github.com/Cycling74/max-sdk) conventions (this repo vendors its own copy of [min-api](https://github.com/Cycling74/min-api), no separate install needed)
- [FFmpeg](https://ffmpeg.org), e.g. via Homebrew:
  ```bash
  brew install ffmpeg
  ```

## Building

```bash
git clone <this-repo-url>
cd RTMP-Max-Output
cmake -S . -B build
cmake --build build
```

(`min-api`, including its bundled Max SDK, is vendored directly in this repo — no submodule init needed.)

The built external lands at `build/externals/rtmp.stream~.mxo`. Copy or symlink it (and `patchers/`, if you want the example patch) into a package folder under your Max `Packages` directory, e.g. `~/Documents/Max 9/Packages/RTMP-Max-Output/externals/`.

> **Rebuilding?** Max only loads a given external's compiled code once per app launch — reopening a patch after a rebuild does *not* pick up new code. **Fully quit and relaunch Max** after every rebuild.

An example patch demonstrating both video paths and MC audio is at [`patchers/rtmp.stream~.test.maxpat`](patchers/rtmp.stream~.test.maxpat).

## Quick start

```
[adc~ 1 2] → [mc.pack~ 2] → (right inlet) [rtmp.stream~] → (left inlet, message) [print]
[jit.grab] ──────────────────────────────→ (left inlet)
```

```
url rtmp://a.rtmp.youtube.com/live2/YOUR-STREAM-KEY
start
```

Turn Max's DSP on before sending `start` — the object checks and will refuse (with a console error) otherwise.

## Attributes

| Attribute | Default | Description |
|---|---|---|
| `@url` | `""` | RTMP URL to publish to, e.g. `rtmp://a.rtmp.youtube.com/live2/KEY` |
| `@width` / `@height` | 1280 / 720 | Output video resolution |
| `@fps` | 30 | Output video frame rate |
| `@video_kbps` | 4000 | Video encoder target bitrate (kbit/s) |
| `@audio_kbps` | 160 | Audio (AAC) encoder target bitrate (kbit/s) |
| `@samplerate` | 48000 | Audio encode sample rate in Hz; input is resampled to this |
| `@hwaccel` | 0 | Use VideoToolbox hardware H.264 encoding instead of libx264 |
| `@keyframe_interval` | 60 | Keyframe (GOP) interval in frames |
| `@gl_context` | `""` | Name of a `jit.gl` context (e.g. a `jit.world`'s `@name`) to stream directly — see How it works below |

## Messages

| Message | Description |
|---|---|
| `start` | Connect and begin streaming. Requires `@url` to be set and Max's DSP to be running. |
| `stop` | Stop streaming and disconnect. |
| `jit_matrix <name>` | Video frame from a classic Jitter object — normally sent automatically by whatever's patched into the left inlet. Char ARGB (4-plane) or RGB (3-plane) matrices only; convert upstream (e.g. `jit.matrix 4 char WxH`) if your source is float. |
| `jit_gl_texture <name>` | Video frame from a `jit.gl.*` texture — sent automatically by a texture-outputting object patched into the left inlet. Not needed if you're using `@gl_context`. |
| `gl_disconnect` | Manually stop the internal GL readback helper. Not usually needed — switching to a `jit_matrix` source does this automatically — but available if you want to stop GL reading without switching to a matrix. |

The outlet reports status and errors as messages: `status connecting`, `status live`, `status stopped`, and `error <message>`.

## How it works

- **Audio**: the inlet is MC-capable (`c74::min::mc_operator<>`) — `audio_bundle::channel_count()` reflects however many channels are actually connected, dynamically. Since RTMP/FLV audio is essentially always expected to be stereo by real-world consumers, the object downmixes on the audio thread before anything else happens: 1 channel duplicates to L/R; 2+ channels alternate (even index → L, odd index → R), each side averaged over however many landed on it to avoid gain buildup. The stereo pair is copied into a lock-free single-producer/single-consumer ring buffer (no locks or allocation on the realtime thread).
- **Matrix video**: the `jit_matrix` handler locks the matrix, converts it with `libswscale` straight into a YUV420P frame at the configured output resolution, and swaps it into a "latest frame" slot under a small mutex.
- **GL video**: rather than reading OpenGL textures directly (undocumented Jitter GL internals, GL context/thread affinity to manage), the object instantiates a hidden internal `jit.gl.asyncread` — the same object/behavior you'd patch in by hand — and polls its internally-registered readback matrix (`out_name`) on a timer, feeding it through the exact same conversion path as `jit_matrix`. `jit.gl.asyncread` is driven with an explicit `bang` per poll rather than its own `@automatic`, since GL and matrix input need to be mutually exclusive without racing each other.
- **Encoder thread**: a dedicated background thread owns the FFmpeg output context exclusively (its muxer isn't safe to call from multiple threads at once). Each loop iteration drains available audio and, on a steady per-frame timer, encodes the latest video frame — re-emitting the last one if nothing new arrived, to keep output at a constant frame rate the way a real streaming encoder should. All network I/O and encoding happens here; nothing blocks Max's own threads, and an interrupt callback lets `stop` abort a hung connection promptly.
- H.264 is encoded via VideoToolbox (`@hwaccel 1`) or libx264; audio always via FFmpeg's built-in AAC encoder.

## Known limitations

- **Distribution**: the built `.mxo` links against your local FFmpeg install by absolute path. Fine on your own machine; sharing the patch with someone else requires them to install FFmpeg too, or the dylibs need to be bundled into the `.mxo` and re-pathed (e.g. with `dylibbundler`) first.
- **Stereo output only**: input audio accepts any channel count via MC, but it's always downmixed to stereo before encoding.
- **Video format**: only char ARGB/RGB `jit_matrix` data is handled (this includes what the internal GL helper produces — it outputs a regular char matrix too).
- One `jit.gl.*` render source per stream at a time (see "How it works" above) — no compositing of multiple GL contexts.

## License

[MIT](LICENSE). Note that Homebrew's default FFmpeg build enables `libx264`, which requires `--enable-gpl`; if you distribute *compiled binaries* of this external linked against a GPL-enabled FFmpeg build, that carries GPL obligations independent of this repository's own (MIT) license.
