#!/usr/bin/env python3
"""Close authenticated BIOS-resident shell frontier slices into static AOT shards."""
from __future__ import annotations
import argparse, hashlib, json, struct, sys
from pathlib import Path

SCHEMA="wo3se.bios-shell-static-shards.v1"; MANIFEST_SCHEMA="clean-aot-compiler-manifest-v1"
class ShellShardError(ValueError): pass
def sha(b): return hashlib.sha256(b).hexdigest()
def canonical(v): return (json.dumps(v,sort_keys=True,separators=(",",":"))+"\n").encode()
def parse_manifest(path):
 d={}
 for line in path.read_text(encoding="utf-8").splitlines():
  if not line or line.startswith("#"): continue
  if "=" not in line: raise ShellShardError(f"malformed manifest line in {path}")
  k,v=line.split("=",1); d.setdefault(k,[]).append(v)
 if d.get("schema")!=[MANIFEST_SCHEMA]: raise ShellShardError("unsupported compiler manifest")
 return d
def ranges(pcs):
 out=[]
 for pc in sorted(pcs):
  if out and out[-1][1]==pc: out[-1]=(out[-1][0],pc+4)
  else: out.append((pc,pc+4))
 return out
def in_ranges(pc,allowed): return any(a<=pc<b for a,b in allowed)
def successors(pc,raw):
 op=raw>>26; fn=raw&63
 if op in (2,3): return {((pc+4)&0xf0000000)|((raw&0x03ffffff)<<2)}, op==3
 if op in (1,4,5,6,7) or (op in (16,17,18) and ((raw>>21)&31)==8):
  imm=raw&0xffff; imm=imm-0x10000 if imm&0x8000 else imm
  return {pc+8,pc+4+(imm<<2)},False
 if op==0 and fn in (8,9): return None,fn==9
 if op==0 and fn in (12,13): return set(),False
 return {pc+4},False
def build(image,base,manifests):
 identity=sha(image); allowed=[]; roots=set(); slices=[]
 for path,data in manifests:
  start=int(data["load_start"][0],0); end=int(data["load_end_exclusive"][0],0); expected=data["image_sha256"][0]
  if not(base<=start<end<=base+len(image)) or sha(image[start-base:end-base])!=expected: raise ShellShardError(f"slice identity mismatch: {path}")
  codes=[]
  for item in data.get("code",[]):
   a,b=(int(x,0) for x in item.split("-",1))
   if not(start<=a<b<=end) or a%4 or b%4: raise ShellShardError("invalid slice code range")
   allowed.append((a,b)); codes.append({"start":a,"end_exclusive":b})
  roots.update(int(x,0) for x in data.get("entry",[]))
  slices.append({"manifest":path.name,"identity_sha256":expected,"load_start":start,"load_end_exclusive":end,"roots":[int(x,0) for x in data.get("entry",[])],"ranges":codes})
 allowed=sorted(set(allowed)); merged=[]
 for a,b in allowed:
  if merged and a<=merged[-1][1]: merged[-1]=(merged[-1][0],max(b,merged[-1][1]))
  else: merged.append((a,b))
 if any(not in_ranges(r,merged) for r in roots): raise ShellShardError("root outside exact slice authority")
 discovered=set(); direct={}; indirect=[]; calls=[]; delay_pairs=[]; work=list(sorted(roots))
 while work:
  pc=work.pop()
  if pc in discovered or not in_ranges(pc,merged): continue
  raw=struct.unpack_from("<I",image,pc-base)[0]; discovered.add(pc)
  succ,is_call=successors(pc,raw)
  if succ is None:
   indirect.append({"pc":pc,"register":(raw>>21)&31,"kind":"return" if ((raw>>21)&31)==31 else "callback"})
   if in_ranges(pc+4,merged): delay_pairs.append((pc,pc+4)); work.append(pc+4)
   continue
  if is_call:
   calls.append({"source":pc,"continuation":pc+8}); succ.add(pc+8)
  # Delay slots are exact and indivisible for every control transfer.
  if (raw>>26 in (1,2,3,4,5,6,7,16,17,18) or (raw>>26==0 and (raw&63) in (8,9))) and in_ranges(pc+4,merged):
   delay_pairs.append((pc,pc+4))
   if pc+4 not in discovered: work.append(pc+4)
  direct[pc]=set(succ)
  work.extend(t for t in succ if in_ranges(t,merged))
 continuations=sorted({c["continuation"] for c in calls if in_ranges(c["continuation"],merged)})
 # A delay-slot instruction does not contribute its decoded sequential edge;
 # control is owned exclusively by the preceding transfer.
 for _,slot in delay_pairs: direct.pop(slot,None)
 cfi=[]; rejected=[]
 for site in indirect:
  if site["kind"]=="return" and continuations:
   site["finite_targets"]=continuations; site["unknown_target_action"]="reject"; cfi.append(site)
  else: rejected.append(site["pc"])
 safe=set(discovered)-set(rejected); changed=True
 while changed:
  changed=False
  for pc,targets in direct.items():
   if pc in safe and any(t not in safe for t in targets): safe.remove(pc); changed=True
  for owner,slot in delay_pairs:
   if (owner in safe)!=(slot in safe):
    safe.discard(owner); safe.discard(slot); changed=True
 for site in cfi:
  site["finite_targets"]=[pc for pc in site["finite_targets"] if pc in safe]
 cfi=[site for site in cfi if site["finite_targets"]]
 # Keep roots only when their complete direct closure survived.
 surviving_roots=sorted(roots & safe); closed=ranges(safe)
 return {"schema":SCHEMA,"status":"partial-fail-closed","resident_identity_sha256":identity,"load_start":base,"load_end_exclusive":base+len(image),
  "source_slices":slices,"source_slice_count":len(slices),"root_count":len(roots),"surviving_root_count":len(surviving_roots),"roots":surviving_roots,
  "authorized_byte_ranges":[{"start":a,"end_exclusive":b} for a,b in merged],"discovered_instruction_count":len(discovered),"compiled_instruction_count":len(safe),
  "compiled_region_count":len(closed),"excluded_instruction_count":len(discovered-safe),"ranges":[{"start":a,"end_exclusive":b} for a,b in closed],
  "delay_slot_pairs":[{"owner":a,"delay_slot":b} for a,b in sorted(set(delay_pairs)) if a in safe and b in safe],
  "finite_return_cfi":cfi,"unresolved_indirect_sites":[{"pc":p,"action":"exclude-source-and-reverse-direct-closure"} for p in sorted(rejected)],
  "policy":{"byte_authority":"union-of-exact-frontier-slices","dormant_or_unknown_ranges":"excluded","unknown_identity_action":"reject","unknown_pc_action":"reject","unknown_indirect_target_action":"reject","runtime_decode_or_compile":False}}
def manifest(receipt):
 lines=[f"schema={MANIFEST_SCHEMA}","image_kind=raw-payload",f"variant_sha256={receipt['resident_identity_sha256']}",f"image_sha256={receipt['resident_identity_sha256']}",f"load_start=0x{receipt['load_start']:08X}",f"load_end_exclusive=0x{receipt['load_end_exclusive']:08X}"]
 entries=sorted(pc for item in receipt["ranges"] for pc in range(item["start"],item["end_exclusive"],4))
 lines += [f"entry=0x{x:08X}" for x in entries]; lines += [f"code=0x{x['start']:08X}-0x{x['end_exclusive']:08X}" for x in receipt["ranges"]]; return "\n".join(lines)+"\n"
def epoch_variants(items):
 out={}
 for manifest_path,blob_path in items:
  data=parse_manifest(manifest_path); blob=blob_path.read_bytes(); start=int(data["load_start"][0],0); end=int(data["load_end_exclusive"][0],0); identity=data["image_sha256"][0]
  if len(blob)!=end-start or sha(blob)!=identity: raise ShellShardError(f"epoch slice identity mismatch: {manifest_path}")
  key=(identity,start,end); row=out.setdefault(key,{"identity_sha256":identity,"load_start":start,"load_end_exclusive":end,"roots":set(),"ranges":set(),"source_manifests":set(),"unknown_pc_action":"reject","unknown_identity_action":"reject"})
  row["roots"].update(int(x,0) for x in data.get("entry",[])); row["ranges"].update(tuple(int(x,0) for x in v.split("-",1)) for v in data.get("code",[])); row["source_manifests"].add(manifest_path.name)
 result=[]
 for row in out.values():
  row["roots"]=sorted(row["roots"]); row["ranges"]=[{"start":a,"end_exclusive":b} for a,b in sorted(row["ranges"])]; row["source_manifests"]=sorted(row["source_manifests"]); result.append(row)
 return sorted(result,key=lambda x:(x["load_start"],x["identity_sha256"]))
def incremental_variants(receipt,baseline,max_entries):
 if baseline.get("schema")!=SCHEMA or baseline.get("resident_identity_sha256")!=receipt["resident_identity_sha256"]: raise ShellShardError("incremental baseline identity/schema mismatch")
 current={pc for x in receipt["ranges"] for pc in range(x["start"],x["end_exclusive"],4)}; prior={pc for x in baseline["ranges"] for pc in range(x["start"],x["end_exclusive"],4)}; delta=current-prior
 # Never split architectural delay ownership across registry variants.
 for pair in receipt["delay_slot_pairs"]:
  a,b=pair["owner"],pair["delay_slot"]
  if (a in delta)!=(b in delta): delta.discard(a); delta.discard(b)
 chunks=[]; chunk=[]
 for pc in sorted(delta):
  if chunk and (pc!=chunk[-1]+4 or len(chunk)>=max_entries): chunks.append(chunk); chunk=[]
  chunk.append(pc)
 if chunk: chunks.append(chunk)
 seen=set(); variants=[]
 for index,pcs in enumerate(chunks,1):
  if seen.intersection(pcs): raise ShellShardError("incremental variants duplicate PCs")
  seen.update(pcs); variants.append({"name":f"bios-shell-frontier84-increment-{index:03d}","entries":pcs,"ranges":[{"start":a,"end_exclusive":b} for a,b in ranges(set(pcs))],"instruction_count":len(pcs),"unknown_pc_action":"reject"})
 return variants
def variant_manifest(receipt,variant):
 view=dict(receipt); view["ranges"]=variant["ranges"]; return manifest(view)
def main(argv=None):
 p=argparse.ArgumentParser(); p.add_argument("--image",type=Path,required=True); p.add_argument("--identity",required=True); p.add_argument("--load-start",type=lambda x:int(x,0),required=True); p.add_argument("--frontier-manifest",type=Path,action="append",required=True); p.add_argument("--epoch-frontier",action="append",default=[],metavar="MANIFEST=BIN"); p.add_argument("--baseline-receipt",type=Path); p.add_argument("--incremental-manifest-dir",type=Path); p.add_argument("--max-increment-entries",type=int,default=256); p.add_argument("--receipt",type=Path,required=True); p.add_argument("--manifest-out",type=Path,required=True); a=p.parse_args(argv)
 try:
  image=a.image.read_bytes()
  if sha(image)!=a.identity: raise ShellShardError("resident image identity mismatch")
  if not 1<=a.max_increment_entries<=1024: raise ShellShardError("invalid incremental manifest bound")
  parsed=[(x,parse_manifest(x)) for x in a.frontier_manifest]; result=build(image,a.load_start,parsed); result["exact_epoch_variants"]=epoch_variants([(Path(x.split("=",1)[0]),Path(x.split("=",1)[1])) for x in a.epoch_frontier]); text=manifest(result); result["compiler_manifest_sha256"]=sha(text.encode())
  if a.baseline_receipt:
   baseline=json.loads(a.baseline_receipt.read_text(encoding="utf-8")); variants=incremental_variants(result,baseline,a.max_increment_entries); result["incremental_variants"]=[]
   if a.incremental_manifest_dir is None: raise ShellShardError("incremental manifest directory is required with baseline")
   a.incremental_manifest_dir.mkdir(parents=True,exist_ok=True)
   for variant in variants:
    path=a.incremental_manifest_dir/(variant["name"]+".manifest"); payload=variant_manifest(result,variant)
    if path.exists(): raise ShellShardError(f"refusing to overwrite {path}")
    path.write_text(payload,encoding="utf-8",newline="\n"); row={k:v for k,v in variant.items() if k!="entries"}; row.update(manifest=path.name,manifest_sha256=sha(payload.encode())); result["incremental_variants"].append(row)
   result["incremental_instruction_count"]=sum(x["instruction_count"] for x in result["incremental_variants"])
   result["incremental_registry_contract"]={"baseline_receipt_sha256":baseline.get("receipt_sha256"),"maximum_entries_per_manifest":a.max_increment_entries,"duplicate_pc_count":0,"selection_key":"authenticated-resident-identity-plus-exact-pc","unknown_pc_action":"reject"}
  result["receipt_sha256"]=sha(canonical(result))
  for path,payload in ((a.manifest_out,text),(a.receipt,json.dumps(result,sort_keys=True,separators=(",",":"))+"\n")):
   if path.exists(): raise ShellShardError(f"refusing to overwrite {path}")
   path.parent.mkdir(parents=True,exist_ok=True); path.write_text(payload,encoding="utf-8",newline="\n")
  return 0
 except (OSError,KeyError,ValueError,ShellShardError) as e: sys.stderr.write(f"BIOS shell shard build failed: {e}\n"); return 2
if __name__=="__main__": raise SystemExit(main())
