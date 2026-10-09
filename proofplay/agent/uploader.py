"""ProofPlay uploader: runs on the player's PC next to the game and sends every finished race to a
ProofPlay server on another computer. No screen recording: the game already saves the race as its
inputs (a 1-2 MB JSON), which the server replays exactly. Stdlib only.

    python uploader.py --server http://192.168.1.20:8787 --player Sanan [--sessions DIR]

--sessions defaults to the game's folder: <PROOFPLAY_SESSIONS or proofplay/sessions>/<player>.
Already-sent files are remembered in .uploaded next to them, so restarting does not resend.
"""
import argparse
import json
import os
import pathlib
import time
import urllib.error
import urllib.parse
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent


def send(server, player, path):
    url = server.rstrip("/") + "/api/upload?" + urllib.parse.urlencode({"player": player})
    req = urllib.request.Request(url, data=path.read_bytes(), method="POST", headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.loads(r.read())
    except urllib.error.HTTPError as e:  # the server explains what was wrong with the file
        return json.loads(e.read() or b"{}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", required=True, help="e.g. http://192.168.1.20:8787")
    ap.add_argument("--player", required=True)
    ap.add_argument("--sessions", help="folder the game writes races into")
    ap.add_argument("--once", action="store_true", help="send what is there and exit")
    args = ap.parse_args()
    root = os.environ.get("PROOFPLAY_SESSIONS")
    folder = pathlib.Path(args.sessions) if args.sessions else pathlib.Path(root or HERE.parent / "sessions") / args.player
    folder.mkdir(parents=True, exist_ok=True)
    sent_file = folder / ".uploaded"
    sent = set(sent_file.read_text(encoding="utf-8").split()) if sent_file.exists() else set()
    print(f"watching {folder} -> {args.server} as {args.player}")
    while True:
        for f in sorted(folder.glob("*.json"), key=lambda p: p.stat().st_mtime):
            if f.name in sent or ".whatif" in f.name:
                continue
            try:
                res = send(args.server, args.player, f)
            except (urllib.error.URLError, OSError) as e:
                print("server unreachable, will retry:", e)
                break
            if res.get("ok"):
                print("sent", f.name)
            else:
                print("rejected", f.name, "-", res.get("error"))
            sent.add(f.name)  # rejected files are not retried; the reason is printed above
            sent_file.write_text("\n".join(sorted(sent)), encoding="utf-8")
        if args.once:
            return
        time.sleep(3)


if __name__ == "__main__":
    main()
