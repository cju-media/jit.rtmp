# jit.rtmp

[Max](https://cycling74.com/products/max) externals for streaming MSP audio and Jitter video to/from RTMP — YouTube, Twitch, a self-hosted [nginx-rtmp](https://github.com/arut/nginx-rtmp-module)/[MediaMTX](https://github.com/bluenviron/mediamtx) server, OBS, or anything else that speaks RTMP — as standard H.264/AAC/FLV. No intermediate screen-capture app, no virtual audio cable — Max talks to the network directly.

- **[`jit.rtmp.send~`](#jitrtmpsend)** — publish audio/video out of a patch to an RTMP URL.
- **[`jit.rtmp.receive~`](#jitrtmpreceive)** — run an RTMP *ingest* server *in* a patch and receive a single incoming publish as a Jitter matrix + MSP audio.
- **[`jit.rtmp.server`](#jitrtmpserver)** — run a full RTMP media server *in* a patch (accepts one publish, serves it out to any number of players — VLC, other Max patches, Raspberry Pis, etc.) with no separate install.

Built with [Min-API](https://github.com/Cycling74/min-api) (C++) and [FFmpeg](https://ffmpeg.org)'s `libavformat` / `libavcodec` / `libswscale` / `libswresample`.

## Requirements

- macOS (Apple Silicon or Intel), Xcode Command Line Tools, [CMake](https://cmake.org)
- Max 8+ with the [Max SDK](https://github.com/Cycling74/max-sdk) conventions (this repo vendors its own copy of [min-api](https://github.com/Cycling74/min-api), no separate install needed)
- [FFmpeg](https://ffmpeg.org), e.g. via Homebrew:
  ```bash
  brew install ffmpeg
  ```

## Building

```bash
git clone https://github.com/cju-media/jit.rtmp.git
cd jit.rtmp
cmake -S . -B build
cmake --build build
```

(`min-api`, including its bundled Max SDK, is vendored directly in this repo — no submodule init needed.)

This assembles a complete Max package at `build/jit.rtmp/` — an `externals/`
folder with all three built objects (`jit.rtmp.send~.mxo`,
`jit.rtmp.receive~.mxo`, `jit.rtmp.server.mxo`), a live symlink to the
authored `patchers/`, and a generated `package-info.json`. That directory *is*
the package, so installing it is one symlink:

```bash
ln -s "$(pwd)/build/jit.rtmp" ~/Documents/"Max 9"/Packages/jit.rtmp
```

Nothing needs re-copying after a rebuild, and if a `docs/`, `help/`, `extras/`,
`media/` or `init/` folder is ever added to the repo, the next configure picks
it up automatically.

`build` is a symlink to `build.nosync`, which is where the output actually
lands — this repo lives under `~/Documents`, and iCloud's file provider races
with CMake's rapid directory create/rename/delete, leaving empty duplicate
directories behind (`CMakeFiles 2`, `jit 2.rtmp`, ...) unless the real build
directory is named so iCloud skips it. **To wipe the build:**
`rm -rf build.nosync && mkdir build.nosync`, not just `rm -rf build.nosync`
alone — that leaves `build` a dangling symlink, and CMake's directory
bootstrap does not follow a dangling symlink to create what it points at, so
the next `cmake -B build` fails with an unrelated-looking `pkgRedirects`
error.

> **Rebuilding?** Max only loads a given external's compiled code once per app launch — reopening a patch after a rebuild does *not* pick up new code. **Fully quit and relaunch Max** after every rebuild. If you're adding a *new* object for the first time (not just rebuilding one you already had), also run **Options → Rebuild the Max File Search Database** after relaunching — Max caches the object/autocomplete database and won't otherwise notice the new object.

> **First build needs internet access.** `jit.rtmp.server` bundles a real [MediaMTX](https://github.com/bluenviron/mediamtx) binary — CMake downloads and checksum-verifies the right one for your Mac the first time you configure the project, then caches it in `build/` (no re-download on later builds). If that download fails (offline, firewall, etc.), the build still succeeds — `jit.rtmp.server` just comes up without a bundled binary until you either fix connectivity and reconfigure, or point its `@mediamtx_path` attribute at your own install.

### Universal (Apple Silicon + Intel) builds

The instructions above build only for the Mac you run them on — Homebrew's
FFmpeg ships dylibs for whichever single architecture that Homebrew install
itself runs as, never both, so a plain `cmake -B build` on an Apple Silicon
Mac produces an arm64-only package that won't load on an Intel Mac (and
vice versa).

To build a single package that runs on both, you need a *second* Homebrew
install for the other architecture, running under Rosetta if you're doing
this on Apple Silicon:

```bash
softwareupdate --install-rosetta --agree-to-license
arch -x86_64 /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
arch -x86_64 /usr/local/bin/brew install ffmpeg pkg-config
```

(If you're building on an Intel Mac instead, it's the same idea in reverse —
you'd need an arm64 Homebrew, which needs Apple Silicon to run at all; in
practice, building the universal package on Apple Silicon is the only side
that actually works standalone.)

With both Homebrews in place, one script does the rest — two single-arch
configure+build passes (one against each Homebrew) and a `lipo -create` merge
of the results, including the bundled `mediamtx` binary:

```bash
./scripts/build-universal.sh
```

The merged, universal package lands at `build/jit.rtmp` — install it the same
way as any other build (see above). Homebrew's dylibs are referenced by
absolute path per-architecture (`/opt/homebrew/...` in the arm64 slice,
`/usr/local/...` in the x86_64 slice), so this only actually runs on another
machine if *that* machine also has a matching-architecture FFmpeg installed
at the matching prefix — see "Known limitations" below.

If your Homebrews live somewhere other than the default `/opt/homebrew`
(arm64) / `/usr/local` (x86_64) prefixes, point the script at them with
`JIT_RTMP_ARM64_HOMEBREW` / `JIT_RTMP_X86_64_HOMEBREW` environment variables.

**Watch out:** a plain `cmake --build build` afterward (e.g. while iterating
on source) writes to that same `build/jit.rtmp` and only ever produces a
single-arch binary, so it will silently turn a universal package back into a
single-arch one. The build prints a warning if this is about to happen
(`warning: ... is currently universal (...) but this build only targets
(...)`) - re-run `./scripts/build-universal.sh` afterward if you see it and
want the universal package back.

`build-universal.sh` verifies its own output before reporting success - it
lipo-inspects every binary that matters (each external, plus the bundled
`mediamtx`) and fails loudly if any of them didn't actually end up universal,
rather than silently shipping a partial merge. You can also run that check
by hand at any time, e.g. right before handing the package to someone else,
or just to confirm what's currently installed:

```bash
./scripts/check-universal.sh
```

(defaults to checking `build/jit.rtmp`; pass a different path to check
elsewhere.)

#### Running a universal build on another machine

A universal `.mxo` still links against FFmpeg by absolute, per-architecture
path (`/opt/homebrew/...` for the arm64 slice, `/usr/local/...` for the
x86_64 slice) - lipo-merging the binary doesn't change that. So whatever
Mac actually *runs* the build - not just the one that built it - needs its
own matching Homebrew FFmpeg install, or `jit.rtmp.send~`/`jit.rtmp.receive~`
will fail to load with something like:

```
jit.rtmp.send~: unable to load object bundle executable: The bundle
"jit.rtmp.send~.mxo" couldn't be loaded.
```

On an **Intel Mac**, that means a native (non-Rosetta) Homebrew at its
standard `/usr/local` prefix:

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```
```bash
brew install ffmpeg
```

On an **Apple Silicon Mac**, the same idea at `/opt/homebrew`:

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```
```bash
brew install ffmpeg
```

(This is a separate step from the build-machine setup above - that one
installs a *second*, Rosetta-based Homebrew alongside your normal one so a
single Mac can build both architectures. A machine that's only ever going
to *run* the result just needs its own single, native Homebrew FFmpeg.)

If a Standalone application built from this package still fails to load the
externals after that, double check the Standalone itself actually contains
a universal binary and wasn't built from a stale, already-loaded copy - see
the `.mxo` rebuild note above about fully quitting and relaunching Max
before rebuilding.

---

## jit.rtmp.send~

Streams live MSP audio and Jitter video straight out of a patch to an RTMP server as a standard H.264/AAC/FLV stream.

### Features

- **Audio**: an MC (multichannel) inlet accepts any number of channels through one cable and downmixes to stereo AAC — route however many stems/mics/buses you want without managing individual mono cords.
- **Video**: two independent input paths, usable interchangeably in the same session —
  - **`jit_matrix`** — connect any classic Jitter object (`jit.grab`, `jit.movie`, `jit.matrix`, etc.) directly to the inlet.
  - **GL textures/contexts** — stream a `jit.gl.*` / `jit.world` render chain, either by naming a context with `@gl_context` (grabs the whole composited framebuffer automatically, no cable needed) or by patching a texture-outputting object in directly. Handled by an internally-managed `jit.gl.asyncread`, so you get correct async GPU→CPU readback without touching OpenGL yourself.
  - Switching from GL to a matrix source mid-stream is handled automatically; switching back just works.
- **Hardware or software H.264 encoding** — VideoToolbox (`@hwaccel 1`) or libx264, your choice.
- Everything (encoding, muxing, network I/O) runs on a dedicated background thread, so nothing here ever blocks Max's audio or scheduler threads.

An example patch demonstrating both video paths and MC audio is at [`patchers/jit.rtmp.send~.test.maxpat`](patchers/jit.rtmp.send~.test.maxpat).

### Quick start

```
[adc~ 1 2] → [mc.pack~ 2] → (right inlet) [jit.rtmp.send~] → (left inlet, message) [print]
[jit.grab] ──────────────────────────────→ (left inlet)
```

```
url rtmp://a.rtmp.youtube.com/live2/YOUR-STREAM-KEY
start
```

Turn Max's DSP on before sending `start` — the object checks and will refuse (with a console error) otherwise.

### Attributes

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

### Messages

| Message | Description |
|---|---|
| `start` | Connect and begin streaming. Requires `@url` to be set and Max's DSP to be running. |
| `stop` | Stop streaming and disconnect. |
| `jit_matrix <name>` | Video frame from a classic Jitter object — normally sent automatically by whatever's patched into the left inlet. Char ARGB (4-plane) or RGB (3-plane) matrices only; convert upstream (e.g. `jit.matrix 4 char WxH`) if your source is float. |
| `jit_gl_texture <name>` | Video frame from a `jit.gl.*` texture — sent automatically by a texture-outputting object patched into the left inlet. Not needed if you're using `@gl_context`. |
| `gl_disconnect` | Manually stop the internal GL readback helper. Not usually needed — switching to a `jit_matrix` source does this automatically — but available if you want to stop GL reading without switching to a matrix. |

The outlet reports status and errors as messages: `status connecting`, `status live`, `status stopped`, and `error <message>`.

### How it works

- **Audio**: the inlet is MC-capable (`c74::min::mc_operator<>`) — `audio_bundle::channel_count()` reflects however many channels are actually connected, dynamically. Since RTMP/FLV audio is essentially always expected to be stereo by real-world consumers, the object downmixes on the audio thread before anything else happens: 1 channel duplicates to L/R; 2+ channels alternate (even index → L, odd index → R), each side averaged over however many landed on it to avoid gain buildup. The stereo pair is copied into a lock-free single-producer/single-consumer ring buffer (no locks or allocation on the realtime thread).
- **Matrix video**: the `jit_matrix` handler locks the matrix, converts it with `libswscale` straight into a YUV420P frame at the configured output resolution, and swaps it into a "latest frame" slot under a small mutex.
- **GL video**: rather than reading OpenGL textures directly (undocumented Jitter GL internals, GL context/thread affinity to manage), the object instantiates a hidden internal `jit.gl.asyncread` — the same object/behavior you'd patch in by hand — and polls its internally-registered readback matrix (`out_name`) on a timer, feeding it through the exact same conversion path as `jit_matrix`. `jit.gl.asyncread` is driven with an explicit `bang` per poll rather than its own `@automatic`, since GL and matrix input need to be mutually exclusive without racing each other.
- **Encoder thread**: a dedicated background thread owns the FFmpeg output context exclusively (its muxer isn't safe to call from multiple threads at once). Each loop iteration drains available audio and encodes the latest video frame whenever the audio clock says the next one is due — re-emitting the last one if nothing new arrived, to keep output at a constant frame rate the way a real streaming encoder should. All network I/O and encoding happens here; nothing blocks Max's own threads, and an interrupt callback lets `stop` abort a hung connection promptly.
- **A/V sync**: video's timestamps are derived from real elapsed *audio* time (samples actually consumed through the ring buffer, i.e. Max's real audio hardware clock), not an independent wall-clock timer. Two separately-ticking clocks - even a system clock and an audio device clock that are each individually accurate - are never at *exactly* the same rate, and that mismatch accumulates into real, growing drift on a long-running stream if each stream is timestamped against its own clock. Deriving video's cadence from audio's own elapsed time means there's only one clock in play, so the two can't drift apart over time by construction. (A wall-clock fallback is used only before any real audio has arrived yet, so video isn't stuck waiting on audio that may never come.)
- H.264 is encoded via VideoToolbox (`@hwaccel 1`) or libx264; audio always via FFmpeg's built-in AAC encoder.

### Known limitations

- **Distribution**: the built `.mxo` links against your local FFmpeg install by absolute path (per-architecture, see "Universal builds" above). Fine on your own machine(s); sharing the patch with someone else requires them to install FFmpeg too (Homebrew, at the standard prefix for their Mac's architecture), or the dylibs need to be bundled into the `.mxo` and re-pathed (e.g. with `dylibbundler`) first.
- **Stereo output only**: input audio accepts any channel count via MC, but it's always downmixed to stereo before encoding.
- **Video format**: only char ARGB/RGB `jit_matrix` data is handled (this includes what the internal GL helper produces — it outputs a regular char matrix too).
- One `jit.gl.*` render source per stream at a time (see "How it works" above) — no compositing of multiple GL contexts.

---

## jit.rtmp.receive~

Runs an actual RTMP server *inside* a patch — point an encoder at its `@url` and it accepts the incoming publish, decodes the video to a Jitter matrix and the audio to a multichannel (MC) signal outlet.

### Features

- **This object is the server.** No separate nginx-rtmp/MediaMTX process needed — `@url` sets the local listen address, and libavformat's native RTMP protocol (in `listen` mode) does the accepting.
- **Video** comes out the left outlet as a normal `jit_matrix <name>` message — connect it to `jit.pwindow`, `jit.matrix`, `jit.gl.texture`, etc. like any other Jitter video source. The matrix is created/resized automatically to match the incoming stream's resolution (or force a size with `@width`/`@height`).
- **Audio** is decoded and resampled onto a single MC signal outlet — stereo by default, or any other width via `@channels` (see [How it works](#how-it-works-1)). One cable to `mc.dac~`, `mc.record~`, or anything else MC-aware, instead of managing individual mono/stereo cords.
- After a publisher disconnects, the server keeps listening for the next one — no need to re-send `start` for every reconnect. Send `stop` to shut the server down.
- All network I/O and decoding happens on a dedicated background thread; only the final matrix write/outlet happens on Max's main thread, so nothing here blocks Max's audio or scheduler threads.

An example patch is at [`patchers/jit.rtmp.receive~.test.maxpat`](patchers/jit.rtmp.receive~.test.maxpat).

### Quick start

```
[jit.rtmp.receive~] → (left outlet, jit_matrix)         [jit.pwindow]
                    → (middle outlet, multichannelsignal) [mc.dac~]
                    → (right outlet, anything)           [print]
```

```
url rtmp://0.0.0.0:1935/live/STREAM-KEY
start
```

Then point an encoder at that same URL, e.g. with ffmpeg:

```bash
ffmpeg -re -i input.mp4 -c:v libx264 -c:a aac -f flv rtmp://127.0.0.1:1935/live/STREAM-KEY
```

or from another Max patch running [`jit.rtmp.send~`](#jitrtmpsend) with the same `url`. The `app`/`streamkey` portion of the path (`live/STREAM-KEY` above) isn't validated by the built-in listener — it just needs a host/port that's actually reachable and free; any client that connects and publishes there is accepted.

### Attributes

| Attribute | Default | Description |
|---|---|---|
| `@url` | `""` | RTMP URL to listen on, e.g. `rtmp://0.0.0.0:1935/live/STREAM-KEY`. Host/port set the local listen address (port defaults to 1935 if omitted). |
| `@width` / `@height` | 0 / 0 | Force the output matrix to a fixed size. `0` = use the incoming stream's native resolution. |
| `@timeout` | -1 | Seconds to wait for an incoming connection before giving up. `-1` = wait indefinitely. |
| `@channels` | 2 | Number of channels on the MC audio outlet (1–16). Incoming audio is up/down-mixed to this count via `libswresample`'s default channel layout regardless of what the publisher actually sends. Like any MC channel count, a change takes effect on the next DSP compile (turn audio off/on), not immediately. |

### Messages

| Message | Description |
|---|---|
| `start` (alias `listen`) | Start the RTMP server at the configured `@url` and wait for a publisher. |
| `stop` | Stop the server and disconnect any current publisher. |

The rightmost outlet reports status and errors as messages: `status listening on <url>`, `status client connected`, `status live`, `status client disconnected`, `status stopped`, and `error <message>`.

### How it works

- **Server**: `start` spins up a background thread that calls libavformat's `avformat_open_input()` on the configured URL with the native `rtmp` protocol's `listen` option set — this *is* an RTMP server accepting a single incoming connection at a time, no external server process involved. An interrupt callback (checked during both the blocking accept and any blocking reads) lets `stop` abort promptly.
- **Per connection**: once a publisher connects, the object probes the stream, opens a decoder for whatever codec is actually present in each track (H.264/AAC in virtually all real-world RTMP publishes, but not hardcoded), and demuxes/decodes packets in a loop until the publisher disconnects — then goes back to listening for the next one.
- **Video**: each decoded frame is converted with `libswscale` into a tightly-packed ARGB buffer and swapped into a "latest frame" slot under a small mutex (decoder thread). A `c74::min::queue<>` (qelem) nudges the main thread once per frame — coalescing automatically if frames arrive faster than the main thread services them — which is where the actual Jitter matrix (created and registered once, resized in place via `setinfo` if the incoming resolution changes) gets written and the `jit_matrix <name>` message goes out. Object creation/registration/freeing all happen on the main thread only, since Max's object registry isn't safe to touch from a background thread.
- **Audio**: each decoded frame is resampled with `libswresample` to interleaved doubles at the current DSP sample rate (captured at `start` time) and the `@channels` count (also captured at `start` time), then written into a lock-free single-producer/single-consumer ring buffer; Max's audio thread reads from it every vector, silence-filling on underrun (e.g. before the first frame arrives, or if there's no audio track at all). The ring is sized against a fixed internal channel cap rather than the current `@channels` value, so changing the attribute never needs to resize/reallocate it while the decoder or audio thread might be touching it concurrently.
- **The MC outlet's width** is driven by Max's own multichannel-signal negotiation: since this object generates audio (it has no MC signal *inlet* to inherit a channel count from), it implements the `multichanneloutputs` class method that Max calls on every DSP compile — the same low-level mechanism the C SDK's own `mc.pack~`/`mc.rotate~` use — which just returns the current `@channels` value. min-api's higher-level attribute/message DSL doesn't expose this hook, so it's wired up directly via a `maxclass_setup` class method and a small trampoline function, per min-api's own escape hatch for raw Max C API calls.

### Known limitations

- **Distribution**: same as `jit.rtmp.send~` — the built `.mxo` links against your local FFmpeg install by absolute path.
- **Software decoding only**: no VideoToolbox hardware decode path (yet) — H.264 is decoded on the CPU.
- **One publisher at a time**: the built-in listener accepts one connection at a time, matching most single-camera/single-encoder use cases; there's no fan-in/compositing of multiple simultaneous publishers.
- **Fixed DSP sample rate per session**: the audio resampler targets whatever sample rate Max's DSP was running at when `start` was sent; changing the global sample rate mid-session isn't picked up until the next `start`.
- **`@channels` needs a DSP recompile to take effect**: like any other object's MC output width, it's fixed for the life of the current DSP chain — change the attribute, then toggle audio off/on (or reopen the patch) for it to apply.
- **No real channel mapping beyond what `libswresample` does by default**: if you set `@channels` higher than what the publisher actually sends, FFmpeg's default up-mix matrix is used (e.g. mono → all channels get a copy) rather than anything spatially meaningful — genuinely surround-aware publishers (5.1 AAC, etc.) pass through correctly, but this isn't a spatializer.
- The `app`/`streamkey` path segment isn't checked against anything — treat the listen URL's host/port as the actual access control (e.g. bind to `127.0.0.1` instead of `0.0.0.0` if you don't want it reachable from other machines on the network).

---

## jit.rtmp.server

Launches and manages a real, bundled [MediaMTX](https://github.com/bluenviron/mediamtx) media server as a background process — the plug-and-play option when you need one stream picked up by *multiple* independent players at once (VLC, Raspberry Pis, other Max patches) rather than a single point-to-point link. No separate install: a real MediaMTX binary ships inside this external's own `.mxo`.

This is a pure process-manager object — no audio/video passes through it. Pair it with [`jit.rtmp.send~`](#jitrtmpsend) pointed at its RTMP port, and any number of players can pull that same stream independently:

```
                    ┌──> jit.rtmp.receive~ (another Max patch)
jit.rtmp.send~ ──> jit.rtmp.server ──> VLC
   (in this patch)   (in this patch)  ──> another VLC, a Raspberry Pi, ...
```

This is the pattern for the "replace OBS + a hand-run RTMP server with one patch" use case: one Max patch runs both `jit.rtmp.server` (the server) and `jit.rtmp.send~` (pointed at that server's own `rtmp://127.0.0.1:1935/...`), and every headless player on the network just points at that machine's LAN IP on the same URL.

An example patch is at [`patchers/jit.rtmp.server.test.maxpat`](patchers/jit.rtmp.server.test.maxpat).

### Quick start

```
jit.rtmp.server:   start
jit.rtmp.send~:    url rtmp://127.0.0.1:1935/live/STREAM-KEY
jit.rtmp.send~:    start
```

Then, from any machine on the same network (swap `127.0.0.1` for this machine's actual LAN IP, e.g. `192.168.1.42`):

- **VLC**: File → Open Network Stream → `rtmp://192.168.1.42:1935/live/STREAM-KEY`
- **ffplay**: `ffplay rtmp://192.168.1.42:1935/live/STREAM-KEY`
- **Another Max patch**: `jit.rtmp.receive~` with `@url rtmp://192.168.1.42:1935/live/STREAM-KEY` — though for two Max patches talking directly to each other, skipping `jit.rtmp.server` entirely and pointing `send~` straight at `receive~`'s own listen URL is simpler (no middle server needed for a single point-to-point link — see [`jit.rtmp.receive~`](#jitrtmpreceive) above).

MediaMTX's default bundled config also serves the same feed as HLS (`http://192.168.1.42:8888/live/STREAM-KEY/`, playable in a browser or Safari/iOS directly) and WebRTC — handy extras, not just RTMP.

### Attributes

| Attribute | Default | Description |
|---|---|---|
| `@mediamtx_path` | `""` | Override path to a mediamtx binary. Empty = use the one bundled inside this external. |
| `@config` | `""` | Override path to a mediamtx YAML config file. Empty = use the default one bundled inside this external. Edit a copy of it to change ports, disable protocols you don't need, add authentication, etc. — see [mediamtx's own config reference](https://github.com/bluenviron/mediamtx#configuration). |

### Messages

| Message | Description |
|---|---|
| `start` | Launch the server. |
| `stop` | Stop it (graceful `SIGTERM`, escalating to a forced kill after ~1s if it doesn't exit). |
| `restart` | `stop` then `start`. |

The outlet reports status as messages: `status starting mediamtx`, `status mediamtx running (pid <n>)`, `status mediamtx stopped (...)`, `error <message>`, and `log <line>` for each line of the server's own console output (connection events, errors, etc. — useful for confirming a publisher/player actually connected).

### How it works

- **Bundling, not reimplementing**: serving an RTMP publish out to many simultaneous players (as opposed to `jit.rtmp.receive~`'s one-publisher-in ingest) isn't something FFmpeg's `libavformat` gives you for free — it would mean hand-implementing the RTMP handshake, AMF command parsing, and per-client fan-out from scratch, with real correctness risk that's hard to fully verify without live clients. MediaMTX already *is* a mature, correct implementation of exactly that, so this object manages a real copy of it as a subprocess instead of reimplementing it.
- **Self-contained**: the binary + its default config are fetched once at build time (see `jit.rtmp.server/CMakeLists.txt`) and copied into this external's own `Contents/Resources` — not committed to this repo as a binary blob, and not a separate install step for anyone using the built package. At runtime the object locates its own `.mxo` on disk via `dladdr()` (no dependency on the current working directory or a hardcoded path) and derives `Contents/Resources` from that.
- **Process lifecycle**: `start` spawns mediamtx with `posix_spawn` (no shell involved) and hands the whole lifecycle to one dedicated background thread — spawn, stream its stdout/stderr back as `log` messages, and either honor a `stop` request (`SIGTERM`, then `SIGKILL` if it hasn't exited within ~1s) or notice an unprompted crash — both converge on the same "child's output closed" signal, so both paths are reaped (`waitpid`) and reported the same way. Nothing here blocks Max's main thread except a bounded join when you send `stop`.

### Known limitations

- **macOS only** — same as the other two externals in this package; the bundling step in `CMakeLists.txt` only fetches a macOS (arm64/x86_64) binary today.
- **One server per machine, effectively** — MediaMTX binds fixed ports (1935 for RTMP, plus 8554/8888/8889/8890/8892 for RTSP/HLS/WebRTC/SRT/MoQ by default); running two `jit.rtmp.server` instances (in one patch or two) at once will fail to bind the second one. One instance serving multiple `jit.rtmp.send~`s on different stream keys is fine.
- **No built-in authentication** in the bundled default config — anyone who can reach the machine's IP on these ports can publish or play. Fine on a trusted LAN (the Pi-display use case this was built for); edit the bundled `mediamtx.yml` (via `@config`) and see MediaMTX's own docs if you need auth or you're exposing this beyond a trusted network.

---

## License

[MIT](LICENSE). Note that Homebrew's default FFmpeg build enables `libx264`, which requires `--enable-gpl`; if you distribute *compiled binaries* of `jit.rtmp.send~`/`jit.rtmp.receive~` linked against a GPL-enabled FFmpeg build, that carries GPL obligations independent of this repository's own (MIT) license. `jit.rtmp.server` bundles an unmodified [MediaMTX](https://github.com/bluenviron/mediamtx) binary, also MIT-licensed — its license is copied alongside it into `Contents/Resources/LICENSE` in the built `.mxo`.
