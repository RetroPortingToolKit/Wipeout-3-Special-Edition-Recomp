# Host provenance and clean-room boundary

All host implementation code in this directory was written for the clean-AOT
lane. No source, generated code, ABI wrapper, test, or design conclusion was
copied from psxrecomp, recomp-ui, DuckStation, GooseStation, or the prior
Claude-era checkout.

The only external behavior and API references used for the optional Windows
shell are the official primary sources below.

| Dependency/reference | Pin or identity | Use |
| --- | --- | --- |
| SDL3 official release | `release-3.4.10`; source archive SHA-256 `12b34280415ec8418c864408b93d008a20a6530687ee613d60bfbd20411f2785` | CMake `FetchContent` source dependency, only when the optional backend is enabled |
| SDL3 official Vulkan API | SDL3.2+ API contract | `SDL_Vulkan_LoadLibrary`, `SDL_Vulkan_UnloadLibrary`, and the Vulkan-backed SDL GPU path |
| Vulkan Loader official guide | dynamic symbol lookup / Windows loader model | runtime loader policy; no import-library or SDK runtime link |
| Microsoft DXC official release | tag `v1.9.2602`; compiler reports `1.9.2602.17` | build-time HLSL-to-SPIR-V compiler only |
| Local user-scoped DXC executable | SHA-256 `b9cff94181248e080804b385da8964b6319fd07760721baa9053a891cf7a727` | verification machine identity; never shipped or loaded by the runtime |

Official URLs:

* https://github.com/libsdl-org/SDL/releases/tag/release-3.4.10
* https://wiki.libsdl.org/SDL3/SDL_Vulkan_LoadLibrary
* https://wiki.libsdl.org/SDL3/SDL_Vulkan_GetInstanceExtensions
* https://wiki.libsdl.org/SDL3/SDL_CreateGPUDevice
* https://wiki.libsdl.org/SDL3/SDL_ClaimWindowForGPUDevice
* https://docs.vulkan.org/guide/latest/loader.html
* https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2602

The checked-in shader artifacts were produced by that DXC identity with
Vulkan 1.2 SPIR-V output:

* `present.vert.spv`: 700 bytes,
  SHA-256 `9b022617e566718270ef1147cee36f97d02f3986f4a8d144bae50450f0040fcc`
* `present.frag.spv`: 336 bytes,
  SHA-256 `20b34d9e9ce2edcfe62734c5113c7aa55c8dfb7446587db2e22ef96b8b32713f`

The build-time script rejects a compiler whose reported version is not
`1.9.2602.17`, rejects non-word-aligned output, checks the SPIR-V magic word,
and records the resulting SHA-256 in the generated header. It does not put
DXC or a shader compiler in the runtime dependency graph.
