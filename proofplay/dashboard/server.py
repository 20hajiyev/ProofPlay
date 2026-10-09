"""ProofPlay dashboard server (stdlib only).

Watches sessions/<player>/*.json written by the game, verifies each by deterministic replay,
analyses it with proofplay.exe, stores the report in SQLite and serves the dashboard.

    python proofplay/dashboard/server.py [--port 8787] [--host 127.0.0.1] [--data DIR] [--exe PATH]

Sessions arrive two ways: the game writes them into <data>/sessions/<player>/, or another PC
uploads them (POST /api/upload, from the dashboard or proofplay/agent/uploader.py). Uploads are
not trusted: the replay check is what decides whether a result counts (D-097).
"""
import argparse
import json
import os
import pathlib
import re
import sqlite3
import subprocess
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs

import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from advice import build_advice, consistency_check, quick_hints, stats_items  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent


def find_exe():
    """Dev tree build, or the packaged layout (bin/proofplay.exe next to dashboard/)."""
    for c in (os.environ.get("PROOFPLAY_EXE"), HERE.parent / "bin/proofplay.exe", ROOT / "build/game/proofplay/Release/proofplay.exe"):
        if c and pathlib.Path(c).exists():
            return pathlib.Path(c)
    return ROOT / "build/game/proofplay/Release/proofplay.exe"


EXE = find_exe()


# Supported games (D-097). The player opens the game themselves; ProofPlay only notices which one is
# running and, during a session, files every race that game records under the session's driver.
GAMES = json.loads((HERE / "games.json").read_text(encoding="utf-8")) if (HERE / "games.json").exists() else []
live = {"player": None, "game": None, "since": 0.0, "races": 0}


def save_live():
    """The session survives a server restart (it used to live only in memory, D-099)."""
    try:
        (DATA / "session.json").write_text(json.dumps(live), encoding="utf-8")
    except OSError as e:
        print("session not saved:", e)


def load_live():
    try:
        live.update(json.loads((DATA / "session.json").read_text(encoding="utf-8")))
    except (OSError, ValueError):
        pass
_seen = {"t": 0.0, "games": []}


def running_games():
    """Supported games that are open right now (Windows process list, cached for 2 s)."""
    if time.time() - _seen["t"] < 2:
        return _seen["games"]
    try:
        out = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True, text=True, timeout=5).stdout.lower()
    except Exception:
        out = ""
    _seen.update(t=time.time(), games=[g for g in GAMES if f'"{g["process"].lower()}"' in out])
    return _seen["games"]


def claim(f: pathlib.Path):
    """A race recorded during a live session into a game's inbox folder moves to the driver's folder."""
    if not live["player"] or f.stat().st_mtime < live["since"]:
        return f
    if not any(f.parent.name == g.get("inbox") for g in GAMES) or f.parent.name == live["player"]:
        return f
    dest = SESSIONS / live["player"] / f.name
    dest.parent.mkdir(parents=True, exist_ok=True)
    f.replace(dest)
    live["races"] += 1
    save_live()
    print(f"session: {f.parent.name}/{f.name} -> {live['player']}")
    return dest
DATA = pathlib.Path(os.environ.get("PROOFPLAY_DATA", HERE.parent))
SESSIONS = DATA / "sessions"
REPORTS = DATA / "reports"
DB = DATA / "proofplay.db"
MAX_UPLOAD = 8 * 1024 * 1024  # a 3-lap 12-car session is ~1-2 MB
NAME_OK = re.compile(r"^[A-Za-z0-9_\- ]{1,32}$")

state = {"busy": None, "proving": None}
lock = threading.Lock()


def db():
    con = sqlite3.connect(DB)
    con.execute(
        """create table if not exists runs (
            id integer primary key, player text, file text unique, created real,
            verified integer, overall integer, time_s real, position integer, track text,
            report text)"""
    )
    if "stage" not in [c[1] for c in con.execute("pragma table_info(runs)")]:
        con.execute("alter table runs add column stage text default 'full'")
    return con


def league(overall):
    return "Elite" if overall >= 85 else "Gold" if overall >= 70 else "Silver" if overall >= 55 else "Bronze"


def quick_analysis(stats):
    """Stage 1: the game's own results-screen numbers - instant, but unverified hints.
    The replay analysis later replaces hints with measured findings and proven fixes."""
    laps = stats.get("lap_times") or []
    racing_laps = laps[1:] if len(laps) > 1 else laps  # lap 1 includes the standing start
    uses, hits = stats.get("uses", 0), stats.get("hits_dealt", 0)
    q = {
        "position": stats.get("position", 0), "participants": stats.get("participants", 0),
        "time_s": stats.get("time_s", 0.0), "finished": stats.get("finished", False),
        "lap_times": laps, "best_lap_s": min(racing_laps) if racing_laps else None,
        "lap_spread_s": (max(racing_laps) - min(racing_laps)) if len(racing_laps) > 1 else 0.0,
        "top_speed_kmh": stats.get("top_speed_kmh", 0.0), "avg_speed_kmh": stats.get("avg_speed_kmh", 0.0),
        "uses": uses, "hits": hits, "hit_rate": hits / uses if uses else None, "damage_taken": stats.get("damage_taken", 0.0),
        "wrecks": stats.get("wrecks", 0), "wrecks_caused": stats.get("wrecks_caused", 0), "resets": stats.get("resets", 0),
    }
    q["hints"] = quick_hints(q)
    return q


def store(con, player, path, stage, report, verified):
    r = report.get("result", {})
    con.execute(
        "insert into runs (player, file, created, verified, overall, time_s, position, track, report, stage)"
        " values (?,?,?,?,?,?,?,?,?,?) on conflict(file) do update set verified=excluded.verified,"
        " overall=excluded.overall, time_s=excluded.time_s, position=excluded.position, report=excluded.report, stage=excluded.stage",
        (player, str(path), path.stat().st_mtime, int(verified), report.get("overall"), r.get("time_s"),
         r.get("position"), report["setup"]["track"], json.dumps(report), stage),
    )


BLENDER = ROOT / ".tools/blender-4.5.14-windows-x64/blender.exe"


def ensure_track_art(track_id):
    """A new track gets its top-down map photo rendered once, in the background (Blender)."""
    if (HERE / "assets" / f"track_{track_id}.png").exists() or not BLENDER.exists():
        return
    print("rendering map art for new track", track_id)
    subprocess.Popen([str(BLENDER), "-b", "-P", str(ROOT / "tools/render_proofplay_assets.py"), "--", f"--track={track_id}"],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def analyse(path: pathlib.Path, first=True, full=True):
    """first: results-screen stats + replay (seconds); full: proven fixes (a minute or more)."""
    player = path.parent.name
    session = json.loads(path.read_text(encoding="utf-8"))
    setup = session["setup"]
    ensure_track_art(setup["track"])
    grid = setup.get("grid") or []
    slot = setup.get("player_index", 0)
    car = grid[slot] if slot < len(grid) else ["D01", "D02", "D03", "D04", "D05"][slot % 5]
    quick = quick_analysis(session["stats"]) if "stats" in session else None
    # Stage 1: instant, from the game's own statistics.
    if quick and first:
        report = {"stage": "quick", "quick": quick,
                  "setup": {"player": player, "track": setup["track"], "laps": setup["laps"], "cars": setup["cars"], "car": car},
                  "result": {"position": quick["position"], "time_s": quick["time_s"], "finished": quick["finished"], "verified": False}}
        with db() as con:
            store(con, player, path, "quick", report, False)
        print(f"quick {player}/{path.name}: P{quick['position']} {len(quick['hints'])} hints")
    REPORTS.mkdir(parents=True, exist_ok=True)
    # Stage 2: replay the recording, score skills and find where time went (seconds, no what-if).
    fast_out = REPORTS / (player + "__" + path.stem + ".fast.json")
    t0 = time.time()
    proc = subprocess.run([str(EXE), "analyze", str(path), "--out", str(fast_out), "--fast"], capture_output=True, text=True) if first else None
    if proc and proc.returncode == 0 and fast_out.exists():
        report = json.loads(fast_out.read_text(encoding="utf-8"))
        report.update({"stage": "fast", "analysis_s": round(time.time() - t0, 1)})
        report["setup"]["car"] = car
        if quick:
            report["quick"] = quick
        with db() as con:
            store(con, player, path, "fast", report, report["result"]["verified"])
        print(f"fast {player}/{path.name}: OVR {report['overall']} in {report['analysis_s']} s")
    if not full:
        return
    # Stage 3: prove fixes by re-simulating changed inputs (parallel worker processes).
    out = REPORTS / (player + "__" + path.stem + ".json")
    t0 = time.time()
    proc = subprocess.run([str(EXE), "analyze", str(path), "--out", str(out)], capture_output=True, text=True)
    if proc.returncode != 0 or not out.exists():
        print("analyze failed", path, proc.stdout, proc.stderr)
        return
    report = json.loads(out.read_text(encoding="utf-8"))
    report["analysis_s"] = round(time.time() - t0, 1)
    report["stage"] = "full"
    report["setup"]["car"] = car
    if quick:
        report["quick"] = quick
    with db() as con:
        store(con, player, path, "full", report, report["result"]["verified"])
    print(f"analysed {player}/{path.name}: OVR {report['overall']} verified={report['result']['verified']} in {report['analysis_s']} s")


def stages():
    with db() as con:
        return {row[0]: row[1] for row in con.execute("select file, stage from runs")}


def watcher():
    """New races first: results screen + replay take seconds, so a race just driven shows up at
    once even while another race's proven fixes (minutes) are still being computed (D-098)."""
    failed = set()  # a file the replay could not read is not retried every second
    while True:
        try:
            seen = stages()
            for f in sorted(SESSIONS.glob("*/*.json"), key=lambda p: p.stat().st_mtime, reverse=True):
                if ".whatif" in f.name or str(f) in failed or seen.get(str(f)) in ("fast", "full"):
                    continue  # worker scratch files sit next to sessions
                f = claim(f)
                with lock:
                    state["busy"] = f"{f.parent.name}/{f.name}"
                try:
                    analyse(f, first=True, full=False)
                finally:
                    with lock:
                        state["busy"] = None
                if stages().get(str(f)) not in ("fast", "full"):
                    failed.add(str(f))
                break  # re-scan: a newer race may have arrived meanwhile
        except Exception as e:  # keep serving even if one file is bad
            print("watcher:", e)
        time.sleep(1)


def prover():
    """Proven fixes, one race at a time, newest first."""
    failed = set()
    while True:
        try:
            seen = stages()
            todo = [f for f in SESSIONS.glob("*/*.json") if seen.get(str(f)) == "fast" and str(f) not in failed]
            if todo:
                f = max(todo, key=lambda p: p.stat().st_mtime)
                with lock:
                    state["proving"] = f"{f.parent.name}/{f.name}"
                try:
                    analyse(f, first=False, full=True)
                finally:
                    with lock:
                        state["proving"] = None
                if stages().get(str(f)) != "full":
                    failed.add(str(f))
                continue
        except Exception as e:
            print("prover:", e)
        time.sleep(2)


def runs(player=None):
    with db() as con:
        q = "select id, player, created, verified, overall, time_s, position, track, stage from runs"
        rows = con.execute(q + (" where player=?" if player else "") + " order by created", (player,) if player else ()).fetchall()
    return [dict(zip(["id", "player", "created", "verified", "overall", "time_s", "position", "track", "stage"], r)) for r in rows]


def leaderboard():
    all_runs = runs()
    players = {}
    for r in all_runs:
        players.setdefault(r["player"], []).append(r)
    rows = []
    for name, rs in players.items():
        verified = [r for r in rs if r["verified"] and r["stage"] in ("fast", "full")]
        if not verified:
            continue
        best = min((r for r in verified if r["position"] > 0), key=lambda r: r["time_s"], default=None)
        latest = verified[-1]["overall"]
        rows.append({
            "player": name, "overall": latest, "league": league(latest), "races": len(verified),
            "best_time_s": best["time_s"] if best else None,
            "improvement": latest - verified[0]["overall"],
        })
    rows.sort(key=lambda r: -r["overall"])
    return rows


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=str(HERE), **k)

    def log_message(self, *a):
        pass

    def send_json(self, obj, code=200):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path, _, query = self.path.partition("?")
        params = {k: v[0] for k, v in parse_qs(query).items()}
        if path == "/api/status":
            with lock:
                busy = state["busy"] or state["proving"]
            return self.send_json({"busy": busy, "local": self.is_local(),
                                   "games": [{"id": g["id"], "name": g["name"]} for g in running_games()],
                                   "supported": [g["name"] for g in GAMES],
                                   "session": {"player": live["player"], "game": live["game"] or next((g["name"] for g in running_games()), None),
                                               "races": live["races"]} if live["player"] else None})
        if path == "/api/players":
            # The session's driver shows up at once, before their first race arrives.
            return self.send_json(sorted({r["player"] for r in runs()} | ({live["player"]} if live["player"] else set())))
        if path == "/api/runs":
            return self.send_json(runs(params.get("player")))
        if path == "/api/report":
            with db() as con:
                row = con.execute("select report from runs where id=?", (params.get("id"),)).fetchone()
            if not row:
                return self.send_json(None)
            report = json.loads(row[0])
            # Advice is derived on every request, so stored reports pick up improved explanations.
            if report.get("quick"):
                report["quick"]["hints"] = quick_hints(report["quick"])
            try:
                if report.get("stage") == "quick":
                    report["advice"] = stats_items(report.get("quick"))
                else:
                    report["advice"] = build_advice(report)
                    report["consistency"] = consistency_check(report.get("quick"), report)
            except Exception as e:  # one malformed report must not blank the dashboard
                print("advice failed:", type(e).__name__, e)
                report["advice"] = []
            return self.send_json(report)
        if path == "/api/leaderboard":
            return self.send_json(leaderboard())
        return super().do_GET()


    def is_local(self):
        return self.client_address[0] in ("127.0.0.1", "::1")

    def session(self, action, params):
        """Start/stop a live session on this computer. The custom header cannot be sent cross-site
        without a CORS preflight (never approved here), so another open web page cannot do it."""
        if not self.is_local() or self.headers.get("X-ProofPlay") != "1":
            return self.send_json({"ok": False, "error": "Sessions are started on the computer that runs the game."}, 403)
        if action == "stop":
            live.update(player=None, game=None)
            save_live()
            return self.send_json({"ok": True})
        player = params.get("player", "").strip()
        if not NAME_OK.match(player):
            return self.send_json({"ok": False, "error": "Driver name: 1-32 letters, digits, spaces, - or _."}, 400)
        # The game may be opened before or after Start: any supported game's races count.
        games = running_games()
        live.update(player=player, game=games[0]["name"] if games else None, since=time.time(), races=0)
        save_live()
        print(f"session started: {player} in {live['game']}")
        return self.send_json({"ok": True, "player": player, "game": live["game"]})

    def do_POST(self):
        path, _, query = self.path.partition("?")
        params = {k: v[0] for k, v in parse_qs(query).items()}
        if path == "/api/quit":  # the launcher has no window to close; the page offers Quit
            if not self.is_local() or self.headers.get("X-ProofPlay") != "1":
                return self.send_json({"ok": False}, 403)
            self.send_json({"ok": True})
            threading.Timer(0.3, os._exit, [0]).start()
            return
        if path in ("/api/session/start", "/api/session/stop"):
            return self.session(path.rsplit("/", 1)[1], params)
        if path != "/api/upload":
            return self.send_error(404)
        player = params.get("player", "").strip()
        size = int(self.headers.get("Content-Length") or 0)
        if not NAME_OK.match(player):
            return self.send_json({"ok": False, "error": "Driver name: 1-32 letters, digits, spaces, - or _."}, 400)
        if not 0 < size <= MAX_UPLOAD:
            return self.send_json({"ok": False, "error": "The file must be a ProofPlay session under 8 MB."}, 400)
        try:
            session = json.loads(self.rfile.read(size).decode("utf-8"))
            setup, inputs = session["setup"], session["inputs"]
            assert isinstance(setup.get("track"), str) and re.match(r"^[A-Za-z0-9_]{1,16}$", setup["track"])
            assert isinstance(inputs, list) and inputs
        except Exception:
            return self.send_json({"ok": False, "error": "This is not a ProofPlay session file (setup and inputs are missing)."}, 400)
        folder = SESSIONS / player
        folder.mkdir(parents=True, exist_ok=True)
        name = time.strftime("%Y%m%d_%H%M%S") + "_up.json"
        tmp = folder / (name + ".tmp")
        tmp.write_text(json.dumps(session), encoding="utf-8")
        tmp.replace(folder / name)  # the watcher only picks up complete files
        print(f"upload {player}/{name} ({size // 1024} KB)")
        return self.send_json({"ok": True, "player": player, "file": name})


def main():
    global EXE, DATA, SESSIONS, REPORTS, DB
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8787)
    ap.add_argument("--host", default="127.0.0.1", help="0.0.0.0 to accept other computers on the network")
    ap.add_argument("--data", help="folder for sessions, reports and the database")
    ap.add_argument("--exe", help="path to proofplay.exe")
    args = ap.parse_args()
    if args.data:
        DATA = pathlib.Path(args.data)
        SESSIONS, REPORTS, DB = DATA / "sessions", DATA / "reports", DATA / "proofplay.db"
    if args.exe:
        EXE = pathlib.Path(args.exe)
    if not EXE.exists():
        print("proofplay.exe not found:", EXE, "(pass --exe or set PROOFPLAY_EXE)")
    SESSIONS.mkdir(parents=True, exist_ok=True)
    sys.stdout.reconfigure(line_buffering=True)  # log lines appear as they happen
    load_live()
    db().close()
    threading.Thread(target=watcher, daemon=True).start()
    threading.Thread(target=prover, daemon=True).start()
    shown = "localhost" if args.host in ("127.0.0.1", "0.0.0.0") else args.host
    print(f"ProofPlay dashboard: http://{shown}:{args.port}  (data: {DATA}, host: {args.host})")
    ThreadingHTTPServer((args.host, args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
