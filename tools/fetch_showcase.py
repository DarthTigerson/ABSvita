#!/usr/bin/env python3
"""Fetches cover art for public-domain LibriVox audiobooks from archive.org into
tests/showcase/ (gitignored) and writes tests/showcase/books.json for
`tools/mock_abs.py --showcase`. Used for the README screenshots: LibriVox
recordings and their cover art are in the public domain."""
import json
import os
import urllib.parse
import urllib.request

OUT = "tests/showcase"
BOOKS = [
    # title query, display title, author, series
    ("A Study in Scarlet", "A Study in Scarlet", "Arthur Conan Doyle", "Sherlock Holmes #1"),
    ("The Sign of the Four", "The Sign of the Four", "Arthur Conan Doyle", "Sherlock Holmes #2"),
    ("The Adventures of Sherlock Holmes", "The Adventures of Sherlock Holmes", "Arthur Conan Doyle", "Sherlock Holmes #3"),
    ("The Hound of the Baskervilles", "The Hound of the Baskervilles", "Arthur Conan Doyle", "Sherlock Holmes #5"),
    ("Pride and Prejudice", "Pride and Prejudice", "Jane Austen", ""),
    ("Emma", "Emma", "Jane Austen", ""),
    ("Frankenstein", "Frankenstein", "Mary Shelley", ""),
    ("Dracula", "Dracula", "Bram Stoker", ""),
    ("The Time Machine", "The Time Machine", "H. G. Wells", ""),
    ("The War of the Worlds", "The War of the Worlds", "H. G. Wells", ""),
    ("The Wonderful Wizard of Oz", "The Wonderful Wizard of Oz", "L. Frank Baum", "Oz #1"),
    ("The Marvelous Land of Oz", "The Marvelous Land of Oz", "L. Frank Baum", "Oz #2"),
    ("Ozma of Oz", "Ozma of Oz", "L. Frank Baum", "Oz #3"),
    ("Treasure Island", "Treasure Island", "Robert Louis Stevenson", ""),
    ("Twenty Thousand Leagues Under the Sea", "Twenty Thousand Leagues Under the Sea", "Jules Verne", ""),
    ("Around the World in Eighty Days", "Around the World in Eighty Days", "Jules Verne", ""),
    ("Moby Dick", "Moby Dick", "Herman Melville", ""),
    ("Jane Eyre", "Jane Eyre", "Charlotte Brontë", ""),
    ("The Picture of Dorian Gray", "The Picture of Dorian Gray", "Oscar Wilde", ""),
]


def get_json(url):
    with urllib.request.urlopen(url, timeout=30) as r:
        return json.load(r)


def find_item(title):
    q = 'collection:librivoxaudio AND title:("%s")' % title
    url = "https://archive.org/advancedsearch.php?" + urllib.parse.urlencode(
        {"q": q, "fl[]": "identifier", "sort[]": "downloads desc", "rows": 5, "output": "json"})
    docs = get_json(url)["response"]["docs"]
    return [d["identifier"] for d in docs]


def best_cover(ident):
    meta = get_json("https://archive.org/metadata/" + ident)
    jpgs = [f for f in meta.get("files", []) if f.get("format") == "JPEG" and not f["name"].startswith("__")]
    if not jpgs:
        return None, None
    f = max(jpgs, key=lambda f: int(f.get("size", 0)))
    return f["name"], meta.get("metadata", {}).get("licenseurl", "")


def main():
    os.makedirs(OUT + "/covers", exist_ok=True)
    out = []
    for i, (query, title, author, series) in enumerate(BOOKS):
        cover = lic = ident = None
        for ident in find_item(query):
            cover, lic = best_cover(ident)
            if cover:
                break
        if not cover or not lic:
            print("skipped (no cover or no licence):", title)
            continue
        dest = "%s/covers/%02d.jpg" % (OUT, i)
        url = "https://archive.org/download/%s/%s" % (ident, urllib.parse.quote(cover))
        urllib.request.urlretrieve(url, dest)
        out.append({"title": title, "author": author, "series": series, "cover": os.path.abspath(dest),
                    "source": "https://archive.org/details/" + ident, "license": lic})
        print("%-40s %s  %s" % (title, ident, lic))
    json.dump(out, open(OUT + "/books.json", "w"), indent=1)


if __name__ == "__main__":
    main()
