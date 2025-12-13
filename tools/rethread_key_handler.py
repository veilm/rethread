#!/usr/bin/env python3
"""Example rethread key handler.

Installs a simple CLI that receives key information from rethread and triggers
custom actions. Exit with status 2 to consume the shortcut (so the browser
suppresses the default behavior). Exit 0/1 to let the key propagate.
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import List, Optional

DEFAULT_URL = "https://veilm.github.io/rethread/"
PEEK_DURATION_MS = 750
SCRIPT_PATH = Path(__file__).resolve()
ACTION_CHOICES = ("open-default", "next-tab", "prev-tab")


def _decode_printable(value: int) -> str:
    if value and 32 <= value <= 126:
        return chr(value)
    return ""


def _decode_control_character(value: int) -> str:
    # ASCII control characters map to letters by subtracting 96.
    if 1 <= value <= 26:
        return chr(value + 96)
    return ""


def key_label(args: argparse.Namespace) -> str:
    """Return a best-effort normalized label for the key."""
    if args.key_label:
        return args.key_label.lower()

    for value in (args.unmodified_character, args.character):
        printable = _decode_printable(value)
        if printable:
            return printable.lower()
        if args.ctrl:
            control = _decode_control_character(value)
            if control:
                return control

    if 65 <= args.windows_key_code <= 90:
        return chr(args.windows_key_code).lower()
    return ""


def _run_tabs_command(arguments: List[str], capture_output: bool = False) -> Optional[str]:
    """Run `rethread tabs ...` and optionally return stdout."""
    command = ["rethread", "tabs", *arguments]
    try:
        result = subprocess.run(
            command,
            check=True,
            capture_output=capture_output,
            text=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        return None
    return result.stdout if capture_output else ""


def _peek_tabstrip() -> bool:
    return _run_tabs_command(["tabstrip", "peek", str(PEEK_DURATION_MS)]) is not None


def _open_default_tab() -> bool:
    if _run_tabs_command(["open", DEFAULT_URL]) is None:
        return False
    return _peek_tabstrip()


def _fetch_tabs() -> Optional[List[dict]]:
    output = _run_tabs_command(["list"], capture_output=True)
    if not output:
        return None
    try:
        payload = json.loads(output)
    except json.JSONDecodeError:
        return None
    tabs = payload.get("tabs")
    if not isinstance(tabs, list):
        return None
    return tabs


def _switch_relative_tab(delta: int) -> bool:
    tabs = _fetch_tabs()
    if not tabs:
        return False
    active_index = next((i for i, tab in enumerate(tabs) if tab.get("active")), None)
    if active_index is None:
        return False
    target_index = (active_index + delta) % len(tabs)
    target_id = tabs[target_index].get("id")
    if not target_id:
        return False
    if _run_tabs_command(["switch", str(target_id)]) is None:
        return False
    return _peek_tabstrip()


def _trigger_async_action(action: str) -> bool:
    """Spawn this script in action mode so the UI thread is not blocked."""
    try:
        subprocess.Popen(
            [sys.executable, str(SCRIPT_PATH), "action", action],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return True
    except OSError:
        return False


def handle_action(action: str) -> int:
    if action == "open-default":
        return 0 if _open_default_tab() else 1
    if action == "next-tab":
        return 0 if _switch_relative_tab(1) else 1
    if action == "prev-tab":
        return 0 if _switch_relative_tab(-1) else 1
    return 1


def handle_key_event(args: argparse.Namespace) -> int:
    """Handle a single key event."""
    label = key_label(args)
    if not label:
        return 0

    is_ctrl = args.ctrl or args.meta or args.command
    # New tab shortcuts.
    if (is_ctrl and not args.alt and label == "t") or (
        args.alt and args.shift and not is_ctrl and label == "o"
    ):
        return 2 if _trigger_async_action("open-default") else 0

    # Next tab shortcuts.
    if (is_ctrl and not args.shift and label == "tab") or (
        args.alt and not is_ctrl and not args.shift and label == "j"
    ):
        return 2 if _trigger_async_action("next-tab") else 0

    # Previous tab shortcuts.
    if (is_ctrl and args.shift and label == "tab") or (
        args.alt and not is_ctrl and not args.shift and label == "k"
    ):
        return 2 if _trigger_async_action("prev-tab") else 0

    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Custom rethread key handler and actions")
    subparsers = parser.add_subparsers(dest="event", required=True)

    key_parser = subparsers.add_parser("key", help="Handle key event payloads")
    key_parser.add_argument("--type", default="", help="CEF key event type name")
    key_parser.add_argument("--windows-key-code", type=int, default=0)
    key_parser.add_argument("--native-key-code", type=int, default=0)
    key_parser.add_argument("--modifiers", type=int, default=0)
    key_parser.add_argument("--character", type=int, default=0)
    key_parser.add_argument("--unmodified-character", type=int, default=0)
    key_parser.add_argument("--key-label", default="")
    key_parser.add_argument("--ctrl", action="store_true")
    key_parser.add_argument("--shift", action="store_true")
    key_parser.add_argument("--alt", action="store_true")
    key_parser.add_argument("--command", action="store_true")
    key_parser.add_argument("--meta", action="store_true")
    key_parser.add_argument("--repeat", action="store_true")
    key_parser.add_argument("--system-key", action="store_true")

    action_parser = subparsers.add_parser("action", help="Execute tab actions asynchronously")
    action_parser.add_argument("name", choices=ACTION_CHOICES)
    return parser


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.event == "key":
        return handle_key_event(args)
    if args.event == "action":
        return handle_action(args.name)
    return 1


if __name__ == "__main__":
    sys.exit(main())
