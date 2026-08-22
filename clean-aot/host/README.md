# Clean native host foundation

## Native production presentation

The Windows SDL3/Vulkan implementation is a complete host backend: it owns the
window, Vulkan swapchain, 44.1-kHz audio stream, frame queue, controller state,
and replacement textures. RGB scanout is converted into a reusable RGBA8
staging texture, uploaded in a GPU copy pass, sampled by pinned SPIR-V, and
drawn into the calculated 4K viewport. Exact 4:3 is the default; widescreen
aspect policies must be selected explicitly.

Replacement PNG pixels use the same upload contract in native and headless
operation. Native uploads retain GPU textures while headless uploads allocate
deterministic handles and digest the exact RGBA bytes.

This directory is a platform-neutral host boundary for the clean static-AOT
lane. It contains no guest CPU, game fix, title-specific graphics policy, or
legacy runner dependency.

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

`render_interfaces.hpp` now connects the generic production graphics
contracts. A GPU polygon's immutable raw packet is reparsed, matched against
generation-checked GTE/RAM candidates, assembled all-or-nothing, and emitted as
clip-space host vertices with storage/origin generations. Horizontal expansion
fails closed if any triangle falls back to native coordinates. RGB888 scanout
is bound without copying into `FrameSubmission`, while the 4K viewport remains
an independent presentation policy. Replacement lookup receives the opaque
two-word key and source generation; asset loading and title policy stay above
the host.

## Optional Windows presentation shell

Configure `-DCLEAN_AOT_HOST_ENABLE_SDL_VULKAN=ON` to fetch the pinned official
SDL3 3.4.10 source archive and build
`clean_aot_host_sdl_vulkan`. The shell:

* loads `vulkan-1.dll` with the system-only `LoadLibraryExW` search policy and confirms
  `vkGetInstanceProcAddr` before asking SDL to use the Vulkan driver;
* uses SDL3's Vulkan-backed GPU device and claimed window, with configurable
  one-to-three frames in flight (one by default for latency) and explicit
  vsync, immediate, mailbox, or low-latency 100 Hz present-mode policy;
* handles bounded SDL event pumping without dropping consumed events,
  resize/fullscreen requests and transition events, clean device-loss
  signaling, and idle-before-reclaim swapchain recreation;
* consumes the embedded SPIR-V sanity shaders at device initialization; and
* reports display refresh as an exact rational when SDL supplies it, and keeps
  accepted frames, acquired images, GPU present submissions, queue latency,
  sequence faults, resize/reclaim, and device loss as separate counters; and
* requires contiguous frame and update identities. SDL GPU exposes no
  compositor/scanout completion feedback, so `display_presentations_confirmed`
  deliberately remains zero and VRR remains unknown rather than being inferred
  from a successful GPU submission.

At device creation the Vulkan path probes `VK_KHR_present_id` plus
`VK_KHR_present_wait`, then `VK_GOOGLE_display_timing`, by requesting each
extension set through SDL's Vulkan options and falling back to the portable
device when unsupported. Extension enablement is diagnostic only: SDL's public
GPU API does not expose its `VkDevice`, queue, swapchain, or a way to attach a
present ID. A platform bridge may provide `IPresentCompletionProbe` when those
native objects are legitimately available. Completion receipts are then bound
to a swapchain generation and contiguous present ID, checked for monotonic
timestamps and 100 Hz interval tolerance, and fail closed on stale, duplicate,
out-of-order, or future receipts. Resize starts a new evidence generation;
device loss cannot be reported as a completion.

`DirectVulkanPresentBridge` is the opt-in native path for systems that require
physical evidence. SDL creates its window and `VkSurfaceKHR`; the bridge owns
the Vulkan instance, physical/logical device selection, graphics/present
queues, command pool, synchronization, and swapchain. It attaches strictly
increasing `VkPresentIdKHR` values and waits with `vkWaitForPresentKHR`, or uses
requested/past presentation timing with `VK_GOOGLE_display_timing`. A hidden
window owns a surface but deliberately has no swapchain or completion. Resize
destroys all swapchain-scoped state after device idle and advances the evidence
generation. Unsupported extensions leave the source `unavailable` and never
manufacture a completion. The stock SDL GPU backend remains the portable
fallback. The direct bridge dependency is pinned to vk-bootstrap 1.3.302 and
uses SDL's pinned Vulkan headers; no Vulkan SDK installation is required.

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
