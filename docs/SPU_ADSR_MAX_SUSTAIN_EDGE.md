# SPU ADSR maximum-sustain edge

## Audit result

The Gaussian voice path matches the pinned hardware definition: it uses the
literal 512-entry table, pitch-counter bits 4–11, the documented tap order,
four independent arithmetic shifts, and preserved three-sample block history.
No Gaussian production change was warranted.

## Closed defect

ADSR sustain level `0xF` encodes a decay target of `(15+1)*0x800 = 0x8000`.
Attack saturates at `0x7FFF`, so that target is already met when Attack ends.
The core clamped the target to `0x7FFF`, entered Decay, and applied one
exponential decrement before switching to Sustain.

Attack now enters Sustain directly only for this unreachable-above-maximum
target. Sustain levels `0x0` through `0xE` retain the normal Decay path and
counter reset.

## Native-boundary proof

The adversarial voice uses fastest Attack, maximum sustain level, and a frozen
Sustain rate. It reaches `0x7FFF` and Sustain on the third 44.1 kHz edge with
no decay sample. A snapshot at that exact transition restores into a fresh SPU;
both paths reproduce the remainder of a 441-sample 100 Hz update, complete
device digest, and fixed full-update audio digest.

An adjacent hostile vector programs sustain level `0xE` and proves that the
same third edge still enters Decay at `0x7FFF`; only the encoded `0x8000`
target receives the no-decay transition.

The 441-sample stereo FNV-1a digest is `10452916077976385533`.

## Remaining boundaries

Sub-sample key/MMIO ordering, the unresolved third voice RAM read, physical
sound-RAM arbitration, and exact wet-reverb silicon rounding remain unclaimed.
