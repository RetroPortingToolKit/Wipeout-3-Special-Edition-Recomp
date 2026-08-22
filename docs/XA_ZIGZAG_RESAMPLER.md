# Native XA zigzag resampler

The previous XA path preserved exact 37.8/18.9-to-44.1 kHz frame counts but
used two-point linear interpolation. That policy materially changed every XA
waveform and therefore the audio accompanying MDEC sequences.

The converter now clocks a bounded 32-frame history at 37.8 kHz and emits the
seven documented phases after every six inputs. An 18.9 kHz source sample is
entered twice, producing the native 7/3 ratio. Each of the 29 products is
divided by `0x8000` in fixed-width signed arithmetic before accumulation and
the result is saturated to 16 bits. No host clock or floating point is used.

The typed snapshot includes source rate, input/output totals, all history,
ring position, six-step phase, and terminal state. Restore rejects invalid
rates, counters, topology, or phase without mutation. Tests pin two consecutive
seven-phase impulse vectors, arbitrary input chunk equivalence, exact full- and
half-rate sector counts, output-capacity atomicity, and mid-phase replay.

The pinned public measurements explicitly report small residual rounding
differences from silicon. Those unknown low-order details and XA emphasis
remain unwaived; the implementation does not claim bit-exact drive silicon.
