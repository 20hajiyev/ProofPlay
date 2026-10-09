"""P7: compare coaches by re-simulating every piece of advice they give.

    python -I proofplay/bench/benchmark.py [--workers N] [--llm-workers 6]

Dataset: Normal bots on S01 with one planted braking flaw each (known corner, known fix
direction) plus clean Normal/Hard bots. Coaches:
  rule      - the classic stats heuristic: slow at the apex -> brake later, else brake earlier
  llm       - an LLM reading the same stats (llm_coach.py), if credentials are set
  proofplay - our proven fixes
Writes proofplay/results/benchmark.json and benchmark.md.
"""
import argparse
import json
import os
import pathlib
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import llm_coach  # noqa: E402

ROOT = HERE.parents[1]
EXE = ROOT / "build/game/proofplay/Release/proofplay.exe"
DATA = ROOT / "proofplay/bench/data"
RESULTS = ROOT / "proofplay/results"
APEX = {1: 1000.0, 2: 1248.0, 3: 2188.0}  # S01 corner apexes (route metres), from FindCorners


def dataset():
    rows = []
    for corner, apex in APEX.items():
        for m in (25, 40):
            # early_brake: brakes hard before the braking point -> the fix is "brake later"
            rows.append({"name": f"c{corner}_early{m}", "flaw": f"early_brake:{apex - 45:.0f}:{m}", "corner": corner, "fix": "brake_later", "bot": "Normal"})
        for m in (30, 45):
            # late_brake: keeps full throttle into the braking zone -> the fix is "brake earlier"
            rows.append({"name": f"c{corner}_late{m}", "flaw": f"late_brake:{apex - 20:.0f}:{m}", "corner": corner, "fix": "brake_earlier", "bot": "Normal"})
    rows.append({"name": "clean_normal", "flaw": "", "corner": None, "fix": None, "bot": "Normal"})
    rows.append({"name": "clean_hard", "flaw": "", "corner": None, "fix": None, "bot": "Hard"})
    return rows


def run(*args):
    return subprocess.run([str(EXE), *map(str, args)], capture_output=True, text=True)


def record_and_analyze(row):
    session, report = DATA / f"{row['name']}.json", DATA / f"{row['name']}_report.json"
    if not session.exists():
        args = ["record", "--out", session, "--bot", row["bot"], "--player", row["name"]]
        if row["flaw"]:
            args += ["--flaw", row["flaw"]]
        run(*args)
    if not report.exists():
        run("analyze", session, "--out", report)
    return json.loads(report.read_text(encoding="utf-8"))


def rule_coach(report):
    passes = []
    for c in report["corners"]:
        for p in c["passes"]:
            passes.append((p["loss_s"], c, p))
    passes.sort(key=lambda x: -x[0])
    out, seen = [], set()
    for loss, c, p in passes:
        if c["id"] in seen:
            continue
        seen.add(c["id"])
        kind = "brake_later" if p["min_speed"] < 0.9 * c["ideal_speed"] else "brake_earlier"
        out.append({"corner": c["id"], "lap": p["lap"], "kind": kind, "amount_m": 15})
        if len(out) == 3:
            break
    return out


def proofplay_coach(report):
    return [{"corner": p["corner"], "lap": p["lap"], "kind": p["kind"], "amount_m": p["amount_m"]} for p in report["proofs"]]


def evaluate(row, coach, advice):
    if not advice:
        return []
    adv = DATA / f"{row['name']}_{coach}_advice.json"
    out = DATA / f"{row['name']}_{coach}_eval.json"
    adv.write_text(json.dumps(advice), encoding="utf-8")
    run("evaluate", DATA / f"{row['name']}.json", "--advice", adv, "--out", out)
    return json.loads(out.read_text(encoding="utf-8"))


def summarize(results, coaches):
    table = {}
    for coach in coaches:
        items = [a for r in results for a in r["coaches"].get(coach, [])]
        tested = [a for a in items if a.get("status") != "not_testable"]
        flawed = [r for r in results if r["row"]["corner"]]
        first_hit = sum(1 for r in flawed if r["coaches"].get(coach) and r["coaches"][coach][0]["corner"] == r["row"]["corner"])
        fix_right = sum(1 for r in flawed for a in r["coaches"].get(coach, [])[:1]
                        if a["corner"] == r["row"]["corner"] and a["kind"] == r["row"]["fix"])
        table[coach] = {
            "advice": len(items),
            "tested": len(tested),
            "works": sum(a["status"] == "works" for a in tested),
            "no_effect": sum(a["status"] == "no_effect" for a in tested),
            "worse": sum(a["status"] == "worse" for a in tested),
            "crash": sum(a["status"] == "crash" for a in tested),
            "works_pct": round(100 * sum(a["status"] == "works" for a in tested) / max(1, len(tested)), 1),
            "mean_gain_s": round(sum(a.get("gain_s", 0) for a in tested) / max(1, len(tested)), 3),
            "planted_flaw_first": f"{first_hit}/{len(flawed)}",
            "planted_fix_direction_first": f"{fix_right}/{len(flawed)}",
        }
    return table


def main():
    ap = argparse.ArgumentParser()
    # ~40 MB per proofplay process (D-094); 6 keeps the machine usable while a benchmark runs
    # (14 parallel runs measured ~0.5 GB and saturated the CPU). Raise with --workers on a free box.
    ap.add_argument("--workers", type=int, default=min(6, max(2, (os.cpu_count() or 4) - 2)))
    ap.add_argument("--llm-workers", type=int, default=6)
    args = ap.parse_args()
    DATA.mkdir(parents=True, exist_ok=True)
    RESULTS.mkdir(parents=True, exist_ok=True)
    rows = dataset()
    t0 = time.time()
    with ThreadPoolExecutor(args.workers) as pool:
        reports = list(pool.map(record_and_analyze, rows))
    print(f"recorded + analysed {len(rows)} sessions in {time.time() - t0:.0f} s")

    coaches = ["rule", "proofplay"] + (["llm"] if llm_coach.available() else [])
    if "llm" not in coaches:
        print("llm coach skipped: set PROOFPLAY_AOAI_ENDPOINT/KEY/DEPLOYMENT (proofplay/llm.env) or Foundry/Claude credentials")
    usage = []
    advice = [{"rule": rule_coach(rep), "proofplay": proofplay_coach(rep)} for rep in reports]
    if "llm" in coaches:
        # LLM calls are network-bound: run them in parallel, and keep each answer on disk so a
        # rerun neither waits nor pays again (delete *_llm_cached.json to ask afresh).
        def ask(i):
            cached = DATA / f"{rows[i]['name']}_llm_cached.json"
            if cached.exists():
                return json.loads(cached.read_text(encoding="utf-8"))
            try:
                a = llm_coach.advise(reports[i], usage)
            except Exception as e:  # one failed call should not lose the whole run
                print("llm failed for", rows[i]["name"], e)
                return []
            cached.write_text(json.dumps(a), encoding="utf-8")
            print(f"  llm {rows[i]['name']}: {len(a)} advice ({time.time() - t0:.0f} s)")
            return a
        t1 = time.time()
        with ThreadPoolExecutor(args.llm_workers) as pool:
            for i, a in enumerate(pool.map(ask, range(len(rows)))):
                advice[i]["llm"] = a
        print(f"llm coach: {len(rows)} races in {time.time() - t1:.0f} s")

    jobs = [(i, coach) for i in range(len(rows)) for coach in coaches]
    t2 = time.time()
    with ThreadPoolExecutor(args.workers) as pool:
        evals = list(pool.map(lambda j: evaluate(rows[j[0]], j[1], advice[j[0]][j[1]]), jobs))
    print(f"evaluated {len(jobs)} advice sets by re-simulation in {time.time() - t2:.0f} s")
    results = [{"row": r, "coaches": {}} for r in rows]
    for (i, coach), ev in zip(jobs, evals):
        results[i]["coaches"][coach] = ev

    table = summarize(results, coaches)
    out = {"date": time.strftime("%Y-%m-%d %H:%M"), "sessions": len(rows), "summary": table, "llm_usage": usage, "results": results}
    (RESULTS / "benchmark.json").write_text(json.dumps(out, indent=1), encoding="utf-8")
    lines = ["| Coach | Advice tested | Works | No effect | Worse | Crash | Works % | Mean gain s | Planted corner #1 | Right fix #1 |",
             "|---|---|---|---|---|---|---|---|---|---|"]
    for coach, s in table.items():
        lines.append(f"| {coach} | {s['tested']} | {s['works']} | {s['no_effect']} | {s['worse']} | {s['crash']} | {s['works_pct']} | {s['mean_gain_s']} | {s['planted_flaw_first']} | {s['planted_fix_direction_first']} |")
    (RESULTS / "benchmark.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    print(f"total {time.time() - t0:.0f} s")


if __name__ == "__main__":
    main()
