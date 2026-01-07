#!/usr/bin/env python3
"""
Basic download handler used by rethread.

Reads JSON metadata about the pending download from stdin and prints a JSON
object describing the decision (currently just the resolved file path). Users
can customize or replace this script in $XDG_CONFIG_HOME/rethread/.
"""

from __future__ import annotations

import json
import os
import pathlib
import sys
from typing import Any, Dict


def _read_payload() -> Dict[str, Any]:
  raw = sys.stdin.read()
  if not raw:
    return {}
  try:
    return json.loads(raw)
  except json.JSONDecodeError:
    return {}


def _downloads_dir() -> pathlib.Path:
  override = os.environ.get("XDG_DOWNLOAD_DIR")
  if override:
    return pathlib.Path(os.path.expanduser(override))
  home = os.environ.get("HOME", "")
  base = home if home else "."
  return pathlib.Path(base).expanduser() / "Downloads"


def _sanitize_filename(name: str) -> str:
  cleaned = "".join(ch for ch in (name or "") if ch not in ("/", "\\"))
  cleaned = cleaned.strip()
  return cleaned or "download"


def _allocate_path(directory: pathlib.Path, filename: str) -> pathlib.Path:
  directory = directory.expanduser()
  directory.mkdir(parents=True, exist_ok=True)
  candidate = directory / filename
  if not candidate.exists():
    return candidate
  stem = candidate.stem or "download"
  suffix = "".join(candidate.suffixes)
  for index in range(1, 1000):
    attempt = directory / f"{stem}-{index}{suffix}"
    if not attempt.exists():
      return attempt
  return candidate


def main() -> int:
  payload = _read_payload()
  directory_value = payload.get("download_directory") or payload.get(
      "download_dir")
  directory = pathlib.Path(directory_value) if directory_value else _downloads_dir()
  filename = (
      payload.get("download_file_name")
      or payload.get("suggested_file_name")
      or payload.get("suggestedFileName")
  )
  resolved_name = _sanitize_filename(str(filename) if filename else "")
  target_path = _allocate_path(directory, resolved_name)
  decision = {"accept": True, "path": str(target_path)}
  json.dump(decision, sys.stdout)
  sys.stdout.write("\n")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
