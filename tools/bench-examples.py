#!/usr/bin/env python3
"""Native GL current-build or alternating comparison benchmark across the examples.

1200 deterministic frames, last 800 measured, 1080p High, no adaptive quality.
Does not stop other applications. Samples load/CPU/GPU utilization and records
actual renderer identity. GPU timers are intentionally off in these timed runs.
"""
import os, subprocess, json, time, pathlib, threading, argparse, hashlib
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", default="build/whimsy-native")
parser.add_argument("--games", nargs="+", default=["bikemesa","derby","pugball","bomberdude","chromite","spout"])
parser.add_argument("--out", default="out/examples-bounds-benchmark")
parser.add_argument("--comparison", choices=["sort", "crowd", "current"], default="sort")
parser.add_argument("--pairs", type=int, default=2)
parser.add_argument("--expect-renderer", help="Reject measurements from a different GPU")
args=parser.parse_args()
if args.comparison == "crowd" and args.games != ["pugball"]:parser.error("crowd comparison requires --games pugball")
if args.pairs < 1:parser.error("--pairs must be positive")
out=pathlib.Path(args.out).resolve();out.mkdir(parents=True,exist_ok=True)
fixtures={
 "bikemesa":{"BIKEMESA_START_TRACK":"3","BIKEMESA_AUTORIDE":"1"},
 "derby":{"DERBY_BOUT":"1","DERBY_CPU_ONLY":"1"},
 "pugball":{"PUG_STADIUM":"1","PUG_CPU_ONLY":"1"},
 "bomberdude":{"BOMBERDUDE_ARENA":"1","BOMBERDUDE_CPU_ONLY":"1"},
 "chromite":{"CHROMITE_START_LEVEL":"1"},
 "spout":{"SPOUT_START_LEVEL":"1"},
}
for game in args.games:
 if game not in fixtures:parser.error("unknown game: "+game)
jobs=[(game,variant,pair) for pair in range(args.pairs) for game in args.games for variant in (("current","current") if args.comparison=="current" else (("origins","bounds") if pair%2==0 else ("bounds","origins")))]
results=[]
for i,(game,variant,pair) in enumerate(jobs):
 exe=pathlib.Path(args.build,"project/games",game,game).resolve()
 binary_sha256=hashlib.sha256(exe.read_bytes()).hexdigest()
 env={k:v for k,v in os.environ.items() if not k.startswith(("CW_","BIKEMESA_","DERBY_","PUG_","BOMBERDUDE_","CHROMITE_","SPOUT_"))}
 env.update(fixtures[game]);env.update(CW_GPU="gl",CW_RENDER_SCALE="2",CW_FRAMES="1200",CW_SEED="1",SDL_AUDIODRIVER="dummy",CW_START_CMDS="@120 quality high",CW_DEBUG_CMDS="perf 800;quality;fields game.state cw.gpu.errors cw.three.missing")
 if variant=="origins":env["CW_SORT_ORIGINS" if args.comparison == "sort" else "PUG_FULL_CROWD"]="1"
 if args.comparison=="current" and game in ("spout","chromite"):
  pad=out/(game+"-input.txt")
  pad.write_text("0 0 0 -1 0 0 0 0\n" if game=="spout" else "0 1 0 0 0 0 0 0\n")
  env["CW_INPUT"]=str(pad)
 name=f"{i+1}-{game}-{variant}"
 env["CW_SCREENSHOT"]=str(out/(name+".png"))
 samples=[]; done=threading.Event()
 def sample():
  while not done.is_set():
   s={"time":time.time(), "load":os.getloadavg(), "cpu":pathlib.Path("/proc/stat").read_text().splitlines()[0]}
   for p in pathlib.Path("/sys/class/drm").glob("card*/device/gpu_busy_percent"):
    try:s[str(p)]=p.read_text().strip()
    except OSError:pass
   samples.append(s); done.wait(.5)
 t=threading.Thread(target=sample);t.start()
 try:
  r=subprocess.run([str(exe)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=180)
 finally:
  done.set();t.join()
 (out/(name+".log")).write_text(r.stdout)
 rows=[]
 for line in r.stdout.splitlines():
  try: rows.append(json.loads(line))
  except ValueError: pass
 result={"name":name,"exit":r.returncode,"rows":rows,"system":samples,"binary_sha256":binary_sha256,"game":game,"variant":variant,"comparison":args.comparison,"renderer":[line for line in r.stdout.splitlines() if "[info] GL:" in line],"settings":{k:v for k,v in env.items() if k.startswith(("CW_","BIKEMESA_","DERBY_","PUG_","BOMBERDUDE_","CHROMITE_","SPOUT_","DRI_","SDL_"))}};results.append(result)
 result["expected_renderer"]=args.expect_renderer
 result["renderer_matches"]=None if args.expect_renderer is None else any(args.expect_renderer.casefold() in line.casefold() for line in result["renderer"])
 (out/"results.json").write_text(json.dumps(results,indent=2))
 print(name,"renderer:"," | ".join(result["renderer"]) or "UNKNOWN",flush=True)
 if result["renderer_matches"] is False:raise SystemExit(f"{name}: wrong GPU; measurement rejected")
 perf=next((x for x in rows if "thread_cpu" in x),{})
 quality=next((x for x in rows if "quality" in x),{})
 fields=next((x for x in rows if "cw.gpu.errors" in x),{})
 assert r.returncode==0 and perf.get("frames")==800, f"{name}: incomplete measurement"
 assert quality.get("quality")=="High" and not quality.get("auto"), f"{name}: unexpected quality"
 assert fields.get("cw.gpu.errors")==0 and fields.get("cw.three.missing")==0, f"{name}: render errors"
 assert fields.get("game.state") not in (None,0), f"{name}: fixture remained in title state"
 if args.comparison=="current" and game!="bikemesa":
  expected_state={"derby":1,"pugball":3,"bomberdude":4,"chromite":1,"spout":2}[game]
  assert fields.get("game.state")==expected_state, f"{name}: fixture left gameplay"
 print(name, json.dumps({k:perf.get(k) for k in ["frame","thread_cpu","draw_calls","triangles"]}),flush=True)
