#!/usr/bin/env python3
"""Build the SD-card zip with the Quake II shareware demo data.

    tools/make_demo_zip.py <out.zip> [q2-314-demo-x86.exe]

Without an installer path, q2-314-demo-x86.exe (id Software's 1998 demo, a
self-extracting zip) is downloaded from a mirror into a cache directory and reused
from there. Either way the installer must match its SHA-256.

The zip holds the demo's data files unmodified, laid out for the SD card, plus the
demo's licence agreement. That agreement allows the demo to be distributed free of
charge and by electronic means only, as long as the agreement accompanies it at all
times, so it goes in twice: at the top of the zip, and next to the data where it
travels onto the card. The Windows executables and DLLs are left out.

    quake2-demo-license.txt
    roms/quake2/license.txt
    roms/quake2/baseq2/pak0.pak
    roms/quake2/baseq2/players/...
"""
import hashlib
import os
import sys
import tempfile
import urllib.request
import zipfile

INSTALLER = "q2-314-demo-x86.exe"
SHA256 = "7ace5a43983f10d6bdc9d9b6e17a1032ba6223118d389bd170df89b945a04a1e"
MIRRORS = [
    "https://ftp.gwdg.de/pub/misc/ftp.idsoftware.com/idstuff/quake2/" + INSTALLER,
    "https://deponie.yamagi.org/quake2/idstuff/" + INSTALLER,
]

LICENSE = "Install/Data/DOCS/license.txt"
BASEQ2 = "Install/Data/baseq2/"
KEEP = ("pak0.pak", "players/")     # under BASEQ2; gamex86.dll is not needed


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def cached_installer():
    cache = os.path.join(os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache"),
                         "pico-quake2")
    path = os.path.join(cache, INSTALLER)
    if os.path.isfile(path) and sha256(path) == SHA256:
        return path
    os.makedirs(cache, exist_ok=True)
    for url in MIRRORS:
        print(f"downloading {url}")
        fd, tmp = tempfile.mkstemp(dir=cache, suffix=".part")
        try:
            with os.fdopen(fd, "wb") as out, urllib.request.urlopen(url, timeout=60) as r:
                while block := r.read(1 << 20):
                    out.write(block)
            if sha256(tmp) == SHA256:
                os.replace(tmp, path)
                return path
            print("  SHA-256 mismatch, discarded", file=sys.stderr)
        except OSError as e:
            print(f"  failed: {e}", file=sys.stderr)
        if os.path.exists(tmp):
            os.remove(tmp)
    sys.exit("no mirror delivered " + INSTALLER)


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__.split("\n\n")[1])
    out = sys.argv[1]
    exe = sys.argv[2] if len(sys.argv) == 3 else cached_installer()
    if sha256(exe) != SHA256:
        sys.exit(f"{exe}: SHA-256 is not that of {INSTALLER}")

    with zipfile.ZipFile(exe) as src, \
            zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as dst:
        text = src.read(LICENSE)
        dst.writestr("quake2-demo-license.txt", text)
        dst.writestr("roms/quake2/license.txt", text)
        count = 0
        for info in src.infolist():
            name = info.filename
            if info.is_dir() or not name.startswith(BASEQ2):
                continue
            rel = name[len(BASEQ2):]
            if rel.startswith(KEEP):
                dst.writestr("roms/quake2/baseq2/" + rel, src.read(info))
                count += 1
    print(f"{out}: {count} data files and the demo licence")


if __name__ == "__main__":
    main()
