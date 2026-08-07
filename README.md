# rtmp.stream~

A Max external that encodes live MSP audio + Jitter video and pushes it to an
RTMP server (YouTube, Twitch, a self-hosted `nginx-rtmp`/MediaMTX server, etc.)
directly from a Max patch, as an H.264/AAC/FLV stream.

Built with [Min-API](https://github.com/Cycling74/min-api) (C++) and
[FFmpeg](https://ffmpeg.org)'s `libavformat`/`libavcodec`/`libswscale`/`libswresample`.

## Status

Builds and links cleanly on this machine (Apple Silicon, FFmpeg 8.1 via
Homebrew, Max 9). **It has not yet been tested against a live audio/video
signal chain and a real RTMP endpoint inside Max** — do that next (see
`patchers/rtmp.stream~.test.maxpat` in the installed package). Treat the
first live test as the real verification step, not this build log.

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

## Messages

- `start` — connect and begin streaming (requires `@url` to be set first)
- `stop` — stop streaming and disconnect
- `jit_matrix <name>` — normally sent automatically by whatever Jitter object
  you patch into the left inlet

## Building

Requires: Xcode command line tools, CMake, and FFmpeg (`brew install ffmpeg`).

```bash
cd /Users/c/Documents/Programming/RTMP-Max-Output
cmake -S . -B build
cmake --build build
```

The built external lands at `build/externals/rtmp.stream~.mxo` and is
symlinked into `~/Documents/Max 9/Packages/RTMP-Max-Output/externals`, so
rebuilding here picks up automatically in Max (quit/reopen the patch, or the
whole app, to reload the external after a rebuild).

## Known limitations / follow-ups

- **Distribution**: the built `.mxo` links against Homebrew's FFmpeg dylibs by
  absolute path (`/opt/homebrew/opt/ffmpeg/...`). Fine for your own machine;
  anyone else opening this patch needs `brew install ffmpeg` too, or the
  dylibs need to be bundled into the `.mxo` and re-pathed with
  `install_name_tool`/`dylibbundler` before sharing.
- **Stereo only**: audio is hardcoded to 2 channels in, matching typical
  stream targets. Extending to N channels is straightforward if needed later.
- **Video format**: only char ARGB/RGB `jit_matrix` input is handled today.
- **Not yet tested live** — see Status above.
