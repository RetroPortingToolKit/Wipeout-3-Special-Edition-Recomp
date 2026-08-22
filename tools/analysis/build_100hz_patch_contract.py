#!/usr/bin/env python3
"""Build an identity-bound, fail-closed static 100 Hz patch authorization receipt."""
from __future__ import annotations
import argparse, hashlib, json, math, sys
from pathlib import Path
from typing import Any

SCHEMA="wo3se.static-100hz-patch-authorization.v1"; CPU_HZ=33_868_800; SPU_HZ=44_100; UPDATE_HZ=100; CD_HZ=75
class ContractError(RuntimeError): pass
def sha(data: bytes)->str: return hashlib.sha256(data).hexdigest()
def canonical(obj: Any)->bytes: return (json.dumps(obj,sort_keys=True,separators=(",",":"))+"\n").encode()
def normalize_ranges(raw,start,end):
    out=[]
    for item in raw:
        pair=(int(item["start"]),int(item["end_exclusive"]))
        if pair[0]%4 or pair[1]%4 or not(start<=pair[0]<pair[1]<=end) or (out and pair[0]<out[-1][1]):
            raise ContractError("generated ranges overlap, are unaligned, unsorted, or out of bounds")
        out.append(pair)
    return out
def diff_receipt(name,source,target,base,ranges):
    if not source or len(source)!=len(target) or len(source)%4: raise ContractError(f"{name} images must be equal, non-empty, word-aligned payloads")
    words=[]; changed_bytes=authorized=0
    for offset in range(0,len(source),4):
        old,new=source[offset:offset+4],target[offset:offset+4]
        if old==new: continue
        mask=sum(1<<i for i in range(4) if old[i]!=new[i]); changed_bytes+=mask.bit_count(); pc=base+offset
        classification="generated-instruction" if any(lo<=pc<hi for lo,hi in ranges) else "not-authorized-as-instruction"
        authorized+=classification=="generated-instruction"
        words.append((offset,mask,classification))
    runs=[]
    for offset,mask,classification in words:
        if runs and offset==runs[-1]["end_offset_exclusive"] and classification==runs[-1]["classification"]:
            run=runs[-1]; run["end_offset_exclusive"]+=4; run["word_count"]+=1; run["changed_byte_count"]+=mask.bit_count(); run["changed_byte_masks"].append(mask)
        else:
            runs.append({"start_offset":offset,"end_offset_exclusive":offset+4,"guest_start":base+offset,"word_count":1,"changed_byte_count":mask.bit_count(),"classification":classification,"changed_byte_masks":[mask]})
    for run in runs:
        lo,hi=run["start_offset"],run["end_offset_exclusive"]; masks=bytes(run.pop("changed_byte_masks"))
        run["changed_byte_masks_sha256"]=sha(masks); run["source_span_sha256"]=sha(source[lo:hi]); run["target_span_sha256"]=sha(target[lo:hi])
    return {"name":name,"load_start":base,"image_bytes":len(source),"source_sha256":sha(source),"target_sha256":sha(target),
            "changed_byte_count":changed_bytes,"changed_word_count":len(words),"generated_instruction_word_count":authorized,
            "not_authorized_as_instruction_word_count":len(words)-authorized,"changed_word_run_count":len(runs),"changed_word_runs_sha256":sha(canonical(runs)),"changed_word_runs":runs,
            "classification_policy":"Only PCs in authenticated generated ranges are instructions; every other changed word remains unclassified payload."}
def build(config,authority,blobs):
    if config.get("schema")!="wo3se.static-100hz-patch-input.v1" or authority.get("schema")!="wo3se.generated-game-shards.v1": raise ContractError("unsupported schema")
    by_name={i["name"]:i for i in authority.get("images",[])}; mutations=[]
    for spec in config["mutations"]:
        image=by_name.get(spec["generated_image"])
        if image is None or image["identity_sha256"]!=spec["target_sha256"]: raise ContractError(f"{spec['name']} target is not the generated authenticated identity")
        ranges=normalize_ranges(image["ranges"],int(image["load_start"]),int(image["load_end_exclusive"]))
        result=diff_receipt(spec["name"],blobs[spec["source_key"]],blobs[spec["target_key"]],int(image["load_start"]),ranges)
        if result["source_sha256"]!=spec["source_sha256"] or result["target_sha256"]!=spec["target_sha256"]: raise ContractError(f"{spec['name']} configured identity mismatch")
        mutations.append(result)
    mult=config.get("cpu_retirement_multiplier"); cpu={"status":"unresolved","independent_runtime_control":True,"native_cpu_cycles_per_100hz_tick":CPU_HZ//UPDATE_HZ}
    if mult is not None:
        n,d=int(mult["numerator"]),int(mult["denominator"])
        if n<=0 or d<=0 or math.gcd(n,d)!=1 or n>10*d: raise ContractError("CPU retirement multiplier must be reduced, positive, and at most 10x")
        cpu.update(status="selected",numerator=n,denominator=d,retirement_numerator_per_tick=(CPU_HZ//UPDATE_HZ)*n,retirement_denominator=d)
    body={"schema":SCHEMA,"status":"authorized" if cpu["status"]=="selected" else "incomplete-fail-closed",
          "generated_authority_sha256":sha(canonical(authority)),"mutations":mutations,"cpu_retirement":cpu,
          "native_device_clocks":{"update_hz":UPDATE_HZ,"spu_hz":SPU_HZ,"spu_samples_per_tick":SPU_HZ//UPDATE_HZ,"cd_sectors_per_second":CD_HZ,"cd_sector_schedule_first_four_ticks":[0,1,1,1],"cpu_multiplier_applies_to_spu":False,"cpu_multiplier_applies_to_cd":False},
          "claims":{"static_byte_and_word_diff_complete":True,"generated_instruction_binding_complete":True,"unique_gameplay_updates_proven":False,"gameplay_update_addresses_authorized":[]}}
    body["receipt_sha256"]=sha(canonical(body)); return body
def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__); p.add_argument("--config",type=Path,required=True); p.add_argument("--authority",type=Path,required=True); p.add_argument("--blob",action="append",default=[]); p.add_argument("--json-out",type=Path,required=True); a=p.parse_args(argv)
    try:
        config=json.loads(a.config.read_text()); authority=json.loads(a.authority.read_text()); paths=dict(x.split("=",1) for x in a.blob)
        expected={m["source_key"]:m["source_sha256"] for m in config["mutations"]}; expected.update({m["target_key"]:m["target_sha256"] for m in config["mutations"]})
        if set(paths)!=set(expected): raise ContractError("blob keys do not exactly match configured mutation inputs")
        blobs={k:Path(v).read_bytes() for k,v in paths.items()}
        for k,data in blobs.items():
            if sha(data)!=expected[k]: raise ContractError(f"identity mismatch for {paths[k]}")
        if a.json_out.exists(): raise ContractError(f"refusing to overwrite output: {a.json_out}")
        a.json_out.parent.mkdir(parents=True,exist_ok=True); a.json_out.write_text(json.dumps(build(config,authority,blobs),indent=2,sort_keys=True)+"\n",encoding="utf-8",newline="\n"); return 0
    except (OSError,ValueError,KeyError,json.JSONDecodeError,ContractError) as e: sys.stderr.write(f"100 Hz patch contract failed: {e}\n"); return 2
if __name__=="__main__": raise SystemExit(main())
