#!/usr/bin/env python3
"""Sample right-click handler for rethread.

Copies the link/media/source URL under the cursor into the Wayland clipboard.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote


def parse_args(argv: list[str]) -> argparse.Namespace:
  parser = argparse.ArgumentParser(
      description="Handle rethread right-click context actions."
  )
  parser.add_argument(
      "--save",
      default="clipboard",
      help="Save destination: 'clipboard' or a relative/absolute path.",
  )
  return parser.parse_args(argv)


def write_to_clipboard(value: str) -> int:
  try:
    subprocess.run(["wl-copy"], input=value, text=True, check=True)
  except FileNotFoundError:
    sys.stderr.write("wl-copy is not installed or not in PATH\n")
    return 1
  except subprocess.CalledProcessError as error:
    sys.stderr.write(f"wl-copy failed with exit code {error.returncode}\n")
    return error.returncode
  return 0


def append_to_file(value: str, destination: str) -> int:
  if not (destination.startswith(".") or destination.startswith("/")):
    sys.stderr.write("--save must be 'clipboard' or a relative/absolute path\n")
    return 1
  path = Path(destination)
  if not path.is_absolute():
    path = Path.cwd() / path
  path.parent.mkdir(parents=True, exist_ok=True)
  with path.open("a", encoding="utf-8") as outfile:
    outfile.write(f"{value}\n")
  return 0


def show_message(message: str) -> None:
  subprocess.run(
      ["rethread", "tabstrip", "message", "--duration=800", message],
      check=False,
  )


def main(argv: list[str]) -> int:
  source = (
      os.environ.get("RETHREAD_CONTEXT_LINK_URL")
      or os.environ.get("RETHREAD_CONTEXT_SOURCE_URL")
      or os.environ.get("RETHREAD_MENU_SOURCE_URL")
  )
  if not source:
    return 0

  args = parse_args(argv)
  decoded = unquote(source)
  if args.save == "clipboard":
    result = write_to_clipboard(decoded)
    if result == 0:
      show_message("Copied URL")
    return result
  result = append_to_file(decoded, args.save)
  if result == 0:
    show_message(f">> {args.save}")
  return result


if __name__ == "__main__":
  raise SystemExit(main(sys.argv[1:]))
