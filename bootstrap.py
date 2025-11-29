#!/usr/bin/env python3
import os, sys, tarfile, urllib.request, pathlib

CEF_VERSION = "139.0.28+g55ab8a8+chromium-139.0.7258.139"
PLATFORM    = "linux64"  # this script is Linux-only by design
BASE_URL    = "https://cef-builds.spotifycdn.com"
TARBALL     = f"cef_binary_{CEF_VERSION}_{PLATFORM}.tar.bz2"

root = pathlib.Path(__file__).parent.resolve()
tdir = root / "third_party" / "cef"
tdir.mkdir(parents=True, exist_ok=True)

tar_path = tdir / TARBALL
dst_dir  = tdir / f"cef_binary_{CEF_VERSION}_{PLATFORM}"

if not tar_path.exists():
    url = f"{BASE_URL}/{TARBALL}"
    print(f"Downloading {url} -> {tar_path}")
    urllib.request.urlretrieve(url, tar_path)

if not dst_dir.exists():
    print(f"Extracting {tar_path} -> {tdir}")
    with tarfile.open(tar_path, "r:bz2") as tf:
        tf.extractall(tdir)

print("Done. CEF at:", dst_dir)

