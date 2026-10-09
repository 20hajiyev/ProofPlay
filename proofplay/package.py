"""Builds a portable ProofPlay folder that runs on another Windows PC (no build tools, no repo).

    python proofplay/package.py [--out dist/ProofPlay] [--zip]

Layout:
    bin/proofplay.exe            headless replay + analysis (no GPU needed)
    content/{tracks,vehicles,combat}  the only game data a replay reads
    dashboard/                   server.py, advice.py, index.html, assets/
    agent/uploader.py            for the player's PC: sends finished races to the server
    ProofPlay.pyw                double-click launcher: server in the background + browser
    dashboard/games.json         supported games (process name, adapter); add a line per new game
    start_server.bat             local use (this PC only)
    start_server_lan.bat         other PCs on the network can open the site and upload
    sessions/                    races land here (the game's own folder, or uploads)
Requires Python 3.10+ on the PC that runs the server; the uploader needs Python too.
"""
import argparse
import pathlib
import shutil

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXE = ROOT / "build/game/proofplay/Release/proofplay.exe"

START = """@echo off
cd /d "%~dp0"
python dashboard\\server.py --port 8787 {host}
pause
"""
UPLOADER = """@echo off
rem Run on the player's PC. Edit the server address and your driver name.
cd /d "%~dp0"
python agent\\uploader.py --server http://SERVER-PC:8787 --player Driver
pause
"""
README = """ProofPlay portable

0. Everyday use: double-click ProofPlay.pyw (Python 3.10+ needed). The site opens by itself.
   Open your game, press "Start session" on the site, drive. Every race you finish is analysed.
   "Quit ProofPlay" at the bottom of the page stops it.
1. Server PC: install Python 3.10+, then double-click start_server.bat and open http://localhost:8787
   To let other PCs on the same network use it: start_server_lan.bat, then open
   http://<this PC's IP>:8787 on them (allow Python through Windows Firewall when asked).
2. Player PC: the game saves each race in its sessions folder. Either
   - upload the file on the website (upload button or drag and drop), or
   - edit and run start_uploader.bat: it sends every new race automatically.
3. The server replays every race itself. A file that was edited does not reproduce and is
   marked as not verified, so it never reaches the leaderboard.
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "dist/ProofPlay"))
    ap.add_argument("--zip", action="store_true")
    args = ap.parse_args()
    out = pathlib.Path(args.out)
    if out.exists():
        shutil.rmtree(out)
    (out / "bin").mkdir(parents=True)
    shutil.copy2(EXE, out / "bin/proofplay.exe")
    # App-local VC++ runtime (Microsoft allows redistributing these), so a PC without the VC++
    # Redistributable still runs it. XAudio2_9 / XInput1_4 ship with Windows 10+.
    crt = sorted(pathlib.Path("C:/Program Files/Microsoft Visual Studio/2022").glob("*/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT"))
    if crt:
        for dll in ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"):
            if (crt[-1] / dll).exists():
                shutil.copy2(crt[-1] / dll, out / "bin" / dll)
    else:
        print("note: VC++ runtime not found; the target PC needs the VC++ 2015-2022 Redistributable")
    for sub in ("tracks", "vehicles", "combat"):
        shutil.copytree(ROOT / "content" / sub, out / "content" / sub)
    dash = ROOT / "proofplay/dashboard"
    (out / "dashboard").mkdir()
    for f in ("server.py", "advice.py", "index.html", "games.json"):
        shutil.copy2(dash / f, out / "dashboard" / f)
    shutil.copytree(dash / "assets", out / "dashboard/assets")
    (out / "agent").mkdir()
    shutil.copy2(ROOT / "proofplay/agent/uploader.py", out / "agent/uploader.py")
    (out / "sessions").mkdir()
    shutil.copy2(ROOT / "proofplay/ProofPlay.pyw", out / "ProofPlay.pyw")
    (out / "start_server.bat").write_text(START.format(host=""), encoding="ascii")
    (out / "start_server_lan.bat").write_text(START.format(host="--host 0.0.0.0"), encoding="ascii")
    (out / "start_uploader.bat").write_text(UPLOADER, encoding="ascii")
    (out / "README.txt").write_text(README, encoding="utf-8")
    size = sum(f.stat().st_size for f in out.rglob("*") if f.is_file())
    print(f"packaged {out} ({size / 1e6:.1f} MB)")
    if args.zip:
        z = shutil.make_archive(str(out), "zip", out.parent, out.name)
        print("zip:", z)


if __name__ == "__main__":
    main()
