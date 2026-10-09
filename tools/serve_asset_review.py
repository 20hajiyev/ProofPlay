"""Serve the asset review locally without exposing the repository or network."""
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit
import argparse

ROOT = Path(__file__).resolve().parents[1]

class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def send_head(self):
        route = unquote(urlsplit(self.path).path)
        candidate = (ROOT / route.lstrip('/')).resolve()
        allowed = any(candidate.is_relative_to(ROOT / folder) for folder in ('review', 'assets'))
        if not allowed or not candidate.is_file():
            self.send_error(404)
            return None
        return super().send_head()

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=8766)
    args = parser.parse_args()
    print(f'Asset review: http://127.0.0.1:{args.port}/review/index.html', flush=True)
    ThreadingHTTPServer(('127.0.0.1', args.port), Handler).serve_forever()
