Option labels, selected instructions, and instruction ownership were recovered
from Hueponik's unified PAL patch at commit
`ac46a5dc500a9581ea55b8efb5c1a0d4be944aba`.

Hueponik's `Stock` menu choice is intentionally the constant `0x3000`, not the
original load instruction. This package preserves that exact behavior. It
contains no patch bytes, guest code engine, high-RAM data, or menu detour.
