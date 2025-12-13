#!/usr/bin/env python3
"""Example rethread key handler.

Installs a simple CLI that receives key information from rethread and triggers
custom actions. Exit with status 2 to consume the shortcut (so the browser
suppresses the default behavior). Exit 0/1 to let the key propagate.
"""

import argparse
import subprocess
import sys
from typing import List, Optional


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


def handle_key_event(args: argparse.Namespace) -> int:
    """Handle a single key event."""
    label = key_label(args)
    if args.ctrl and label == "e":
        # Launch "alert hi" asynchronously so we can return immediately.
        subprocess.Popen(["alert", "hi"])
        return 2
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Handle key events from rethread")
    parser.add_argument("event", choices=["key"], help="type of event payload")
    parser.add_argument("--type", default="", help="CEF key event type name")
    parser.add_argument("--windows-key-code", type=int, default=0)
    parser.add_argument("--native-key-code", type=int, default=0)
    parser.add_argument("--modifiers", type=int, default=0)
    parser.add_argument("--character", type=int, default=0)
    parser.add_argument("--unmodified-character", type=int, default=0)
    parser.add_argument("--key-label", default="")
    parser.add_argument("--ctrl", action="store_true")
    parser.add_argument("--shift", action="store_true")
    parser.add_argument("--alt", action="store_true")
    parser.add_argument("--command", action="store_true")
    parser.add_argument("--meta", action="store_true")
    parser.add_argument("--repeat", action="store_true")
    parser.add_argument("--system-key", action="store_true")
    return parser


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.event == "key":
        return handle_key_event(args)
    return 1


if __name__ == "__main__":
    sys.exit(main())
