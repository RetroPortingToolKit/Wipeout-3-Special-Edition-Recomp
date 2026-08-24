# Semantic audit: `0x8012E408` / `CreateObjectInstance`

## Result

`0x8012E408` is a deep-copy constructor for a compact named object instance.
The retained allocation diagnostic starts with `New Instance`, while callers
use the returned pointer as an independently mutable instance. A descriptive
catalog name is therefore `CreateObjectInstance`.

```c
ObjectInstance *CreateObjectInstance(const ObjectInstance *source);
```

The function returns the newly allocated instance in `v0`.

## Exact boundary and allocation behavior

The owner spans `[0x8012E408, 0x8012E534)`, exactly `0x12C` bytes. It has a
32-byte frame, ten basic blocks, one entry, and one `jr ra` return. The next
independent generated owner starts at `0x8012E534`.

It performs two allocations through `AllocateHeapBlock` (`0x8011686C`) using
the arena pointer at `0x8018F098`:

1. allocate `0x28` bytes for the instance;
2. allocate `0x50` bytes for the payload owned through instance offset `+0x20`.

Each null result is reported through `0x800F82EC` with severity 2 and the
literal at `0x8016E56C`: `New Instance could not allocate free memory`. The
diagnostic path then rejoins construction, matching the title's fatal/error
handler convention rather than implementing local recovery.

## Proven copy layout

The constructor copies these source fields into the new allocation:

- bytes `+0x00..+0x0F`, an exact 16-byte name/identifier;
- bytes `+0x10` and `+0x11`;
- halfword `+0x12`;
- words `+0x14`, `+0x18`, and `+0x1C`;
- the complete 80-byte block referenced by pointer `+0x20`.

The payload copy is the Psy-Q `memcpy` thunk at `0x8015CF38`, called with
`(new_payload, source->payload, 0x50)`. The copied payload words at offsets
`+0x44`, `+0x48`, and `+0x4C` are also assigned explicitly from the source;
this is redundant in the emitted instruction stream but confirms those values
belong to the copied payload. Source word `+0x14` is preserved, while the new
instance word at `+0x24` is always cleared instead of cloned.

The constructor does not mutate the source, publish the result globally, or
link it into a list; those operations remain caller-owned.

## Complete call-target proof

An aligned scan of the loaded executable finds exactly twelve direct JALs to
the entry and none to an interior address:

```text
800EFFC4
80131CF0
80132408  80132424  80132444  80132590  80132684
80157940  801579BC  80157A44  80157B34  80157B70
```

They group under four generated owners: `0x800EFEE4`, `0x8013199C`,
`0x80132250`, and the pair `0x801577B0`/`0x80157A98`. Several callers load
source pointers from the shared resource table rooted at `0x80197344`; others
store returned clones into caller-owned pointer arrays. This agrees with a
resource-template-to-runtime-instance constructor.

## Confidence

`confirmed`: entry and end, both allocation sizes, exact copied fields, owned
payload cloning, reset field, return value, retained failure diagnostic, and
all incoming direct JALs are instruction-proven. `Object` is intentionally
generic because this function is shared by ship and non-ship setup paths.
