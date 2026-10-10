#!/usr/bin/env python3
"""A small fake Audiobookshelf server for ABSvita tests and emulator runs.

    python3 tools/mock_abs.py [--port 13378] [--media tests/media] [--books 60]

Login with any username and the password "pw". Books 0-2 have real audio
(long.m4b, faststart.m4a, part1+part2.mp3); the rest are cover-only filler
so the library, series and authors screens have something to show.

Test controls (no auth):
    POST /__test/expire        invalidate every access token issued so far
    POST /__test/drop?after=N  the next audio file response is cut after N bytes
    GET  /__test/log           JSON list of requests since the last reset
    POST /__test/reset         clear the log and test switches
"""
import argparse
import json
import os
import re
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

LIB_ID = "lib-books"
USER_ID = "user-1"

state = {
    "access": set(),       # valid access tokens
    "refresh": set(),      # valid refresh tokens
    "n": 0,
    "drop_after": None,
    "log": [],
    "progress": {},        # item id -> (currentTime, lastUpdate ms)
    "sessions": {},
}
lock = threading.Lock()


def probe(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration",
                          "-of", "default=nw=1:nk=1", path], capture_output=True, text=True)
    return float(out.stdout.strip() or 0)


AUTHORS = ["Ursula K. Le Guin", "Terry Pratchett", "Frank Herbert", "Madeline Miller",
           "Brandon Sanderson", "Andy Weir", "N. K. Jemisin", "Becky Chambers"]
SERIES = [("Discworld", 1), ("Earthsea", 2), ("Dune", 3), ("Mistborn", 4), ("Wayfarers", 7)]
WORDS = ["Silent", "Hidden", "Last", "Iron", "Glass", "Winter", "Distant", "Burning", "Quiet",
         "Golden", "Broken", "Wandering"]
NOUNS = ["Harbour", "Crown", "Library", "Orbit", "Forest", "Engine", "Empire", "River",
         "Lantern", "Archive", "Garden", "Signal"]


def make_showcase(media, path):
    """Books from tools/fetch_showcase.py (public-domain LibriVox titles and covers).
    A few get the generated test audio so they can be played and downloaded."""
    entries = json.load(open(path))
    real = {e["title"] for e in entries[:3]}

    def track(i, fname, start, codec, mime):
        p = os.path.join(media, fname)
        return {"index": i + 1, "ino": "ino-%s" % fname.replace(".", "-"), "fname": fname, "path": p,
                "startOffset": start, "duration": probe(p), "codec": codec, "mimeType": mime,
                "ext": os.path.splitext(fname)[1], "size": os.path.getsize(p)}

    sets = [[track(0, "long.m4b", 0, "aac", "audio/mp4")], [track(0, "faststart.m4a", 0, "aac", "audio/mp4")]]
    p1 = track(0, "part1.mp3", 0, "mp3", "audio/mpeg")
    sets.append([p1, track(1, "part2.mp3", p1["duration"], "mp3", "audio/mpeg")])
    books = []
    for i, e in enumerate(entries):
        tracks = sets[i] if e["title"] in real else []
        dur = sum(t["duration"] for t in tracks) if tracks else 3600.0 * (3 + (i * 7) % 15) + 60 * (i * 13 % 60)
        chapters = [{"id": k, "start": k * dur / 12, "end": (k + 1) * dur / 12, "title": "Chapter %d" % (k + 1)}
                    for k in range(12)] if tracks else []
        books.append({"id": "sc-%02d" % i, "title": e["title"], "author": e["author"], "series": e["series"],
                      "narrator": "LibriVox volunteers", "tracks": tracks, "duration": dur, "chapters": chapters,
                      "cover": e["cover"], "addedAt": 1700000000000 - i * 86400000,
                      "description": "A LibriVox recording of %s by %s. LibriVox recordings are read by volunteers "
                                     "and are in the public domain." % (e["title"], e["author"])})
    return books


def make_books(media, nfiller):
    books = []

    def track(i, fname, start, codec, mime):
        path = os.path.join(media, fname)
        return {"index": i + 1, "ino": "ino-%s" % fname.replace(".", "-"), "fname": fname, "path": path,
                "startOffset": start, "duration": probe(path), "codec": codec, "mimeType": mime,
                "ext": os.path.splitext(fname)[1], "size": os.path.getsize(path)}

    long_t = [track(0, "long.m4b", 0, "aac", "audio/mp4")]
    fast_t = [track(0, "faststart.m4a", 0, "aac", "audio/mp4")]
    p1 = track(0, "part1.mp3", 0, "mp3", "audio/mpeg")
    p2 = track(1, "part2.mp3", p1["duration"], "mp3", "audio/mpeg")
    real = [
        ("li-long", "The Long Listen", "Ursula K. Le Guin", "Earthsea #1", long_t,
         [(h * 3600.0, (h + 1) * 3600.0, "Hour %d" % (h + 1)) for h in range(6)]),
        ("li-fast", "A Short Story", "Andy Weir", "", fast_t,
         [(0, 60, "Opening"), (60, 120, "Ending")]),
        ("li-mp3", "Two Parts", "Terry Pratchett", "Discworld #3", [p1, p2], []),
    ]
    for i, (iid, title, author, series, tracks, chapters) in enumerate(real):
        dur = sum(t["duration"] for t in tracks)
        books.append({"id": iid, "title": title, "author": author, "series": series,
                      "narrator": "Test Narrator", "tracks": tracks, "duration": dur,
                      "chapters": [{"id": k, "start": s, "end": min(e, dur), "title": n}
                                   for k, (s, e, n) in enumerate(chapters)],
                      "cover": "cover%d.jpg" % (i % 8), "addedAt": 1700000000000 + i})
    for i in range(nfiller):
        s = SERIES[i % len(SERIES)] if i % 3 else None
        books.append({"id": "li-f%03d" % i,
                      "title": "The %s %s" % (WORDS[i % len(WORDS)], NOUNS[(i * 7) % len(NOUNS)]),
                      "author": AUTHORS[(i * 5) % len(AUTHORS)],
                      "series": ("%s #%d" % (s[0], (i // 3) % 9 + 1)) if s else "",
                      "narrator": "Filler Narrator", "tracks": [], "duration": 3600.0 * (2 + i % 17),
                      "chapters": [], "cover": "cover%d.jpg" % ((i + 3) % 8),
                      "addedAt": 1700000000000 - i * 86400000})
    return books


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    books = []
    media = ""

    def log_message(self, fmt, *args):
        pass

    # -- helpers -------------------------------------------------------------
    def send_json(self, obj, status=200):
        body = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def body_json(self):
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(n) if n else b""
        try:
            return json.loads(raw or b"{}")
        except ValueError:
            return {}

    def authed(self):
        h = self.headers.get("Authorization", "")
        tok = h[7:] if h.startswith("Bearer ") else parse_qs(urlparse(self.path).query).get("token", [""])[0]
        with lock:
            return tok in state["access"]

    def issue(self):
        with lock:
            state["n"] += 1
            a, r = "acc-%d" % state["n"], "ref-%d" % state["n"]
            state["access"].add(a)
            state["refresh"].add(r)
        return a, r

    def book(self, iid):
        return next((b for b in self.books if b["id"] == iid), None)

    def progress_list(self):
        out = []
        with lock:
            for iid, (ct, upd) in state["progress"].items():
                b = self.book(iid)
                if b:
                    out.append({"id": "mp-" + iid, "libraryItemId": iid, "episodeId": None,
                                "duration": b["duration"], "progress": ct / b["duration"] if b["duration"] else 0,
                                "currentTime": ct, "isFinished": False, "lastUpdate": upd})
        return out

    def minified(self, b):
        return {"id": b["id"], "ino": "ino-" + b["id"], "libraryId": LIB_ID, "mediaType": "book",
                "addedAt": b["addedAt"],
                "media": {"id": "media-" + b["id"],
                          "metadata": {"title": b["title"], "titleIgnorePrefix": b["title"], "subtitle": None,
                                       "authorName": b["author"], "authorNameLF": b["author"],
                                       "narratorName": b["narrator"], "seriesName": b["series"],
                                       "description": b.get("description", "<p>A book for testing <b>ABSvita</b>.</p>")},
                          "coverPath": "/covers/" + b["cover"], "numTracks": len(b["tracks"]),
                          "duration": b["duration"], "size": sum(t["size"] for t in b["tracks"])}}

    def audio_tracks(self, b):
        return [{"index": t["index"], "ino": t["ino"], "startOffset": t["startOffset"], "duration": t["duration"],
                 "title": t["fname"], "contentUrl": "/api/items/%s/file/%s" % (b["id"], t["ino"]),
                 "mimeType": t["mimeType"], "codec": t["codec"],
                 "metadata": {"filename": t["fname"], "ext": t["ext"], "size": t["size"]}} for t in b["tracks"]]

    def expanded(self, b):
        authors = [{"id": "au-" + b["author"], "name": b["author"]}]
        series = []
        m = re.match(r"(.*) #(\S+)$", b["series"] or "")
        if m:
            series = [{"id": "se-" + m.group(1), "name": m.group(1), "sequence": m.group(2)}]
        return {"id": b["id"], "libraryId": LIB_ID, "mediaType": "book", "addedAt": b["addedAt"],
                "media": {"metadata": {"title": b["title"], "authors": authors, "narrators": [b["narrator"]],
                                       "series": series, "description": b.get("description", "<p>A book for testing ABSvita.</p>")},
                          "chapters": b["chapters"], "duration": b["duration"],
                          "tracks": self.audio_tracks(b), "audioFiles": []}}

    # -- routing -------------------------------------------------------------
    def handle_any(self, method):
        u = urlparse(self.path)
        p = u.path
        q = parse_qs(u.query)
        with lock:
            state["log"].append({"m": method, "p": p, "range": self.headers.get("Range"), "t": time.time()})

        if p == "/__test/expire":
            with lock:
                state["access"].clear()
            return self.send_json({"ok": True})
        if p == "/__test/drop":
            with lock:
                state["drop_after"] = int(q.get("after", ["0"])[0])
            return self.send_json({"ok": True})
        if p == "/__test/log":
            with lock:
                return self.send_json(list(state["log"]))
        if p == "/__test/reset":
            with lock:
                state["log"].clear()
                state["drop_after"] = None
            return self.send_json({"ok": True})

        if p == "/status":
            return self.send_json({"app": "audiobookshelf", "serverVersion": "2.31.0", "isInit": True})
        if p == "/login" and method == "POST":
            body = self.body_json()
            if body.get("password") != "pw":
                self.send_response(401)
                self.send_header("Content-Length", "12")
                self.end_headers()
                return self.wfile.write(b"Unauthorized")
            a, r = self.issue()
            return self.send_json({"user": {"id": USER_ID, "username": body.get("username", "root"), "type": "root",
                                            "token": a, "accessToken": a, "refreshToken": r,
                                            "mediaProgress": self.progress_list(), "isActive": True},
                                   "userDefaultLibraryId": LIB_ID, "serverSettings": {"version": "2.31.0"},
                                   "Source": "mock"})
        if p == "/auth/refresh" and method == "POST":
            rt = self.headers.get("x-refresh-token", "")
            with lock:
                ok = rt in state["refresh"]
                if ok:
                    state["refresh"].discard(rt)
            if not ok:
                return self.send_json({"error": "invalid"}, 401)
            a, r = self.issue()
            return self.send_json({"user": {"id": USER_ID, "username": "root", "accessToken": a,
                                            "refreshToken": r, "mediaProgress": []}})

        if p.startswith("/covers/") or re.match(r"^/api/items/[^/]+/cover$", p):
            if p.startswith("/api/"):
                b = self.book(p.split("/")[3])
                name = b["cover"] if b else "cover0.jpg"
            else:
                name = os.path.basename(p)
            return self.send_file(os.path.join(self.media, name), "image/jpeg", drop=False)

        if not p.startswith("/api/"):
            return self.send_json({"error": "not found"}, 404)
        if not self.authed():
            self.send_response(401)
            self.send_header("Content-Length", "12")
            self.end_headers()
            return self.wfile.write(b"Unauthorized")

        if p == "/api/libraries":
            return self.send_json({"libraries": [{"id": LIB_ID, "name": "Audiobooks", "mediaType": "book"}]})
        m = re.match(r"^/api/libraries/([^/]+)/items$", p)
        if m:
            limit = int(q.get("limit", ["100"])[0])
            page = int(q.get("page", ["0"])[0])
            res = [self.minified(b) for b in self.books[page * limit:(page + 1) * limit]]
            return self.send_json({"results": res, "total": len(self.books), "limit": limit, "page": page,
                                   "sortBy": "media.metadata.title", "mediaType": "book", "minified": True})
        if p == "/api/me":
            return self.send_json({"id": USER_ID, "username": "root", "mediaProgress": self.progress_list()})
        if p == "/api/me/items-in-progress":
            with lock:
                ids = sorted(state["progress"], key=lambda k: -state["progress"][k][1])
            return self.send_json({"libraryItems": [self.minified(self.book(i)) for i in ids if self.book(i)]})
        m = re.match(r"^/api/items/([^/]+)$", p)
        if m:
            b = self.book(m.group(1))
            return self.send_json(self.expanded(b)) if b else self.send_json({"error": "nf"}, 404)
        m = re.match(r"^/api/items/([^/]+)/play$", p)
        if m and method == "POST":
            self.body_json()
            b = self.book(m.group(1))
            if not b:
                return self.send_json({"error": "nf"}, 404)
            with lock:
                state["n"] += 1
                sid = "sess-%d" % state["n"]
                state["sessions"][sid] = b["id"]
                ct = state["progress"].get(b["id"], (0, 0))[0]
            return self.send_json({"id": sid, "userId": USER_ID, "libraryItemId": b["id"], "mediaType": "book",
                                   "mediaMetadata": {"title": b["title"], "authors": [{"id": "a", "name": b["author"]}]},
                                   "chapters": b["chapters"], "displayTitle": b["title"], "displayAuthor": b["author"],
                                   "duration": b["duration"], "playMethod": 0, "currentTime": ct,
                                   "audioTracks": self.audio_tracks(b),
                                   "libraryItem": self.expanded(b)})
        m = re.match(r"^/api/session/([^/]+)/(sync|close)$", p)
        if m and method == "POST":
            body = self.body_json()
            with lock:
                iid = state["sessions"].get(m.group(1))
                if iid and "currentTime" in body:
                    state["progress"][iid] = (float(body["currentTime"]), int(time.time() * 1000))
            return self.send_json({}) if iid else self.send_json({"error": "nf"}, 404)
        if p == "/api/session/local" and method == "POST":
            body = self.body_json()
            with lock:
                if body.get("libraryItemId"):
                    state["progress"][body["libraryItemId"]] = (float(body.get("currentTime", 0)),
                                                                int(time.time() * 1000))
            return self.send_json({})
        m = re.match(r"^/api/items/([^/]+)/file/([^/]+)$", p)
        if m:
            b = self.book(m.group(1))
            t = next((t for t in (b["tracks"] if b else []) if t["ino"] == m.group(2)), None)
            if not t:
                return self.send_json({"error": "nf"}, 404)
            return self.send_file(t["path"], t["mimeType"], drop=True)
        return self.send_json({"error": "not found"}, 404)

    def send_file(self, path, ctype, drop):
        size = os.path.getsize(path)
        start, end = 0, size - 1
        rng = self.headers.get("Range")
        status = 200
        if rng:
            m = re.match(r"bytes=(\d*)-(\d*)", rng)
            if m and m.group(1):
                start = int(m.group(1))
                if m.group(2):
                    end = min(int(m.group(2)), size - 1)
                status = 206
            if start >= size:
                self.send_response(416)
                self.send_header("Content-Range", "bytes */%d" % size)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
        n = end - start + 1
        cut = None
        if drop:
            with lock:
                cut, state["drop_after"] = state["drop_after"], None
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(n))
        if status == 206:
            self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end, size))
        self.end_headers()
        with open(path, "rb") as f:
            f.seek(start)
            left = n if cut is None else min(n, cut)
            try:
                while left > 0:
                    chunk = f.read(min(65536, left))
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    left -= len(chunk)
            except (BrokenPipeError, ConnectionResetError):
                return
        if cut is not None and cut < n:
            self.close_connection = True
            try:
                self.connection.shutdown(2)
            except OSError:
                pass

    def do_GET(self):
        self.handle_any("GET")

    def do_POST(self):
        self.handle_any("POST")

    def do_PATCH(self):
        self.handle_any("PATCH")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=13378)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--media", default=os.path.join(os.path.dirname(__file__), "..", "tests", "media"))
    ap.add_argument("--books", type=int, default=60, help="filler books")
    ap.add_argument("--showcase", help="books.json from tools/fetch_showcase.py (README screenshots)")
    a = ap.parse_args()
    Handler.media = os.path.abspath(a.media)
    if a.showcase:
        Handler.books = make_showcase(Handler.media, a.showcase)
        now = int(time.time() * 1000)
        for k, frac in ((0, 0.42), (5, 0.18), (8, 0.67)):  # some listening in progress
            if k < len(Handler.books):
                state["progress"][Handler.books[k]["id"]] = (Handler.books[k]["duration"] * frac, now - k * 60000)
    else:
        Handler.books = make_books(Handler.media, a.books)
    srv = ThreadingHTTPServer((a.host, a.port), Handler)
    srv.daemon_threads = True
    print("mock ABS on %s:%d with %d books" % (a.host, a.port, len(Handler.books)), flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
