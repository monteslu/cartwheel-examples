#!/usr/bin/env python3
"""Alternating native GL benchmark; run from cartwheel-examples.

Configurable deterministic frame range, 1080p High, no adaptive quality.
Does not stop other applications. Samples load/CPU/GPU utilization and records
actual renderer identity. GPU timers are intentionally off in these timed runs.
"""
import os, subprocess, json, time, pathlib, threading, argparse, hashlib
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--exe", default="build/whimsy-native/project/games/bikemesa/bikemesa")
parser.add_argument("--out", default="out/neon-benchmark")
parser.add_argument("--pairs", type=int, default=3)
parser.add_argument("--comparison", choices=("sort", "culling", "current"), default="sort")
parser.add_argument("--track", type=int, default=3, help="1-based BikeMesa track number")
parser.add_argument("--players", type=int, choices=(1,2,3,4), default=1)
parser.add_argument("--spread-players", action="store_true", help="Only player one pedals; other players remain at the start to exercise distant views")
parser.add_argument("--frames", type=int, default=2050)
parser.add_argument("--samples", type=int, default=1700)
parser.add_argument("--require-gameplay", action="store_true", help="Reject runs that finish the race or leave gameplay; omit when measuring finish effects such as the ball pit")
parser.add_argument("--expect-renderer", help="Required substring of actual GL renderer; reject a run on another GPU")
args=parser.parse_args()
if not 0 < args.samples <= min(args.frames, 2048):parser.error("samples must be positive, <= frames and <= 2048")
if not 1 <= args.track <= 16:parser.error("--track must be 1..16")
if args.comparison == "culling" and args.track != 3:parser.error("city culling comparison only applies to track 3")
if args.spread_players and args.players < 2:parser.error("--spread-players requires --players 2 or more")
if args.pairs < 1:parser.error("--pairs must be positive")
out=pathlib.Path(args.out).resolve();out.mkdir(parents=True,exist_ok=True)
exe=pathlib.Path(args.exe).resolve()
binary_sha256=hashlib.sha256(exe.read_bytes()).hexdigest()
results=[]
variants = ["current"] * (args.pairs * 2) if args.comparison == "current" else [v for pair in range(args.pairs) for v in (("before", "after") if pair%2==0 else ("after", "before"))]
for i,variant in enumerate(variants):
 name=f"{i+1}-{variant}"
 env={k:v for k,v in os.environ.items() if not k.startswith(("CW_", "BIKEMESA_"))}; env.update(CW_GPU="gl", BIKEMESA_START_TRACK=str(args.track), BIKEMESA_AUTORIDE="1", BIKEMESA_BENCH_QUALITY="2", BIKEMESA_RES="1920x1080", CW_RENDER_SCALE="1", CW_FRAMES=str(args.frames), CW_SEED="1", SDL_AUDIODRIVER="dummy", CW_DEBUG_CMDS=f"perf {args.samples};quality;fields bike.pos game.track game.state finished checkpoint cw.gpu.errors cw.three.missing")
 if args.players>1:
  # Exercise real split-screen input instead of the solo autoride shortcut.
  env.pop("BIKEMESA_START_TRACK");env.pop("BIKEMESA_AUTORIDE")
  input_path=out/"pads.txt"
  extra=" ".join(f"{key}={0 if args.spread_players else 1},0,0" for key in ("p","p2","p3")[:args.players-1])
  input_path.write_text(f"0 1 0 0 0 0 0 0 {extra}\n")
  env.update(CW_INPUT=str(input_path),CW_START_CMDS=f"players {args.players};track {args.track}")
  env["CW_DEBUG_CMDS"]+=";players;fields race.x"
 # End-of-run diagnostics do not enter the measured frame window. Keep the
 # persistent-effect workload with the timing evidence for Candy Comet.
 if args.track==15:
  env["CW_DEBUG_CMDS"]+=";chocolate stats;fields chocolate.bubbles chocolate.bubble_hits chocolate.body_hits chocolate.deposits chocolate.splashes"
 if variant=="before":
  env["CW_SORT_ORIGINS" if args.comparison=="sort" else "BIKEMESA_CITY_NO_CULL"]="1"
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
 result={"name":name,"comparison":args.comparison,"exit":r.returncode,"rows":rows,"system":samples,"binary_sha256":binary_sha256,"renderer":[line for line in r.stdout.splitlines() if "[info] GL:" in line],"settings":{k:v for k,v in env.items() if k.startswith(("CW_","BIKEMESA_","DRI_","SDL_"))}};results.append(result)
 result["expected_renderer"]=args.expect_renderer
 result["renderer_matches"]=None if args.expect_renderer is None else any(args.expect_renderer.casefold() in line.casefold() for line in result["renderer"])
 (out/"results.json").write_text(json.dumps(results,indent=2))
 print(name, "renderer:", " | ".join(result["renderer"]) or "UNKNOWN", flush=True)
 if result["renderer_matches"] is False:
  raise SystemExit(f"{name}: renderer mismatch: expected {args.expect_renderer!r}; raw evidence saved, run rejected")
 perf=next((x for x in rows if "thread_cpu" in x),{})
 quality=next((x for x in rows if "quality" in x),{})
 fields=next((x for x in rows if "cw.gpu.errors" in x),{})
 assert "[error]" not in r.stdout and perf.get("draw_calls",0)>0, f"{name}: asset/render initialization failure"
 assert r.returncode==0 and perf.get("frames")==args.samples, f"{name}: incomplete measurement"
 assert quality.get("quality")=="High" and quality.get("locked") and not quality.get("auto"), f"{name}: unexpected quality"
 assert fields.get("cw.gpu.errors")==0 and fields.get("cw.three.missing")==0, f"{name}: render errors"
 if args.require_gameplay:
  assert fields.get("game.state")==3 and fields.get("finished")==0, f"{name}: measurement reached the finish or left gameplay; shorten --frames"
 if args.players>1:
  player_state=next((x for x in rows if "players" in x),{})
  assert player_state.get("players")==args.players, f"{name}: wrong player count"
 print(name, json.dumps({k:perf.get(k) for k in ["frame","thread_cpu","draw_calls","triangles"]}),flush=True)
