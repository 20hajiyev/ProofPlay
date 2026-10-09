"""P6 gate: forged sessions must fail replay verification; the genuine one must pass.

    python -I proofplay/tests/tamper_test.py <genuine_session.json>

Writes proofplay/results/tamper.json and prints a table.
"""
import copy
import json
import pathlib
import random
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = pathlib.Path(__file__).resolve().parents[2]
EXE = ROOT / "build/game/proofplay/Release/proofplay.exe"


def variants(s):
    n = len(s["inputs"])
    mid = n // 2
    out = [("genuine (control)", copy.deepcopy(s), False)]

    def v(name, fn):
        t = copy.deepcopy(s)
        fn(t)
        out.append((name, t, True))

    v("claim 5 s faster time", lambda t: t["end"].__setitem__("player_time_s", t["end"]["player_time_s"] - 5.0))
    v("claim 1st place", lambda t: t["end"].__setitem__("player_position", 1))
    v("random state hash", lambda t: t["end"].__setitem__("hash", str(random.getrandbits(63))))
    v("one steering input flipped mid-race", lambda t: t["inputs"][mid].__setitem__(2, -t["inputs"][mid][2] or 1.0))

    def boost(t):
        # A braking zone: replacing braking with full throttle must change the race (a window
        # that is already full throttle would be a no-op "forgery").
        start = next(i for i in range(mid, n) if t["inputs"][i][1] > 0.2)
        for i in range(start, min(n, start + 240)):
            t["inputs"][i][0], t["inputs"][i][1] = 1.0, 0.0
    v("braking replaced by 2 s of full throttle", boost)
    v("easier opponents in the grid", lambda t: t["setup"].__setitem__("grid", ["D01"] * len(t["setup"]["grid"])))
    v("field difficulty Easy instead of Normal", lambda t: t["setup"].__setitem__("difficulty", "Easy"))
    v("last 1000 steps cut off", lambda t: t.__setitem__("inputs", t["inputs"][:-1000]))
    v("one lap fewer", lambda t: t["setup"].__setitem__("laps", t["setup"]["laps"] - 1))
    v("performance part added", lambda t: t["setup"]["player_parts"].__setitem__("perf_engine", "stage2"))
    return out


def check(item, tmp):
    name, session, forged = item
    path = pathlib.Path(tmp) / (str(abs(hash(name))) + ".json")
    path.write_text(json.dumps(session), encoding="utf-8")
    proc = subprocess.run([str(EXE), "replay", str(path)], capture_output=True, text=True)
    accepted = proc.returncode == 0
    detail = proc.stdout.strip().splitlines()[-1] if proc.stdout.strip() else proc.stderr.strip()
    return {"case": name, "forged": forged, "accepted": accepted, "correct": accepted != forged, "detail": detail}


def main():
    random.seed(7)
    genuine = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory() as tmp, ThreadPoolExecutor(4) as pool:
        rows = list(pool.map(lambda it: check(it, tmp), variants(genuine)))
    forged = [r for r in rows if r["forged"]]
    rejected = sum(not r["accepted"] for r in forged)
    for r in rows:
        print(f"{'OK ' if r['correct'] else 'BAD'} {'forged ' if r['forged'] else 'genuine'} {'accepted' if r['accepted'] else 'rejected'}  {r['case']}")
    print(f"forged rejected: {rejected}/{len(forged)}; genuine accepted: {not rows[0]['forged'] and rows[0]['accepted']}")
    out = ROOT / "proofplay/results/tamper.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({"source": sys.argv[1], "rejected": rejected, "forged": len(forged), "rows": rows}, indent=1), encoding="utf-8")
    sys.exit(0 if all(r["correct"] for r in rows) else 1)


if __name__ == "__main__":
    main()
