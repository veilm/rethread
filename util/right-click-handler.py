#!/usr/bin/env python3
"""Sample right-click handler for rethread.

Copies the media/source URL under the cursor into the Wayland clipboard.
"""

from __future__ import annotations

import os
import subprocess
import sys
from urllib.parse import unquote


def main() -> int:
  source = (
      os.environ.get("RETHREAD_CONTEXT_SOURCE_URL")
      or os.environ.get("RETHREAD_MENU_SOURCE_URL"))
  if not source:
    return 0

  decoded = unquote(source)
  try:
    subprocess.run(
        ["wl-copy"], input=decoded, text=True, check=True)
  except FileNotFoundError:
    sys.stderr.write("wl-copy is not installed or not in PATH\n")
    return 1
  except subprocess.CalledProcessError as error:
    sys.stderr.write(f"wl-copy failed with exit code {error.returncode}\n")
    return error.returncode
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
