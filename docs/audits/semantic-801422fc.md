# Semantic audit: `0x801422FC`

## Result

`0x801422FC` is a genuine function boundary and is best named
`SubmitSceneSectionFaces`.  It submits the two render lists selected by one
scene-section descriptor; it does not walk the scene hierarchy or perform the
outer section visibility decision.

The compiler range is exact: `0x801422FC..0x80142633` (`0x338` bytes).  The
entry builds a 72-byte frame and saves `ra`, `fp`, and `s0..s7`; the epilogue at
`0x80142604..0x80142630` restores that frame and returns.  No internal address
is a separately callable function.

## Caller and ABI

There is one static `jal`, at `0x80124054`.  Its delay-slot predecessor loads
`a0 = *(s1 + 0)`, so the public input is a pointer to a two-word submission
descriptor:

```c
struct SceneSectionSubmit {
    void *owner;          // +0: owner/model object used for transform and draws
    void *section_table;  // +4: section-indexed render-list metadata
};

void SubmitSceneSectionFaces(struct SceneSectionSubmit *submit);
```

The field type names remain provisional, but the offsets and single-argument
calling convention are direct instruction evidence.  The return value is not
used and the routine does not intentionally produce one.

Immediately before this call, the caller invokes `0x80135B8C` with clip/frustum
parameters assembled from its current traversal record.  Thus visibility and
section selection happen in the caller; `0x801422FC` receives an already chosen
submission descriptor.

## Render behavior

1. `0x80142310..0x80142358` derives a small mode value from globals
   `0x801762D4`/`0x801762D8`.  A sentinel combination exits without drawing.
   The value selects a signed section index from the halfword table at
   `0x801762DC` and temporarily sets corresponding low bits in
   `0x801762CC`.
2. `0x80142364..0x8014238C` follows `submit->owner + 0x44` and calls
   `0x80136078`.  That callee copies a nested transform into the GTE scratch
   matrix at `0x1F800010`, confirming that this function installs the owner's
   render transform before submission.
3. The first list is selected from `submit->section_table` using the signed
   section index.  Its pointer is read at indexed offset `+0x20` and its signed
   count at indexed offset `+0x2C`.  `0x801423C4..0x801423F8` consumes signed
   16-bit face indices and calls `0x8016FC28(owner, face_index)` once per entry.
   `0x8016FC28` is the low-level face/material dispatch path, not a sibling
   section walker.
4. The second list starts at the same indexed pointer.  Each 16-bit packed word
   carries an ID in bits `0..9` and a run length of `((word >> 10) & 7) + 1`.
   The ID selects a 64-byte entry beneath the table reached through
   `0x801A85D4 + 0x14`.  The routine resolves a render object through that
   entry's `+0x1C` index and the pointer table at `0x801CD984`.
5. Before each indirect submission, it patches three words at resolved-object
   offsets `+0x14`, `+0x18`, and `+0x1C` from the object's `+0x28`, `+0x2C`,
   and `+0x30` fields.  The call at `0x801424D8` uses the function pointer at
   resolved-object offset `+0`, with `a0 = resolved_object` and `a1 = owner`.
   The run length advances through consecutive 64-byte table entries and
   repeats that polymorphic renderer dispatch.
6. The analogous loops at `0x80142580..0x801425E8` advance to subsequent list
   records until the section count is exhausted.  `0x801425EC..0x80142600`
   clears the temporary low bits in `0x801762CC` before returning.

## Naming confidence and limits

The former `SceneSectionDraw` name was directionally close, but obscured that
this is the terminal submit stage rather than the scene-section traversal or
visibility routine.  `SubmitSceneSectionFaces` is marked `known` because the
boundary, sole caller, transform setup, indexed-face loop, packed run decoding,
and both direct/indirect submission mechanisms are all explicit in the code.

Names for the owner object, section-table record, and packed-ID catalog remain
semantic placeholders until their producers are audited.  In particular, this
audit does not claim that packed bits `0..9` are a texture or material ID; only
their observed table-selection role is established.
