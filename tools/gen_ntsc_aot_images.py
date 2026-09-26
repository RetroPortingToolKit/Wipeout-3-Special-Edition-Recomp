#!/usr/bin/env python3
"""Declare the NTSC Mode package's patched boot-EXE code as AOT images.

hueponik's 
tsc build edits SCES_028.45 in place: detours in ~150 stock
functions into code it packs into the zero padding between the game's own
strings (0x80174A08.., team/track names) and a NOP run at 0x8017B8F0. The
runtime runs a main-EXE function natively only while its bytes equal the
stock image the game C was compiled from, so without these images every
patched function (several of them per-frame) runs in the dirty-RAM
interpreter.

This rewrites aot/overlays.json's ixed_address_extents image for the
ramerate-ntsc view from original inputs only:

* the package's changed boot-EXE words (tools/mod_package_images.py view),
  excluding the NFE.PB copy linked at the start of the EXE (data; its menu
  member is its own indexed_lzss_members image);
* the stock recompile's function list (generated/SCES_028.45_full.ranges,
  static analysis of the stock EXE): every function containing a changed word,
  grown to its whole function (the stock recompile chunks large ones), starting
  on its 4 KB page; touching spans merge;
* the caves: only the code the package's J/JAL reach, one extent per
  contiguous run, so no image holds the strings in between;
* entries: the stock function entries (callers keep dispatching there),
  	ransfer_entries (targets of J/JAL words the package wrote: cave entries
  and the resume points the caves jump back to), and the branch/jump targets
  of code reached from those transfers (a loop resumed into from a cave).

Two changed spans are data, not code, and are left out (DATA_SPANS). The
extents sit where the BIOS loads them (load_addr == address).
Run after Generate (it reads generated/SCES_028.45_full.ranges):
    py -3 tools/gen_ntsc_aot_images.py --disc <cue>
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VIEW = 'framerate-ntsc'
EXE = 'SCES_028.45'
BASE = 0x80010000
PACK_END = BASE + 0x2484C   # the NFE.PB copy linked at 0x80010000
DATA_SPANS = {
    0x80178014: 'Halfword table the stock analysis read as code; the package '
                'changes one value (0x801783C8: 30 -> 36).',
    0x8017BC3C: "Option descriptors for the package's settings code at "
                '0x8017B90C (value tables and the instruction halfwords it '
                'rewrites), not code.',
}
PACK_EXCLUSION = dict(
    start=hex(BASE), end=hex(PACK_END),
    reason='The NFE.PB copy linked at the start of the EXE: data the package '
           'replaced (menu member 5 is decoded to 0x800BF800 by its own image); '
           'words there that decode as J/JAL are not transfers.')


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--disc', required=True)
    parser.add_argument('--framework', default=str(ROOT / 'psxrecomp'))
    args = parser.parse_args()
    sys.path.insert(0, str(Path(args.framework) / 'tools'))
    import aot_overlay_pipeline as aop

    path = ROOT / 'aot/overlays.json'
    profile = json.loads(path.read_text(encoding='utf-8'))
    disc = aop.Disc(args.disc)
    view = aop.mod_package_views(profile, disc, ROOT)[VIEW]
    stock, patched = disc.read(EXE), view.read(EXE)

    def word(buf, va):
        return struct.unpack_from('<I', buf, 0x800 + va - BASE)[0]

    functions, current = [], None
    for line in (ROOT / 'generated' / f'{EXE}_full.ranges').read_text().splitlines():
        if line.startswith('F '):
            current = [int(line[2:], 16), []]
            functions.append(current)
        elif line.startswith('R '):
            lo, size = (int(x, 16) for x in line[2:].split())
            current[1].append((lo, lo + size))
    end = BASE + len(stock) - 0x800
    changed = [va for va in range(PACK_END, end, 4) if word(stock, va) != word(patched, va)]
    changed_set = set(changed)
    hit = [(entry, min(lo for lo, _ in ranges), max(hi for _, hi in ranges))
           for entry, ranges in functions
           if any(va in changed_set for lo, hi in ranges for va in range(lo, hi, 4))]
    owned = {va for _, lo, hi in hit for va in range(lo, hi, 4)}
    if changed_set - owned:
        raise SystemExit(f'{len(changed_set - owned)} changed words lie in no stock function')
    # The stock recompile splits a large function into chunks at interior
    # entry points; the overlay compiler hosts such an entry in its whole
    # function, so an extent must reach back to the chunk that opens with the
    # prologue and on to the last chunk before the next one. NOP-sled chunks
    # (the caves' stock bytes) are left alone.
    by_hi, by_lo = {}, {}
    for _, ranges in functions:
        lo, hi = min(l for l, _ in ranges), max(h for _, h in ranges)
        by_hi[hi] = min(by_hi.get(hi, lo), lo)
        by_lo[lo] = max(by_lo.get(lo, hi), hi)

    def prologue(va):
        x = word(stock, va)
        return x >> 16 == 0x27BD and x & 0x8000

    def whole_function(lo, hi):
        if word(stock, lo) == 0:
            return lo, hi
        while not prologue(lo) and lo in by_hi and word(stock, by_hi[lo]) != 0:
            lo = by_hi[lo]
        while hi in by_lo and not prologue(hi) and word(stock, hi) != 0:
            hi = by_lo[hi]
        return lo, hi

    hit = [(entry, *whole_function(lo, hi)) for entry, lo, hi in hit]
    spans = []
    for _, lo, hi in sorted(hit, key=lambda item: item[1:]):
        if spans and lo <= spans[-1][1]:
            spans[-1][1] = max(spans[-1][1], hi)
        else:
            spans.append([lo, hi])

    written = [(a, w) for a, w in view.written_words() if not BASE <= a < PACK_END]
    all_transfers = {((a + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
                     for a, w in written if w >> 26 in (2, 3)}
    missing = set(DATA_SPANS) - {lo for lo, _ in spans}
    if missing:
        raise SystemExit(f'data spans no longer changed: {sorted(map(hex, missing))}')
    for lo, hi in spans:
        if lo in DATA_SPANS and any(lo <= t < hi for t in all_transfers):
            raise SystemExit(f'data span {lo:#x} is a transfer target')
    # A function extent starts on its 4 KB page: the overlay compiler reads an
    # image from its page start and may host an interior entry in any function
    # it finds there, so every byte it can reach must be a declared (and
    # hash-pinned) input. It ends where its last function ends. A cave (stock
    # NOP sled) keeps its exact span: the bytes around it are string data.
    pages = []
    for lo, hi in spans:
        if lo in DATA_SPANS:
            continue
        cave = word(stock, lo) == 0
        if not cave:
            lo &= ~0xFFF
        if pages and lo <= pages[-1][1]:
            if cave or pages[-1][2]:
                raise SystemExit(f'cave at {lo:#x} touches function code')
            pages[-1][1] = max(pages[-1][1], hi)
        else:
            pages.append([lo, hi, cave])
    def reachable(entries, lo, hi):
        """Words the code entered at entries executes -- sequential flow,
        branch targets and delay slots, up to an unconditional J/JR; calls
        return to the word after their delay slot -- and its conditional
        branches as (site, target)."""
        seen, branches, work = set(), [], list(entries)
        while work:
            pc = work.pop()
            while lo <= pc < hi and pc not in seen:
                seen.add(pc)
                x = word(patched, pc)
                op, rs, rt, funct = x >> 26, (x >> 21) & 31, (x >> 16) & 31, x & 63
                if op in (4, 5, 6, 7) or (op == 1 and rt in (0, 1, 16, 17)):
                    off = x & 0xFFFF
                    target = pc + 4 + ((off - 0x10000 if off & 0x8000 else off) << 2)
                    branches.append((pc, target))
                    work.append(target)
                if op in (2, 3):
                    target = ((pc + 4) & 0xF0000000) | ((x & 0x3FFFFFF) << 2)
                    if op == 2:
                        branches.append((pc, target))
                    work.append(target)
                if op == 0 and funct == 8 and rs != 31:
                    raise SystemExit(f'cave jr via register at {pc:#x}: jump table not modelled')
                if op == 2 or (op == 0 and funct == 8) or (op == 4 and rs == 0 and rt == 0):
                    if lo <= pc + 4 < hi:
                        seen.add(pc + 4)   # delay slot
                    break
                pc += 4
        return seen, branches

    def extent_for(lo, hi):
        body = patched[0x800 + lo - BASE:0x800 + hi - BASE]
        return dict(file=EXE, file_offset='0x800', base=hex(BASE), address=f'0x{lo:08X}',
                    size=hex(hi - lo), load_addr=f'0x{lo:08X}',
                    sha256=hashlib.sha256(body).hexdigest())

    def transfer_spec(lo, hi):
        count = sum(1 for t in all_transfers if lo <= t < hi)
        return {'from': 'mod_package_writes', 'count': count, 'exclude_writes': [PACK_EXCLUSION]}

    extents = []
    for lo, hi, cave in pages:
        if not cave:
            extent = extent_for(lo, hi)
            entries = {e for e, _, _ in hit if lo <= e < hi and word(stock, e) != 0}
            # Code a cave resumes into is compiled as a fragment walked forward
            # from the resume point; a loop there branching back above it would
            # leave native code. Those branch and jump targets are entries.
            _, flow = reachable(sorted(t for t in all_transfers if lo <= t < hi), lo, hi)
            entries |= {t for _, t in flow if lo <= t < hi}
            entries = sorted(entries)
            if entries:
                extent['entries'] = [f'0x{e:08X}' for e in entries]
            extent['transfer_entries'] = transfer_spec(lo, hi)
            extents.append(extent)
            continue
        # A cave: the zero padding between the game's own strings (team and
        # track names, pilot tables), entered only through the package's
        # J/JAL. Only the code those transfers reach becomes native, one extent
        # per contiguous run, so no image holds the strings in between: the
        # recompiler sweeps a function linearly to the image end, and a string
        # decoded as a reserved branch fails the audit. Its pointer tables
        # point at strings, not callbacks, so pointer-table seeding is off.
        code, branches = reachable(sorted(t for t in all_transfers if lo <= t < hi), lo, hi)
        pieces, start = [], None
        for va in range(lo, hi + 4, 4):
            if va < hi and va in code:
                start = va if start is None else start
            elif start is not None:
                pieces.append((start, va))
                start = None
        for p_lo, p_hi in pieces:
            extent = extent_for(p_lo, p_hi)
            # A conditional branch from another run lands here: an entry.
            entries = sorted({t for site, t in branches
                              if p_lo <= t < p_hi and not p_lo <= site < p_hi})
            if entries:
                extent['entries'] = [f'0x{e:08X}' for e in entries]
            extent['supplemental_entries'] = False
            extent['transfer_entries'] = transfer_spec(p_lo, p_hi)
            extents.append(extent)
    image = {
        'method': 'fixed_address_extents',
        'mod_package': VIEW,
        'allow_missing': True,
        'reason': "The package's patched boot-EXE functions and its two code caves, where the "
                  'BIOS loads them. Stock functions containing a changed word (stock recompile '
                  'function list), touching spans merged; data spans left out. Generated by '
                  'tools/gen_ntsc_aot_images.py.',
        'extents': extents,
    }
    # One recipe per extent; every other image keeps its own count.
    old = [i for i in profile['images']
           if i.get('mod_package') == VIEW and i['method'] == 'fixed_address_extents']
    profile['images'] = [i for i in profile['images'] if i not in old] + [image]
    profile['expected_records'] += len(extents) - sum(len(i['extents']) for i in old)
    path.write_text(json.dumps(profile, indent=2, ensure_ascii=False) + '\n',
                    encoding='utf-8', newline='\n')
    print(f'{len(changed)} changed code words, {len(hit)} functions, {len(extents)} extents, '
          f'{sum(int(e["size"], 16) for e in extents):#x} bytes, '
          f'{sum(len(e.get("entries", [])) for e in extents)} entries, '
          f'{sum(e["transfer_entries"]["count"] for e in extents)} transfer entries, '
          f'expected_records {profile["expected_records"]}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
