#!/usr/bin/env python3
"""
Utility helper for managing cosmetic filters in rethread.

Default invocation launches the picker inside the active tab, records the
selection, writes it to cosmetic-filters.json, registers / refreshes the
per-site userscript, and reloads the tab.
"""
from __future__ import annotations

import argparse
import time
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional
from urllib.parse import urlparse

CONFIG_VERSION = 1
CONFIG_FILENAME = "cosmetic-filters.json"
SCRIPT_ID_PREFIX = "cosmetic-filter-"
TEMPLATE_MATCH_ANCHOR = "__RETHREAD_PICKER_MATCH__"
TEMPLATE_CONFIG_ANCHOR = "__RETHREAD_PICKER_CONFIG__"


def _default_config_dir() -> Path:
  root = os.environ.get("XDG_CONFIG_HOME")
  if not root:
    root = os.path.join(Path.home(), ".config")
  return Path(root) / "rethread"


class CosmeticFilterManager:
  def __init__(self, quiet: bool = False):
    self.quiet = quiet
    self.config_dir = _default_config_dir()
    self.config_path = self.config_dir / CONFIG_FILENAME
    self.template_path = Path(__file__).with_name("cosmetic-filtering-execute.js")
    self.picker_path = Path(__file__).with_name("cosmetic-filtering-picker.js")
    self.data = self._load_config()

  def log(self, message: str) -> None:
    if not self.quiet:
      print(message, file=sys.stderr)

  def _load_config(self) -> Dict[str, Any]:
    if not self.config_path.exists():
      return {"version": CONFIG_VERSION, "filters": {}}
    try:
      with self.config_path.open("r", encoding="utf-8") as fh:
        raw = json.load(fh)
    except Exception as exc:  # pylint: disable=broad-except
      self.log(f"Failed to parse {self.config_path}: {exc}")
      return {"version": CONFIG_VERSION, "filters": {}}
    filters = raw.get("filters")
    if not isinstance(filters, dict):
      filters = {}
    return {"version": raw.get("version", CONFIG_VERSION), "filters": filters}

  def _save_config(self) -> None:
    self.config_dir.mkdir(parents=True, exist_ok=True)
    tmp_path = self.config_path.with_suffix(".tmp")
    with tmp_path.open("w", encoding="utf-8") as fh:
      json.dump(
          {"version": CONFIG_VERSION, "filters": self.data.get("filters", {})},
          fh,
          indent=2,
          sort_keys=True)
      fh.write("\n")
    tmp_path.replace(self.config_path)

  def run(self) -> None:
    parser = argparse.ArgumentParser(description="Manage cosmetic filters")
    parser.add_argument(
        "-q",
        "--quiet",
        action="store_true",
        help="Suppress informational messages")
    sub = parser.add_subparsers(dest="command")

    sub.add_parser("sync", help="Re-register userscripts for all saved hosts")
    sub.add_parser("list", help="Show stored filters")

    rm_parser = sub.add_parser("rm", help="Remove a filter")
    rm_parser.add_argument("host", help="Host entry to edit")
    rm_parser.add_argument(
        "index",
        type=int,
        help="1-based index printed by the list command")

    add_parser = sub.add_parser(
        "add",
        help="Run the picker and save a cosmetic filter (default command)")
    add_parser.add_argument(
        "--host",
        help="Override host match (default = active tab hostname)")

    args = parser.parse_args()
    if args.quiet:
      self.quiet = True

    command = args.command or "add"
    if command == "sync":
      self.sync()
      return
    if command == "list":
      self.list_filters()
      return
    if command == "rm":
      self.remove_rule(args.host, args.index)
      return
    if command == "add":
      self.add_filter(getattr(args, "host", None))
      return
    parser.error(f"Unknown command: {command}")

  def add_filter(self, host_override: Optional[str]) -> None:
    # Determine target host
    if host_override:
      host = host_override.strip()
      self.log(f"Using host override: {host}")
    else:
      self.log("Detecting active tab...")
      active_tab = self._active_tab()
      if not active_tab:
        self.log("Unable to detect active tab – is the browser running?")
        return
      url = active_tab.get("url", "")
      host = urlparse(url).hostname or ""
      if not host:
        self.log(f"Active tab URL is not filterable: {url!r}")
        return
      self.log(f"Active tab host: {host}")

    self.log("Launching picker UI...")
    picker_result = self._run_picker()
    if picker_result is None:
      self.log("Picker cancelled")
      return

    selector = picker_result.get("selector", "").strip()
    if not selector:
      self.log("Picker did not provide a selector; aborting")
      return

    rule: Dict[str, Any] = {"selector": selector, "created_at": self._timestamp()}
    has_text = picker_result.get("hasText")
    if isinstance(has_text, str) and has_text.strip():
      rule["hasText"] = has_text.strip()

    self.log(f"Selected rule for {host}: {selector}"
             + (f" (text contains {rule['hasText']!r})" if "hasText" in rule else ""))

    if not self._append_rule(host, rule):
      self.log("An identical filter already exists; nothing to do")
      return

    if self._register_host(host):
      self._save_config()
      self.log(f"Saved filter for {host}: {selector}")
      self.log("Reloading active tab to apply filter...")
      self._reload_active_tab()

  def sync(self) -> None:
    filters = self.data.get("filters", {})
    if not filters:
      self.log("No cosmetic filters stored")
      return
    any_success = False
    for host, rules in sorted(filters.items()):
      if not isinstance(rules, list) or not rules:
        continue
      self.log(f"Registering stored filters for {host}...")
      if self._register_host(host):
        any_success = True
    if any_success:
      self.log("Refreshed cosmetic filter scripts")

  def list_filters(self) -> None:
    filters = self.data.get("filters", {})
    if not filters:
      print("No cosmetic filters saved")
      return
    for host in sorted(filters.keys()):
      print(host)
      rules = filters[host]
      if not isinstance(rules, list) or not rules:
        print("  (no rules)")
        continue
      for idx, rule in enumerate(rules, start=1):
        selector = rule.get("selector", "")
        text = rule.get("hasText")
        detail = f"[{idx}] {selector}"
        if text:
          detail += f" (text contains {text!r})"
        print(f"  {detail}")

  def remove_rule(self, host: str, index: int) -> None:
    self.log(f"Removing rule {index} for {host}...")
    filters = self.data.get("filters", {})
    rules = filters.get(host)
    if not isinstance(rules, list) or not rules:
      self.log(f"No rules stored for {host}")
      return
    if index < 1 or index > len(rules):
      self.log(f"Index {index} is out of range for {host}")
      return
    removed = rules.pop(index - 1)
    if not rules:
      filters.pop(host, None)
      self._remove_script(host)
      self.log(f"Removed last rule for {host}")
    else:
      filters[host] = rules
      self._register_host(host)
      self.log(f"Removed rule {index} for {host}")
    self._save_config()
    selector = removed.get("selector", "")
    if selector:
      self.log(f"Deleted selector: {selector}")

  def _append_rule(self, host: str, rule: Dict[str, Any]) -> bool:
    filters = self.data.setdefault("filters", {})
    rules = filters.setdefault(host, [])
    if not isinstance(rules, list):
      rules = []
    for existing in rules:
      if existing.get("selector") == rule.get("selector") and existing.get(
          "hasText") == rule.get("hasText"):
        self.log("Existing rule matches selector/text; skipping append")
        return False
    rules.append(rule)
    self.log(f"Appended rule #{len(rules)} for {host}")
    filters[host] = rules
    return True

  def _register_host(self, host: str) -> bool:
    filters = self.data.get("filters", {})
    rules = filters.get(host)
    if not isinstance(rules, list) or not rules:
      return False
    template = self._load_template()
    if template is None:
      return False
    match_pattern = f"*://{host}/*"
    config_blob = json.dumps(rules, ensure_ascii=False, indent=2)

    script_body = template
    if TEMPLATE_MATCH_ANCHOR not in script_body:
      self.log("Template does not contain expected match anchor")
      return False
    script_body = script_body.replace(TEMPLATE_MATCH_ANCHOR, match_pattern, 1)
    if TEMPLATE_CONFIG_ANCHOR not in script_body:
      self.log("Template does not contain expected config anchor")
      return False
    script_body = script_body.replace(TEMPLATE_CONFIG_ANCHOR, config_blob, 1)

    script_id = self._script_id(host)
    self.log(f"Registering userscript {script_id} ({match_pattern})...")
    result = self._run_rethread(["rethread", "scripts", "add", "--id", script_id],
                                input_data=script_body)
    if result is None:
      self.log(f"Failed to register userscript for {host}")
      return False
    return True

  def _remove_script(self, host: str) -> None:
    script_id = self._script_id(host)
    self.log(f"Removing userscript {script_id} for {host}...")
    self._run_rethread(["rethread", "scripts", "rm", "--id", script_id])

  def _active_tab(self) -> Optional[Dict[str, Any]]:
    output = self._run_rethread(["rethread", "tabs", "list"])
    if output is None:
      return None
    try:
      data = json.loads(output)
    except json.JSONDecodeError as exc:
      self.log(f"Failed to parse tabs list: {exc}")
      return None
    tabs = data.get("tabs")
    if isinstance(tabs, list):
      for tab in tabs:
        if tab.get("active"):
          return tab
    return None

  def _run_picker(self) -> Optional[Dict[str, Any]]:
    try:
      script = self.picker_path.read_text(encoding="utf-8")
    except OSError as exc:
      self.log(f"Unable to read picker script: {exc}")
      return None
    output = self._run_rethread(["rethread", "eval", "--stdin"], input_data=script)
    if output is None:
      return None
    trimmed = output.strip()
    if not trimmed or trimmed == "null":
      self.log("Picker returned empty payload")
      return None
    if trimmed.startswith("ERR"):
      self.log(trimmed)
      return None
    try:
      return json.loads(trimmed)
    except json.JSONDecodeError as exc:
      self.log(f"Picker returned invalid JSON: {exc}")
      return None

  def _reload_active_tab(self) -> None:
    self.log("Triggering location.reload() in active tab")
    self._run_rethread(["rethread", "eval", "window.location.reload()"])

  def _script_id(self, host: str) -> str:
    slug = re.sub(r"[^a-zA-Z0-9._-]+", "-", host)
    return f"{SCRIPT_ID_PREFIX}{slug}"

  def _load_template(self) -> Optional[str]:
    try:
      return self.template_path.read_text(encoding="utf-8")
    except OSError as exc:
      self.log(f"Unable to read cosmetic filter template: {exc}")
      return None

  def _run_rethread(self,
                    argv: List[str],
                    input_data: Optional[str] = None) -> Optional[str]:
    self.log(f"Running command: {' '.join(argv)}")
    try:
      result = subprocess.run(
          argv,
          input=input_data,
          text=True,
          capture_output=True,
          check=False)
    except FileNotFoundError:
      self.log("rethread command not found in PATH")
      return None
    if result.returncode != 0:
      stderr = result.stderr.strip()
      if stderr:
        self.log(stderr)
      self.log(f"Command failed with exit code {result.returncode}")
      return None
    stdout = result.stdout
    if stdout.strip().startswith("ERR"):
      self.log(stdout.strip())
      return None
    return stdout

  def _timestamp(self) -> str:
    return str(int(time.time()))


def main() -> None:
  manager = CosmeticFilterManager()
  manager.run()


if __name__ == "__main__":
  main()
