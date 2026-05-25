import json
import os
import sys
import threading
from enum import Enum
from typing import Optional, TextIO


class Color(Enum):
    RESET = "\033[0m"
    BOLD = "\033[1m"
    RED = "\033[91m"
    GREEN = "\033[92m"
    YELLOW = "\033[93m"
    BLUE = "\033[94m"
    CYAN = "\033[96m"
    GRAY = "\033[90m"


def _supports_color(stream: TextIO) -> bool:
    if not hasattr(stream, "isatty"):
        return False
    if not stream.isatty():
        return False
    if sys.platform == "win32":
        return bool(os.environ.get("TERM") or os.environ.get("WT_SESSION"))
    return True


class Console:
    BANNER = """
    _   __            __
   / | / /___  ____  / /__
  /  |/ / __ \\/ __ \\/ //_/
 / /|  / /_/ / /_/ / ,<
/_/ |_/\\____/\\____/_/|_|  v{version}

 Dynamic instrumentation toolkit for Android
"""

    def __init__(self, stdout: Optional[TextIO] = None, stderr: Optional[TextIO] = None, color: Optional[bool] = None):
        self.stdout = stdout or sys.stdout
        self.stderr = stderr or sys.stderr
        self._color = _supports_color(self.stdout) if color is None else color
        self._script_origin = None
        self._prompt_active = False
        self._io_lock = threading.RLock()

    def _c(self, color: Color, text: str) -> str:
        if not self._color:
            return text
        return f"{color.value}{text}{Color.RESET.value}"

    def info(self, message: str) -> None:
        with self._io_lock:
            self._ensure_fresh_output_line(self.stdout)
            print(f"{self._c(Color.BLUE, '[*]')} {message}", file=self.stdout)

    def success(self, message: str) -> None:
        with self._io_lock:
            self._ensure_fresh_output_line(self.stdout)
            print(f"{self._c(Color.GREEN, '[+]')} {message}", file=self.stdout)

    def warning(self, message: str) -> None:
        with self._io_lock:
            self._ensure_fresh_output_line(self.stderr)
            print(f"{self._c(Color.YELLOW, '[!]')} {message}", file=self.stderr)

    def error(self, message: str) -> None:
        with self._io_lock:
            self._ensure_fresh_output_line(self.stderr)
            print(f"{self._c(Color.RED, '[-]')} {message}", file=self.stderr)

    def rpc_result(self, method: str, result) -> None:
        with self._io_lock:
            self._ensure_fresh_output_line(self.stdout)
            print(f"{self._c(Color.GREEN, '[rpc]')} {method} => {json.dumps(result, ensure_ascii=False)}",
                  file=self.stdout)

    def log(self, message: str) -> None:
        with self._io_lock:
            self._ensure_fresh_output_line(self.stdout)
            print(message, file=self.stdout)

    def bind_script_origin(self, device_name: str, process_name: str) -> None:
        self._script_origin = f"[{device_name}::{process_name}]"

    def clear_script_origin(self) -> None:
        self._script_origin = None

    def _script_line_prefix(self) -> str:
        return f"{self._script_origin} " if self._script_origin else ""

    def mark_prompt_active(self) -> None:
        with self._io_lock:
            self._prompt_active = True

    def clear_prompt_active(self) -> None:
        with self._io_lock:
            self._prompt_active = False

    def write_prompt(self, prompt: str) -> None:
        with self._io_lock:
            self._prompt_active = True
            self.stdout.write(prompt)
            self.stdout.flush()

    def _ensure_fresh_output_line(self, stream: TextIO) -> None:
        if self._prompt_active:
            print(file=stream)
            self._prompt_active = False

    def _ensure_fresh_script_line(self, stream: TextIO) -> None:
        self._ensure_fresh_output_line(stream)

    def script_message(self, payload: str, script_id: Optional[int] = None) -> None:
        with self._io_lock:
            self._ensure_fresh_script_line(self.stdout)
            print(f"{self._script_line_prefix()}{payload}", file=self.stdout)

    def raw_script_message(self, script_id: int, message: str, data_len: int) -> None:
        with self._io_lock:
            self._ensure_fresh_output_line(self.stdout)
            print(f"script message: script_id={script_id} json={message} data_len={data_len}", file=self.stdout)

    def format_prompt(self, context) -> str:
        device_name = getattr(context, "device_name", "Local")
        process_name = context.session.process_name
        prompt = f"[{device_name}::{process_name}]"
        if not getattr(context, "resumed", True):
            prompt += " " + self._c(Color.YELLOW, "(suspended)")
        return f"{prompt}-> "

    def print_repl_help(self) -> None:
        print(file=self.stdout)
        print(self._c(Color.BOLD, "Commands:"), file=self.stdout)
        commands = [
            ("/help", "Show this help message"),
            ("/info", "Display session information"),
            ("/load <path>", "Load a script file"),
            ("/reload", "Reload the current script"),
            ("/unload", "Unload the current script"),
            ("/post <msg>", "Send a message to the script"),
            ("/call <method> [args]", "Call an RPC export"),
            ("/resume", "Resume a spawned process"),
            ("/exit", "Exit the REPL"),
        ]
        for command, description in commands:
            print(f"    {self._c(Color.CYAN, command):<28} {description}", file=self.stdout)
        print(file=self.stdout)

    def print_banner(self, version: str) -> None:
        banner = self.BANNER.format(version=version)
        if self._color:
            print(self._c(Color.CYAN, banner), file=self.stdout)
        else:
            print(banner, file=self.stdout)

    def print_apps(self, apps) -> None:
        self.info(f"Found {len(apps)} application(s)")
        print(file=self.stdout)
        header = f"{'PID':>7}  {'Name':<40}  {'Identifier'}"
        print(self._c(Color.BOLD, header), file=self.stdout)
        print("-" * 70, file=self.stdout)
        for app in apps:
            pid = getattr(app, "pid", "-")
            name = getattr(app, "name", "-")
            identifier = getattr(app, "package_name", "-")
            if name == "-" and identifier != "-":
                name = identifier
            print(f"{str(pid):>7}  {str(name)[:40]:<40}  {identifier}", file=self.stdout)

    def print_processes(self, processes) -> None:
        self.info(f"Found {len(processes)} process(es)")
        print(file=self.stdout)
        header = f"{'PID':>7}  {'Name'}"
        print(self._c(Color.BOLD, header), file=self.stdout)
        print("-" * 50, file=self.stdout)
        for process in processes:
            print(f"{process.pid:>7}  {process.name}", file=self.stdout)

    def print_script_message_event(self, message) -> None:
        try:
            decoded = json.loads(message.message)
        except (TypeError, ValueError, json.JSONDecodeError):
            self.raw_script_message(message.script_id, message.message, len(message.data))
            return

        if not isinstance(decoded, dict):
            self.raw_script_message(message.script_id, message.message, len(message.data))
            return

        msg_type = decoded.get("type")
        payload = decoded.get("payload")
        if msg_type == "send" and isinstance(payload, str):
            self.script_message(payload, message.script_id)
            return
        if msg_type == "log" and isinstance(payload, str):
            level = decoded.get("level")
            if level == "warn":
                with self._io_lock:
                    self._ensure_fresh_script_line(self.stderr)
                    print(f"{self._script_line_prefix()}{self._c(Color.YELLOW, '[!]')} {payload}", file=self.stderr)
            elif level == "error":
                with self._io_lock:
                    self._ensure_fresh_script_line(self.stderr)
                    print(f"{self._script_line_prefix()}{self._c(Color.RED, '[-]')} {payload}", file=self.stderr)
            else:
                with self._io_lock:
                    self._ensure_fresh_script_line(self.stdout)
                    print(f"{self._script_line_prefix()}{payload}", file=self.stdout)
            return
        if msg_type == "error" and isinstance(payload, str):
            with self._io_lock:
                self._ensure_fresh_script_line(self.stderr)
                print(f"{self._script_line_prefix()}{self._c(Color.RED, '[-]')} {payload}", file=self.stderr)
            return
        self.raw_script_message(message.script_id, message.message, len(message.data))
