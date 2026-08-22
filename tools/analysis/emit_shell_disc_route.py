#!/usr/bin/env python3
"""Emit deterministic compiler manifests and finite CFI from a verified shell route."""
from __future__ import annotations
import argparse,hashlib,json,struct,sys
from pathlib import Path

SCHEMA="clean-aot-shell-disc-route-static-v1"
class EmitError(ValueError): pass
def sha(b): return hashlib.sha256(b).hexdigest()
def num(x): return x if isinstance(x,int) else int(x,0)
def canonical(x): return (json.dumps(x,sort_keys=True,separators=(",",":"))+"\n").encode()
def merge(ranges):
 out=[]
 for a,b in sorted(set(ranges)):
  if a%4 or b%4 or b<=a: raise EmitError("unaligned or empty route range")
  if out and a<=out[-1][1]: out[-1]=(out[-1][0],max(out[-1][1],b))
  else: out.append((a,b))
 return out
def control(word):
 op=word>>26; return op in {1,2,3,4,5,6,7} or (op==18 and ((word>>21)&31)==8) or (op==0 and (word&63) in {8,9})
def build(image,route,max_entries=256):
 if route.get("schema")!="clean-aot-shell-disc-route-receipt-v1": raise EmitError("unsupported route receipt")
 resident=route["resident_image"]; base=num(resident["load_start"])
 if len(image)!=resident["length"] or sha(image)!=resident["sha256"]: raise EmitError("resident image mismatch")
 goal=num(route["goal_call_site"]); start=num(route["shell_init_entry"]); ranges=[(start,goal+8)]
 calls=[]
 for x in route["direct_calls"]:
  pc,target=num(x["pc"]),num(x["target"]); raw=struct.unpack_from("<I",image,target-base)[0]; ranges += [(pc,pc+8),(target,target+(8 if control(raw) else 4))]; calls.append((pc,target))
 branches=[]
 for x in route["branches"]:
  pc,target=num(x["pc"]),num(x["target"]); ranges.append((pc,pc+8)); branches.append({"pc":pc,"encoded_target":target,"prerequisite":x["prerequisite"],"unknown_outcome_action":"reject"})
 returns=[]
 for x in route["finite_returns"]:
  pc=num(x["pc"]); targets=[num(v) for v in x["finite_targets"]]
  if not targets or len(targets)!=len(set(targets)): raise EmitError("return CFI universe is empty or duplicated")
  ranges.append((pc,pc+8)); returns.append({"pc":pc,"callee":num(x["callee"]),"finite_targets":targets,"unknown_target_action":"reject","selection":"live-ra-only"})
 loops=[]
 for x in route["loops"]:
  a,b=num(x["start"]),num(x["end_exclusive"]); payload=image[a-base:b-base]
  if len(payload)!=b-a or sha(payload)!=x["sha256"] or not 1<=x["bound"]<=1024: raise EmitError("loop identity or bound mismatch")
  ranges.append((a,b)); loops.append({"name":x["name"],"start":a,"end_exclusive":b,"bound":x["bound"],"sha256":x["sha256"],"bound_exhaustion_action":"reject"})
 merged=merge(ranges); pcs=[pc for a,b in merged for pc in range(a,b,4)]; owners={a for a,b in calls}|{x["pc"] for x in branches}|{x["pc"] for x in returns}
 chunks=[]; cursor=0
 while cursor<len(pcs):
  end=min(cursor+max_entries,len(pcs))
  if end<len(pcs) and pcs[end-1] in owners and pcs[end]==pcs[end-1]+4: end-=1
  if end==cursor: raise EmitError("manifest bound cannot preserve a delay slot")
  chunks.append(pcs[cursor:end]);cursor=end
 variants=[]
 for index,chunk in enumerate(chunks,1):
  cr=merge((pc,pc+4) for pc in chunk)
  variants.append({"name":f"bios-shell-disc-route-{index:03d}","entries":chunk,"ranges":[{"start":a,"end_exclusive":b} for a,b in cr],"instruction_count":len(chunk)})
 if sum(v["instruction_count"] for v in variants)!=len(set(pcs)): raise EmitError("route manifests duplicate PCs")
 return {"schema":SCHEMA,"status":"bounded-fail-closed","resident_identity_sha256":resident["sha256"],"load_start":base,"load_end_exclusive":base+len(image),"goal_call_site":goal,"goal_target":num(route["direct_calls"][-1]["target"]),"authorized_instruction_count":len(pcs),"variants":variants,"direct_calls":[{"pc":a,"target":b} for a,b in calls],"branch_prerequisites":branches,"finite_return_cfi":returns,"bounded_loops":loops,"policy":{"post_goal_status":"excluded","unknown_pc_action":"reject","unknown_branch_outcome_action":"reject","unknown_return_target_action":"reject","runtime_decode":False}}
def manifest(result,variant):
 lines=["schema=clean-aot-compiler-manifest-v1","image_kind=raw-payload",f"variant_sha256={result['resident_identity_sha256']}",f"image_sha256={result['resident_identity_sha256']}",f"load_start=0x{result['load_start']:08X}",f"load_end_exclusive=0x{result['load_end_exclusive']:08X}"]
 lines += [f"entry=0x{x:08X}" for x in variant["entries"]]; lines += [f"code=0x{x['start']:08X}-0x{x['end_exclusive']:08X}" for x in variant["ranges"]]; return "\n".join(lines)+"\n"
def main(argv=None):
 p=argparse.ArgumentParser();p.add_argument("--image",type=Path,required=True);p.add_argument("--route",type=Path,required=True);p.add_argument("--manifest-dir",type=Path,required=True);p.add_argument("--receipt",type=Path,required=True);p.add_argument("--cfi-output",type=Path,required=True);p.add_argument("--max-entries",type=int,default=256);a=p.parse_args(argv)
 try:
  if not 1<=a.max_entries<=1024: raise EmitError("invalid manifest bound")
  route_raw=a.route.read_bytes();result=build(a.image.read_bytes(),json.loads(route_raw),a.max_entries);result["route_receipt_sha256"]=sha(route_raw);a.manifest_dir.mkdir(parents=True,exist_ok=True)
  for v in result["variants"]:
   path=a.manifest_dir/(v["name"]+".manifest");text=manifest(result,v)
   if path.exists(): raise EmitError(f"refusing to overwrite {path}")
   path.write_text(text,encoding="utf-8",newline="\n");v["manifest"]=path.name;v["manifest_sha256"]=sha(text.encode());del v["entries"]
  cfi={"schema":"clean-aot-shell-disc-route-cfi-v1","resident_identity_sha256":result["resident_identity_sha256"],"route_receipt_sha256":result["route_receipt_sha256"],"finite_returns":result["finite_return_cfi"],"branch_prerequisites":result["branch_prerequisites"],"bounded_loops":result["bounded_loops"],"unknown_target_action":"reject"};cfi["receipt_sha256"]=sha(canonical(cfi));a.cfi_output.write_bytes(canonical(cfi));result["cfi_receipt_sha256"]=cfi["receipt_sha256"];result["receipt_sha256"]=sha(canonical(result));a.receipt.write_bytes(canonical(result));return 0
 except (OSError,KeyError,json.JSONDecodeError,EmitError) as e: sys.stderr.write(f"shell route emit failed: {e}\n");return 2
if __name__=="__main__":raise SystemExit(main())
