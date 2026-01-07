#!/usr/bin/env python3
"""Interactive command menu for rethread."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Callable
from urllib.parse import urlparse


MenuAction = Callable[[], int]


def config_dir() -> Path:
  base = os.environ.get("XDG_CONFIG_HOME")
  if not base:
    base = os.path.join(Path.home(), ".config")
  return Path(base) / "rethread"


def refresh_page() -> int:
  subprocess.run(
      ["rethread", "eval", "--stdin"],
      input="window.location.reload()",
      text=True,
      check=True,
  )
  return 0


def get_active_tab_url() -> str:
  result = subprocess.run(
      ["rethread", "tabs", "list"],
      check=True,
      capture_output=True,
      text=True,
  )
  try:
    payload = json.loads(result.stdout)
  except json.JSONDecodeError as exc:
    raise RuntimeError("failed to parse tabs list JSON") from exc

  tabs = payload.get("tabs") or []
  for tab in tabs:
    if tab.get("active"):
      url = tab.get("url")
      if not url:
        break
      return url
  raise RuntimeError("no active tab URL available")


def append_host_to_whitelist(host: str, target_file: Path) -> None:
  target_file.parent.mkdir(parents=True, exist_ok=True)
  existing = set()
  if target_file.exists():
    with target_file.open("r", encoding="utf-8") as infile:
      existing = {line.strip() for line in infile if line.strip()}
  if host in existing:
    return
  with target_file.open("a", encoding="utf-8") as outfile:
    outfile.write(f"{host}\n")


def whitelist_current_host() -> int:
  url = get_active_tab_url()
  parsed = urlparse(url)
  host = parsed.hostname or parsed.netloc
  if not host:
    raise RuntimeError(f"unable to determine host from URL: {url}")

  target = config_dir() / "iframes-whitelist-menu.txt"
  append_host_to_whitelist(host, target)

  subprocess.run(
      ["rethread", "rules", "iframes", "--whitelist", "--append"],
      input=f"{host}\n",
      text=True,
      check=True,
  )
  subprocess.run(
      [
          "rethread",
          "tabstrip",
          "message",
          "--duration=2000",
          f"{host} >> iframe whitelist",
      ],
      check=False,
  )
  refresh_page()
  return 0


def right_click_handler_path() -> Path:
  return config_dir() / "right-click-handler.py"


def bind_right_click(save_dest: str) -> int:
  handler = right_click_handler_path()
  subprocess.run(
      [
          "rethread",
          "bind",
          "--context-menu",
          f"{handler} --save {save_dest}",
      ],
      check=True,
  )
  return 0


def set_right_click_clipboard() -> int:
  return bind_right_click("clipboard")


def set_right_click_tmpfile() -> int:
  return bind_right_click("/tmp/rethread-rclick")


def find_menu_binary() -> list[str] | None:
  candidates = [
      (["rofi", "-dmenu"], "rofi"),
      (["dmenu"], "dmenu"),
      (["bemenu"], "bemenu"),
      (["tofi"], "tofi"),
      (["menu", "x"], "menu"),
  ]
  for command, binary in candidates:
    if shutil.which(binary):
      return command
  return None


def show_command_menu(options: list[str]) -> str | None:
  menu_command = find_menu_binary()
  if not menu_command:
    message = (
        "rethread-command-menu: no menu program found!\n"
        "install rofi/dmenu/etc. or update the script for custom"
    )
    subprocess.run(
        ["rethread", "tabstrip", "message", "--duration=10000", message],
        check=False,
    )
    return None

  menu_input = "\n".join(options)
  result = subprocess.run(
      menu_command,
      input=menu_input,
      text=True,
      capture_output=True,
      check=False,
  )
  if result.returncode != 0:
    return None
  selection = result.stdout.strip()
  return selection if selection in options else None


def parse_args(argv: list[str]) -> argparse.Namespace:
  parser = argparse.ArgumentParser(
      description="Run a rethread helper command via a menu or CLI argument."
  )
  parser.add_argument(
      "command",
      nargs="?",
      help="Optional command to run directly instead of showing the menu.",
  )
  return parser.parse_args(argv)


def main(argv: list[str]) -> int:
  actions: dict[str, MenuAction] = {
      "refresh/reload the page": refresh_page,
      "add current host to iframe whitelist": whitelist_current_host,
      "right-click: copy to clipboard": set_right_click_clipboard,
      "right-click: append to /tmp/rethread-rclick": set_right_click_tmpfile,
  }

  args = parse_args(argv)
  selection = args.command or show_command_menu(list(actions.keys()))
  if not selection:
    return 1

  action = actions.get(selection)
  if not action:
    sys.stderr.write(
        f"Unknown command '{selection}'. Available: {', '.join(actions)}\n"
    )
    return 1

  try:
    return action()
  except subprocess.CalledProcessError as exc:
    sys.stderr.write(f"Command failed: {exc}\n")
    return exc.returncode
  except RuntimeError as exc:
    sys.stderr.write(f"{exc}\n")
    return 1


if __name__ == "__main__":
  raise SystemExit(main(sys.argv[1:]))
