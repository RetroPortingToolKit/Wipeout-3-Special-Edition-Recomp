# Clean native host foundation

This directory is a platform-neutral host boundary for the clean static-AOT
lane. It contains no guest CPU, GPU emulation, PGXP implementation, texture
replacement lookup, game fix, or legacy runner dependency.

The contract has three independent clocks:

* The emulation update clock is fixed at 100 Hz. A `StereoAudioBatch` contains
  exactly 441 interleaved stereo sample frames at 44,100 Hz. The headless
  backend rejects every other batch size.
* `MonotonicPacer` is presentation-only. It can schedule distinct frames at
  exactly 25, 50, or 100 Hz using rational nanosecond deadlines. It never
  advances a game tick, changes an audio batch, skips a missed deadline, or
  repeats a frame. A late result is reported to the caller.
* Window, fullscreen, controller, and device events are bounded messages. A
  full queue returns backpressure; it never overwrites an older message.

Frame and audio pointers are borrowed. The producer keeps the memory alive
until the backend's service call consumes that item. The fixed SPSC queues
store descriptors only, so the host adds no per-frame allocation. The caller
must use an external fixed render/audio pool when a producer and consumer run
on different threads.

`calculate_viewport` provides 4:3, 16:9, 21:9, and 32:9 fit policies with
exact reduced-rational fractional or largest-fitting-integer scaling. It produces integer pixel
rectangles and has no connection to `MonotonicPacer` or the game clock.

`render_interfaces.hpp` is deliberately only a seam for the later GPU,
PGXP, and replacement-texture implementations. Those systems own their
guest-derived data and can provide an `IFrameEncoder` to the Windows
presentation shell.

## Optional Windows presentation shell

Configure `-DCLEAN_AOT_HOST_ENABLE_SDL_VULKAN=ON` to fetch the pinned official
SDL3 3.4.10 source archive and build
`clean_aot_host_sdl_vulkan`. The shell:

* loads `vulkan-1.dll` with the system-only `LoadLibraryExW` search policy and confirms
  `vkGetInstanceProcAddr` before asking SDL to use the Vulkan driver;
* uses SDL3's Vulkan-backed GPU device and claimed window, with two frames in
  flight and a fixed descriptor queue;
* handles bounded SDL event pumping without dropping consumed events,
  resize/fullscreen requests and transition events, clean device-loss
  signaling, and idle-before-reclaim swapchain recreation;
* consumes the embedded SPIR-V sanity shaders at device initialization; and
* clears a swapchain when no `IFrameEncoder` is installed. Actual game frame
  upload and presentation belongs to the later renderer tranche.

The Vulkan SDK is therefore not a runtime dependency. A Windows release still
needs the SDL3 runtime DLL and a Vulkan-capable driver. The current shell does
not provide an audio device; the display-free headless backend is the accepted
audio contract for this tranche.

## Shader path

`generated/present_shaders.hpp` contains the verified runtime SPIR-V. Setting
`-DCLEAN_AOT_HOST_REBUILD_SHADERS=ON` runs the user-scoped official DXC
1.9.2602.17 executable, validates the SPIR-V magic/alignment, computes the
SHA-256 values, and writes a build-directory header. Runtime code never invokes
DXC and never compiles shaders.

## Display-free verification

From the repository root:

```text
cmake -S clean-aot/host -B build-host -G Ninja \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

The test executable uses a fake monotonic clock and no SDL, GPU, audio device,
window, or Vulkan loader. It adversarially covers exact 25/50/100 schedules,
wait/commit ordering, queue-full retry, sequence recovery, tight final-row
storage, 100 batches of 441 stereo frames, controller state,
resize/fullscreen events, and exact rational viewport policies.
