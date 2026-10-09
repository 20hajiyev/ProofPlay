"""ProofPlay launcher: double-click to start. No terminal, no AI, no commands.

Starts the dashboard server in the background (if it is not already running) and opens the
site in the browser. Open your game, press "Start session" on the page, and every race you
finish is filed under your name and analysed automatically. Closing the browser leaves the server running; double-clicking again just
reopens the page. To stop it, use the Quit link at the bottom of the page.
"""
import pathlib
import socket
import sys
import threading
import webbrowser

HERE = pathlib.Path(__file__).resolve().parent
PORT = 8787
URL = f"http://localhost:{PORT}"


def running():
    with socket.socket() as s:
        s.settimeout(0.3)
        return s.connect_ex(("127.0.0.1", PORT)) == 0


def main():
    if running():
        webbrowser.open(URL)
        return
    sys.path.insert(0, str(HERE / "dashboard"))
    log = open(HERE / "proofplay.log", "a", encoding="utf-8", buffering=1)  # pythonw has no console
    sys.stdout = sys.stderr = log
    import server  # noqa: E402
    sys.argv = [sys.argv[0], "--port", str(PORT)]
    threading.Timer(0.8, webbrowser.open, [URL]).start()
    server.main()  # serves until Quit on the page (POST /api/quit ends the process)


if __name__ == "__main__":
    main()
