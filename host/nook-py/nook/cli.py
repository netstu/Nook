import argparse
import json
import os
import sys
import time
import threading
from types import SimpleNamespace
from typing import Callable, Optional, Sequence

from . import __version__
from .core import get_device, get_usb_device
from .output import Console

_KNOWN_SUBCOMMANDS = {
    "apps",
    "ps",
    "spawn",
    "attach",
    "call",
    "detach",
    "resume",
    "post",
    "unload",
    "repl",
}

_FRIDA_STYLE_HELP = """Frida-style interactive usage:

  nook-cli -U -f com.demo.target -l hook.js
  nook-cli -U com.demo.target -l hook.js
  nook-cli -U -n com.demo.target -l hook.js
  nook-cli -U -N com.demo.target -l hook.js
  nook-cli -U -p 4321 -l hook.js

Experimental spawn routing:

  nook-cli -U -f com.demo.target -l hook.js --strict-zygote-control
  nook-cli -U -f com.demo.target -l hook.js --symbi

Legacy command-mode usage:
"""

_SPAWN_SYMBI_MARKER = "--nook-spawn-backend=symbi"
_STRICT_ZYGOTE_CONTROL_MARKER = "--nook-strict-zygote-control"


def _add_spawn_backend_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--symbi",
        dest="spawn_symbi",
        action="store_true",
        help="experimental: prefer the symbi spawn backend",
    )
    parser.add_argument(
        "--strict-zygote-control",
        action="store_true",
        help="experimental: attempt zygote-control first",
    )


def _default_device_factory(host: str, port: int, timeout_ms: int):
    return get_device(host=host, port=port, timeout_ms=timeout_ms)


def _default_usb_device_factory(local_port: int, remote_port: int, timeout_ms: int, serial=None):
    return get_usb_device(
        local_port=local_port,
        remote_port=remote_port,
        timeout_ms=timeout_ms,
        serial=serial,
    )


def _add_connection_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--usb", action="store_true")
    parser.add_argument("--serial")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=27042)
    parser.add_argument("--timeout", type=int, default=5000)
    parser.add_argument("--json", action="store_true")


class _TopLevelCliParser(argparse.ArgumentParser):
    def __init__(self, legacy_parser: argparse.ArgumentParser, frida_parser: argparse.ArgumentParser) -> None:
        super().__init__(prog="nook-cli", add_help=False)
        self._legacy_parser = legacy_parser
        self._frida_parser = frida_parser

    def parse_args(self, args=None, namespace=None):
        argv = list(sys.argv[1:] if args is None else args)
        if not argv or argv[0] in _KNOWN_SUBCOMMANDS:
            return self._legacy_parser.parse_args(argv, namespace)
        if argv[0] in ("-h", "--help"):
            self.print_help()
            self.exit(0)
        return self._frida_parser.parse_args(argv, namespace)

    def format_help(self) -> str:
        return _FRIDA_STYLE_HELP + "\n" + self._legacy_parser.format_help()


def _build_legacy_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="nook-cli")
    subparsers = parser.add_subparsers(dest="command", required=True)

    apps = subparsers.add_parser("apps")
    _add_connection_args(apps)

    processes = subparsers.add_parser("ps")
    _add_connection_args(processes)

    spawn = subparsers.add_parser("spawn")
    _add_connection_args(spawn)
    spawn.add_argument("package")
    spawn.add_argument("-l", "--load", dest="script_path")
    spawn.add_argument("--oneshot", action="store_true")
    spawn.add_argument("--resume", action="store_true")
    spawn.add_argument("--wait", action="store_true")
    spawn.add_argument("--interactive", action="store_true")
    spawn.add_argument("--call")
    spawn.add_argument("--call-args", default="[]")
    spawn.add_argument("--agent-ready-timeout", type=int, default=10000)
    spawn.add_argument("--message-timeout", type=int, default=1000)
    _add_spawn_backend_args(spawn)

    attach = subparsers.add_parser("attach")
    _add_connection_args(attach)
    attach.add_argument("target")
    attach.add_argument("-l", "--load", dest="script_path")
    attach.add_argument("--oneshot", action="store_true")
    attach.add_argument("--wait", action="store_true")
    attach.add_argument("--interactive", action="store_true")
    attach.add_argument("--call")
    attach.add_argument("--call-args", default="[]")
    attach.add_argument("--message-timeout", type=int, default=1000)

    call = subparsers.add_parser("call")
    _add_connection_args(call)
    call.add_argument("target")
    call.add_argument("-l", "--load", dest="script_path")
    call.add_argument("method")
    call.add_argument("--call-args", default="[]")
    call.add_argument("--spawn", action="store_true")
    call.add_argument("--attach", action="store_true")
    call.add_argument("--resume", action="store_true")
    call.add_argument("--agent-ready-timeout", type=int, default=10000)
    _add_spawn_backend_args(call)

    detach = subparsers.add_parser("detach")
    _add_connection_args(detach)
    detach.add_argument("session_id", type=int)

    resume = subparsers.add_parser("resume")
    _add_connection_args(resume)
    resume.add_argument("pid", type=int)

    post = subparsers.add_parser("post")
    _add_connection_args(post)
    post.add_argument("package")
    post.add_argument("message")
    post.add_argument("-l", "--load", dest="script_path")
    post.add_argument("--agent-ready-timeout", type=int, default=10000)
    post.add_argument("--message-timeout", type=int, default=5000)
    _add_spawn_backend_args(post)

    unload = subparsers.add_parser("unload")
    _add_connection_args(unload)
    unload.add_argument("package")
    unload.add_argument("script_path")
    unload.add_argument("--agent-ready-timeout", type=int, default=10000)
    _add_spawn_backend_args(unload)

    repl = subparsers.add_parser("repl")
    repl_subparsers = repl.add_subparsers(dest="repl_mode", required=True)

    repl_spawn = repl_subparsers.add_parser("spawn")
    _add_connection_args(repl_spawn)
    repl_spawn.add_argument("package")
    repl_spawn.add_argument("-l", "--load", dest="script_path")
    repl_spawn.add_argument("--resume", action="store_true")
    repl_spawn.add_argument("--agent-ready-timeout", type=int, default=10000)
    repl_spawn.add_argument("--message-timeout", type=int, default=1000)
    _add_spawn_backend_args(repl_spawn)

    repl_attach = repl_subparsers.add_parser("attach")
    _add_connection_args(repl_attach)
    repl_attach.add_argument("target")
    repl_attach.add_argument("-l", "--load", dest="script_path")
    repl_attach.add_argument("--message-timeout", type=int, default=1000)

    return parser


def _build_frida_style_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="nook-cli", add_help=True)
    parser.add_argument("-U", "--usb", action="store_true")
    parser.add_argument("--serial")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=27042)
    parser.add_argument("--timeout", type=int, default=5000)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("-f", dest="package")
    parser.add_argument("-n", "--attach-name", dest="attach_target")
    parser.add_argument("-N", "--attach-identifier", dest="attach_target")
    parser.add_argument("-p", "--attach-pid", dest="attach_target", type=int)
    parser.add_argument("-l", "--load", dest="script_path")
    parser.add_argument("--agent-ready-timeout", type=int, default=10000)
    parser.add_argument("--message-timeout", type=int, default=1000)
    _add_spawn_backend_args(parser)
    parser.add_argument("target", nargs="?")
    return parser


def _normalize_frida_style_args(parsed) -> argparse.Namespace:
    attach_target = parsed.attach_target if getattr(parsed, "attach_target", None) is not None else parsed.target

    if parsed.package:
        return argparse.Namespace(
            command="repl",
            repl_mode="spawn",
            usb=parsed.usb,
            serial=parsed.serial,
            host=parsed.host,
            port=parsed.port,
            timeout=parsed.timeout,
            json=parsed.json,
            package=parsed.package,
            script_path=parsed.script_path,
            resume=True,
            agent_ready_timeout=parsed.agent_ready_timeout,
            message_timeout=parsed.message_timeout,
            spawn_symbi=parsed.spawn_symbi,
            strict_zygote_control=parsed.strict_zygote_control,
        )

    if attach_target is not None:
        return argparse.Namespace(
            command="repl",
            repl_mode="attach",
            usb=parsed.usb,
            serial=parsed.serial,
            host=parsed.host,
            port=parsed.port,
            timeout=parsed.timeout,
            json=parsed.json,
            target=attach_target,
            script_path=parsed.script_path,
            message_timeout=parsed.message_timeout,
            spawn_symbi=parsed.spawn_symbi,
            strict_zygote_control=parsed.strict_zygote_control,
        )

    raise SystemExit("nook-cli: top-level mode requires either -f <package> or <target>")


def _build_parser() -> argparse.ArgumentParser:
    legacy_parser = _build_legacy_parser()
    frida_parser = _build_frida_style_parser()

    original_frida_parse_args = frida_parser.parse_args

    def parse_frida_args(args=None, namespace=None):
        return _normalize_frida_style_args(original_frida_parse_args(args, namespace))

    frida_parser.parse_args = parse_frida_args  # type: ignore[method-assign]
    return _TopLevelCliParser(legacy_parser, frida_parser)


def _read_script_source(script_path: str):
    with open(script_path, "r", encoding="utf-8") as handle:
        return handle.read(), os.path.basename(script_path)


def _is_timeout_error(exc: BaseException) -> bool:
    return isinstance(exc, TimeoutError) or (str(exc) == "operation timed out")


def _format_stage_error(stage: str, exc: BaseException, subject: Optional[str] = None) -> str:
    detail = str(exc) or exc.__class__.__name__
    if _is_timeout_error(exc):
        if subject:
            return f"{stage} timed out for '{subject}': {detail}"
        return f"{stage} timed out: {detail}"
    if subject:
        return f"{stage} failed for '{subject}': {detail}"
    return f"{stage} failed: {detail}"


def _create_device(args, device_factory, usb_device_factory):
    if args.usb:
        return usb_device_factory(
            local_port=args.port,
            remote_port=args.port,
            timeout_ms=args.timeout,
            serial=args.serial,
        )
    return device_factory(host=args.host, port=args.port, timeout_ms=args.timeout)


def _emit_json(stream, payload) -> None:
    print(json.dumps(payload, ensure_ascii=False), file=stream)


def _emit_error(stream, message: str, use_json: bool, console: Optional[Console] = None) -> None:
    if use_json:
        _emit_json(stream, {"ok": False, "error": message})
        return
    (console or Console(stdout=stream, stderr=stream)).error(message)


def _format_connection_guidance(message: str) -> str:
    return (
        f"{message}\n"
        "nook-cli requires a running `nook-server` on a rooted Android device.\n"
        "Download `nook-server` from the GitHub Release for this version, then deploy it manually:\n"
        "  adb push .\\nook-server /data/local/tmp/nook/nook-server\n"
        "  adb shell \"su -c 'chmod 755 /data/local/tmp/nook/nook-server'\"\n"
        "  adb shell \"su -c '/data/local/tmp/nook/nook-server'\"\n"
        "Then rerun `nook-cli`. Also ensure `adb devices` lists the target device."
    )


def _rewrite_connection_error(message: str) -> str:
    lowered = message.lower()
    if any(marker in lowered for marker in ("socket", "connection refused", "adb", "device", "closed")):
        return _format_connection_guidance(message)
    return message


def _load_script(session, script_path: str, console: Optional[Console] = None):
    source, script_name = _read_script_source(script_path)
    if console is not None:
        console.info("Loading '%s'..." % script_name)
    try:
        script = session.create_script(source, name=script_name)
        script_id = script.create()
    except Exception as exc:
        raise type(exc)(_format_stage_error("script create", exc, script_name)) from exc
    try:
        script.load()
    except Exception as exc:
        raise type(exc)(_format_stage_error("script load", exc, script_name)) from exc
    if console is not None:
        console.success("Script loaded (id: %d)" % script_id)
    return script, script_id


def _build_spawn_argv(spawn_symbi: bool, strict_zygote_control: bool = False):
    argv = []
    if spawn_symbi:
        argv.append(_SPAWN_SYMBI_MARKER)
    if strict_zygote_control:
        argv.append(_STRICT_ZYGOTE_CONTROL_MARKER)
    return argv


def _is_attach_mode(args) -> bool:
    if args.command == "attach":
        return True
    if args.command == "repl":
        return getattr(args, "repl_mode", None) == "attach"
    if args.command == "call":
        return bool(getattr(args, "attach", False)) and not bool(getattr(args, "spawn", False))
    return False


def _validate_spawn_backend_flags(args) -> None:
    if not _is_attach_mode(args):
        return
    if getattr(args, "spawn_symbi", False):
        raise ValueError("--symbi is only valid for spawn")
    if getattr(args, "strict_zygote_control", False):
        raise ValueError("--strict-zygote-control is only valid for spawn")


def _spawn_session(device, package: str, agent_ready_timeout_ms: int, argv=None,
                   console: Optional[Console] = None):
    if console is not None:
        console.info("Waiting for agent runtime ready...")
    try:
        return device.spawn(
            package,
            argv=list(argv or []),
            agent_ready_timeout_ms=agent_ready_timeout_ms,
        )
    except Exception as exc:
        raise type(exc)(_format_stage_error("spawn agent-ready", exc, package)) from exc


def _attach_session(device, target, console: Optional[Console] = None):
    if console is not None:
        console.info("Waiting for agent runtime ready...")
    try:
        return device.attach(target)
    except Exception as exc:
        raise type(exc)(_format_stage_error("attach", exc, str(target))) from exc


def _resume_session(device, pid: int, subject: str):
    try:
        return device.resume(pid)
    except Exception as exc:
        raise type(exc)(_format_stage_error("resume", exc, subject)) from exc


def _emit_script_message(stdout, message, use_json: bool, console: Optional[Console] = None) -> None:
    payload = {
        "ok": True,
        "event": "script_message",
        "script_id": message.script_id,
        "message": message.message,
        "data_len": len(message.data),
    }
    if use_json:
        _emit_json(stdout, payload)
        return
    (console or Console(stdout=stdout, stderr=stdout)).print_script_message_event(message)


def _bind_console_script_origin(console: Optional[Console], use_usb: bool, process_name: str) -> None:
    if console is None:
        return
    device_name = "USB Device" if use_usb else "Local"
    console.bind_script_origin(device_name, process_name)


def _parse_call_args(args_json: str):
    args = json.loads(args_json)
    if not isinstance(args, list):
        raise ValueError("--call-args must be a JSON array")
    return args


def _format_repl_prompt(context) -> str:
    return Console(color=False).format_prompt(context)


def _parse_repl_command(line: str):
    raw = line.rstrip("\r\n")
    if not raw.strip():
        return None
    if not raw.startswith("%") and not raw.startswith("/"):
        return {"name": "post", "args": [raw], "raw": raw}

    content = raw[1:].strip()
    if not content:
        return {"name": "", "args": [], "raw": raw}

    parts = content.split(None, 1)
    name = parts[0]
    remainder = parts[1] if len(parts) > 1 else ""
    if name == "call":
        call_parts = remainder.split(None, 1) if remainder else []
        args = call_parts[:1]
        if len(call_parts) == 2:
            args.append(call_parts[1])
        return {"name": name, "args": args, "raw": raw}
    if remainder:
        return {"name": name, "args": [remainder], "raw": raw}
    return {"name": name, "args": [], "raw": raw}


def _emit_rpc_result(stdout, method: str, result, use_json: bool, console: Optional[Console] = None) -> None:
    if use_json:
        _emit_json(stdout, {"ok": True, "event": "rpc_result", "method": method, "result": result})
        return
    (console or Console(stdout=stdout, stderr=stdout)).rpc_result(method, result)


def _emit_repl_help(console: Console) -> None:
    console.print_repl_help()


def _emit_repl_info(context, stdout) -> None:
    script_state = "(none)"
    if context.script_id is not None:
        script_state = str(context.script_id)
    elif getattr(context, "pending_load", False):
        script_state = "(pending)"
    spawn_state = "n/a"
    if context.entry_mode == "spawn":
        spawn_state = "resumed" if context.resumed else "suspended"
    print(
        "mode=%s pid=%d process=%s script=%s resumed=%s spawn_state=%s"
        % (
            context.entry_mode,
            context.session.pid,
            context.session.process_name,
            script_state,
            "yes" if context.resumed else "no",
            spawn_state,
        ),
        file=stdout,
    )


def _repl_clear_active_script(context) -> None:
    context.script = None
    context.script_id = None
    context.script_name = None


def _repl_require_active_script(context) -> bool:
    return context.script is not None and context.script_id is not None


def _repl_load_script(context, script_path: str, console: Console) -> None:
    if context.script is not None and context.script_id is not None:
        context.script.unload()
        console.success("Script unloaded (id: %d)" % context.script_id)
    script, script_id = _load_script(context.session, script_path, console)
    context.script = script
    context.script_id = script_id
    context.script_path = script_path
    context.script_name = script.name
    context.pending_load = False


def _repl_defer_script_load(context, script_path: str) -> None:
    context.script_path = script_path
    context.script_name = os.path.basename(script_path)
    context.pending_load = True


def _repl_unload_script(context, console: Console) -> None:
    if not _repl_require_active_script(context):
        console.error("no active script")
        return
    try:
        context.script.unload()
        console.success("Script unloaded (id: %d)" % context.script_id)
    except Exception as exc:
        console.error("script unload failed: %s" % (str(exc) or exc.__class__.__name__))
        return
    _repl_clear_active_script(context)


def _repl_cleanup(context, console: Console) -> None:
    context.stop_event.set()
    if not _repl_require_active_script(context):
        return
    try:
        context.script.unload()
        console.success("Script unloaded (id: %d)" % context.script_id)
    except Exception as exc:
        console.error("script unload failed during repl cleanup: %s" % (str(exc) or exc.__class__.__name__))
    finally:
        _repl_clear_active_script(context)


def _wait_for_targeted_script_message(device, timeout_ms: int, script_id: Optional[int]):
    return device.wait_for_script_message(timeout_ms=timeout_ms, script_id=script_id)


def _wait_for_current_script_message(device, timeout_ms: int, script_id: Optional[int]):
    try:
        return _wait_for_targeted_script_message(device, timeout_ms=timeout_ms, script_id=script_id)
    except TimeoutError:
        if script_id is not None:
            return _wait_for_targeted_script_message(device, timeout_ms=timeout_ms, script_id=0)
        raise


def _repl_message_loop(context, console: Console) -> None:
    while not context.stop_event.is_set():
        try:
            message = _wait_for_current_script_message(
                context.device,
                timeout_ms=context.message_timeout_ms,
                script_id=context.script_id,
            )
        except TimeoutError:
            time.sleep(0)
            continue
        except KeyboardInterrupt:
            return
        except Exception as exc:
            if context.stop_event.is_set():
                return
            console.error("repl message loop error: %s" % (str(exc) or exc.__class__.__name__))
            return
        _emit_script_message(console.stdout, message, False, console)


def _handle_repl_command(context, command, stdout, stderr, console: Console) -> bool:
    name = command["name"]
    args = command["args"]

    if name == "exit":
        return False
    if name == "help":
        _emit_repl_help(console)
        return True
    if name == "info":
        _emit_repl_info(context, stdout)
        return True
    if name == "load":
        if not args:
            console.error("usage: /load <path>")
            return True
        try:
            _repl_load_script(context, args[0], console)
        except Exception as exc:
            state = "resumed" if context.resumed else "suspended"
            session_label = "spawn" if context.entry_mode == "spawn" else "attach"
            console.error(
                "script load failed while %s session is %s: %s"
                % (session_label, state, str(exc) or exc.__class__.__name__)
            )
        return True
    if name == "reload":
        if not context.script_path:
            console.error("no script path remembered")
            return True
        _repl_load_script(context, context.script_path, console)
        return True
    if name == "unload":
        _repl_unload_script(context, console)
        return True
    if name == "post":
        if not args:
            console.error("usage: /post <message>")
            return True
        if not _repl_require_active_script(context):
            console.error("no active script")
            return True
        context.script.post(args[0])
        return True
    if name == "call":
        if not args:
            console.error("usage: /call <method> [args_json]")
            return True
        if not _repl_require_active_script(context):
            console.error("no active script")
            return True
        method = args[0]
        call_args = []
        if len(args) > 1 and args[1]:
            try:
                call_args = _parse_call_args(args[1])
            except (TypeError, ValueError, json.JSONDecodeError):
                console.error("invalid JSON array")
                return True
        result = context.script.call(method, *call_args)
        _emit_rpc_result(stdout, method, result, False, console)
        return True
    if name == "resume":
        if context.entry_mode != "spawn":
            console.error("resume is only available for spawn mode")
            return True
        if context.resumed:
            console.info("already resumed: pid=%d state=resumed" % context.session.pid)
            return True
        try:
            console.info("Resuming pid %d..." % context.session.pid)
            _resume_session(context.device, context.session.pid, context.session.process_name)
        except Exception as exc:
            console.error(
                "resume failed while spawn session is suspended: %s"
                % (str(exc) or exc.__class__.__name__)
            )
            return True
        context.resumed = True
        console.success("Process resumed")
        return True
    console.error("unknown command, type /help")
    return True


def _run_repl(context, stdin, stdout, stderr) -> int:
    console = context.console
    message_thread = threading.Thread(
        target=_repl_message_loop,
        args=(context, console),
        name="NookCliReplMessageLoop",
        daemon=True,
    )
    message_thread.start()
    while True:
        try:
            console.write_prompt(_format_repl_prompt(context))
            line = stdin.readline()
            console.clear_prompt_active()
        except KeyboardInterrupt:
            console.clear_prompt_active()
            _repl_cleanup(context, console)
            message_thread.join(timeout=0.2)
            return 0
        if line == "":
            console.clear_prompt_active()
            _repl_cleanup(context, console)
            message_thread.join(timeout=0.2)
            return 0
        command = _parse_repl_command(line)
        if command is None:
            continue
        keep_running = _handle_repl_command(context, command, stdout, stderr, console)
        if not keep_running:
            _repl_cleanup(context, console)
            message_thread.join(timeout=0.2)
            return 0


def _create_repl_context(args, device, console: Console):
    device_name = "USB Device" if getattr(args, "usb", False) else "Local"
    if not getattr(args, "json", False):
        console.print_banner(__version__)
    if args.repl_mode == "spawn":
        session = _spawn_session(
            device,
            args.package,
            args.agent_ready_timeout,
            _build_spawn_argv(
                getattr(args, "spawn_symbi", False),
                getattr(args, "strict_zygote_control", False),
            ),
            console=None if getattr(args, "json", False) else console,
        )
        resumed = False
        context = SimpleNamespace(
            entry_mode="spawn",
            device=device,
            session=session,
            script=None,
            script_id=None,
            script_path=None,
            script_name=None,
            resumed=resumed,
            message_timeout_ms=args.message_timeout,
            stop_event=threading.Event(),
            pending_load=False,
            device_name=device_name,
            console=console,
        )
        console.bind_script_origin(device_name, session.process_name)
        if args.script_path:
            script, script_id = _load_script(session, args.script_path, console)
            context.script = script
            context.script_id = script_id
            context.script_path = args.script_path
            context.script_name = script.name
            context.pending_load = False
        if args.resume:
            console.info("Resuming pid %d..." % session.pid)
            _resume_session(device, session.pid, session.process_name)
            context.resumed = True
            console.success("Process resumed")
    else:
        target = int(args.target) if args.target.isdigit() else args.target
        session = _attach_session(device, target, console)
        context = SimpleNamespace(
            entry_mode="attach",
            device=device,
            session=session,
            script=None,
            script_id=None,
            script_path=None,
            script_name=None,
            resumed=True,
            message_timeout_ms=args.message_timeout,
            stop_event=threading.Event(),
            pending_load=False,
            device_name=device_name,
            console=console,
        )
        console.bind_script_origin(device_name, session.process_name)
        if args.script_path:
            script, script_id = _load_script(session, args.script_path, console)
            context.script = script
            context.script_id = script_id
            context.script_path = args.script_path
            context.script_name = script.name
            context.pending_load = False
    return context


def _should_use_interactive_subcommand_mode(args) -> bool:
    if getattr(args, "command", None) not in ("spawn", "attach"):
        return False
    if getattr(args, "oneshot", False):
        return False
    if getattr(args, "json", False):
        return False
    if getattr(args, "wait", False):
        return False
    if getattr(args, "interactive", False):
        return False
    if getattr(args, "call", None):
        return False
    return True


def _should_print_banner_for_command(args) -> bool:
    if getattr(args, "json", False):
        return False
    if getattr(args, "command", None) == "repl":
        return False
    return getattr(args, "command", None) in {
        "spawn",
        "attach",
        "call",
        "post",
        "unload",
    }


def _make_repl_args_from_subcommand(args):
    common = {
        "usb": args.usb,
        "serial": args.serial,
        "host": args.host,
        "port": args.port,
        "timeout": args.timeout,
        "json": args.json,
        "script_path": getattr(args, "script_path", None),
        "message_timeout": getattr(args, "message_timeout", 1000),
    }
    if args.command == "spawn":
        return argparse.Namespace(
            command="repl",
            repl_mode="spawn",
            package=args.package,
            resume=args.resume,
            agent_ready_timeout=args.agent_ready_timeout,
            spawn_symbi=getattr(args, "spawn_symbi", False),
            strict_zygote_control=getattr(args, "strict_zygote_control", False),
            **common,
        )
    return argparse.Namespace(
        command="repl",
        repl_mode="attach",
        target=args.target,
        **common,
    )


def _wait_for_messages(device, stdout, use_json: bool, timeout_ms: int, console: Optional[Console] = None) -> None:
    while True:
        try:
            message = device.wait_for_script_message(timeout_ms=timeout_ms, script_id=None)
        except TimeoutError:
            time.sleep(0)
            continue
        except KeyboardInterrupt:
            return
        _emit_script_message(stdout, message, use_json, console)


def _wait_for_messages_for_script(device,
                                  script_id: Optional[int],
                                  stdout,
                                  use_json: bool,
                                  timeout_ms: int,
                                  console: Optional[Console] = None) -> None:
    while True:
        try:
            message = _wait_for_current_script_message(
                device,
                timeout_ms=timeout_ms,
                script_id=script_id,
            )
        except TimeoutError:
            time.sleep(0)
            continue
        except KeyboardInterrupt:
            return
        _emit_script_message(stdout, message, use_json, console)


def _drain_messages_after_unload(device, stdout, use_json: bool, timeout_ms: int, max_messages: int = 16,
                                 console: Optional[Console] = None,
                                 script_id: Optional[int] = None) -> None:
    drained = 0
    while drained < max_messages:
        try:
            message = _wait_for_current_script_message(
                device,
                timeout_ms=timeout_ms,
                script_id=script_id,
            )
        except TimeoutError:
            return
        except KeyboardInterrupt:
            return
        _emit_script_message(stdout, message, use_json, console)
        drained += 1


def _cleanup_wait_script(device, script, script_id, stdout, stderr, use_json: bool,
                         drain_timeout_ms: int = 100, console: Optional[Console] = None) -> None:
    if script is None or script_id is None:
        return
    try:
        script.unload()
        _drain_messages_after_unload(
            device,
            stdout=stdout,
            use_json=use_json,
            timeout_ms=drain_timeout_ms,
            console=console,
            script_id=script_id,
        )
        if not use_json:
            (console or Console(stdout=stdout, stderr=stderr)).success("Script unloaded (id: %d)" % script_id)
    except Exception as exc:
        message = str(exc) or exc.__class__.__name__
        if use_json:
            _emit_json(stderr, {"ok": False, "error": "script unload failed during wait cleanup: %s" % message})
        else:
            (console or Console(stdout=stdout, stderr=stderr)).error(
                "script unload failed during wait cleanup: %s" % message
            )


def _interactive_post_pump(script, stdin) -> None:
    for line in stdin:
        message = line.rstrip("\r\n")
        if not message.strip():
            continue
        script.post(message)


def _start_interactive_post_thread(script, stdin):
    thread = threading.Thread(
        target=_interactive_post_pump,
        args=(script, stdin),
        name="NookCliInteractivePost",
        daemon=True,
    )
    thread.start()
    return thread


def main(
    argv: Optional[Sequence[str]] = None,
    device_factory: Optional[Callable[..., object]] = None,
    usb_device_factory: Optional[Callable[..., object]] = None,
    stdin=None,
    stdout=None,
    stderr=None,
) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    stdin = stdin or sys.stdin
    stdout = stdout or sys.stdout
    stderr = stderr or sys.stderr
    device_factory = device_factory or _default_device_factory
    usb_device_factory = usb_device_factory or _default_usb_device_factory
    parser = _build_parser()
    if argv and argv[0] in ("-h", "--help"):
        print(parser.format_help(), file=stdout)
        return 0
    args = parser.parse_args(argv)
    try:
        _validate_spawn_backend_flags(args)
    except Exception as exc:
        console = Console(stdout=stdout, stderr=stderr)
        _emit_error(stderr, str(exc) or exc.__class__.__name__, getattr(args, "json", False), console=console)
        return 1
    console = Console(stdout=stdout, stderr=stderr)
    device = None

    try:
        if _should_print_banner_for_command(args):
            console.print_banner(__version__)
        device = _create_device(args, device_factory, usb_device_factory)
        if _should_use_interactive_subcommand_mode(args):
            repl_args = _make_repl_args_from_subcommand(args)
            context = _create_repl_context(repl_args, device, console)
            return _run_repl(context, stdin=stdin, stdout=stdout, stderr=stderr)
        if args.command == "apps":
            apps = device.enumerate_apps()
            if args.json:
                _emit_json(
                    stdout,
                    {
                        "ok": True,
                        "count": len(apps),
                        "apps": [app.package_name for app in apps],
                    },
                )
                return 0
            console.print_apps(apps)
            return 0

        if args.command == "ps":
            processes = device.enumerate_processes()
            if args.json:
                _emit_json(
                    stdout,
                    {
                        "ok": True,
                        "count": len(processes),
                        "processes": [
                            {"pid": process.pid, "name": process.name}
                            for process in processes
                        ],
                    },
                )
                return 0
            console.print_processes(processes)
            return 0

        if args.command == "spawn":
            if not args.json:
                console.info("Spawning '%s'..." % args.package)
            session = _spawn_session(
                device,
                args.package,
                args.agent_ready_timeout,
                _build_spawn_argv(args.spawn_symbi, getattr(args, "strict_zygote_control", False)),
                console=None if args.json else console,
            )
            if not args.json:
                _bind_console_script_origin(console, args.usb, session.process_name)
            if not args.json:
                console.success("Spawned (pid: %d)" % session.pid)
            script = None
            script_id = None
            if args.script_path:
                script, script_id = _load_script(session, args.script_path, None if args.json else console)
            if args.resume:
                if not args.json:
                    console.info("Resuming pid %d..." % session.pid)
                _resume_session(device, session.pid, session.process_name)
                if not args.json:
                    console.success("Process resumed")
            if args.call and script is None:
                raise ValueError("rpc call requires a loaded script")
            if args.interactive and script is None:
                raise ValueError("interactive mode requires a loaded script")
            if args.interactive and not args.wait:
                raise ValueError("interactive mode requires --wait")
            if args.call:
                result = script.call(args.call, *_parse_call_args(args.call_args))
                if not args.json:
                    _emit_rpc_result(stdout, args.call, result, False, console)
            if args.json:
                payload = {
                    "ok": True,
                    "pid": session.pid,
                    "process_name": session.process_name,
                    "resumed": bool(args.resume),
                }
                if script is not None:
                    payload["script"] = {
                        "id": script_id,
                        "name": script.name,
                        "loaded": True,
                    }
                if args.call:
                    payload["rpc"] = {"method": args.call, "result": result}
                _emit_json(stdout, payload)
            interactive_thread = None
            if args.wait:
                try:
                    if args.interactive:
                        interactive_thread = _start_interactive_post_thread(script, stdin)
                    if script_id is not None:
                        _wait_for_messages_for_script(
                            device,
                            script_id=script_id,
                            stdout=stdout,
                            use_json=args.json,
                            timeout_ms=args.message_timeout,
                            console=console,
                        )
                    else:
                        _wait_for_messages(
                            device,
                            stdout=stdout,
                            use_json=args.json,
                            timeout_ms=args.message_timeout,
                            console=console,
                        )
                finally:
                    if interactive_thread is not None:
                        interactive_thread.join(timeout=0.1)
                    _cleanup_wait_script(
                        device,
                        script,
                        script_id,
                        stdout,
                        stderr,
                        args.json,
                        console=console,
                    )
            return 0

        if args.command == "attach":
            target = int(args.target) if args.target.isdigit() else args.target
            if not args.json:
                console.info("Attaching to '%s'..." % target)
            session = _attach_session(device, target, None if args.json else console)
            if not args.json:
                _bind_console_script_origin(console, args.usb, session.process_name)
            if not args.json:
                console.success("Attached (pid: %d, session: %d)" % (session.pid, session.session_id))
            script = None
            script_id = None
            if args.script_path:
                script, script_id = _load_script(session, args.script_path, None if args.json else console)
            if args.call and script is None:
                raise ValueError("rpc call requires a loaded script")
            if args.interactive and script is None:
                raise ValueError("interactive mode requires a loaded script")
            if args.interactive and not args.wait:
                raise ValueError("interactive mode requires --wait")
            if args.call:
                result = script.call(args.call, *_parse_call_args(args.call_args))
                if not args.json:
                    _emit_rpc_result(stdout, args.call, result, False, console)
            if args.json:
                payload = {
                    "ok": True,
                    "session_id": session.session_id,
                    "pid": session.pid,
                    "process_name": session.process_name,
                }
                if script is not None:
                    payload["script"] = {
                        "id": script_id,
                        "name": script.name,
                        "loaded": True,
                    }
                if args.call:
                    payload["rpc"] = {"method": args.call, "result": result}
                _emit_json(stdout, payload)
            interactive_thread = None
            if args.wait:
                try:
                    if args.interactive:
                        interactive_thread = _start_interactive_post_thread(script, stdin)
                    if script_id is not None:
                        _wait_for_messages_for_script(
                            device,
                            script_id=script_id,
                            stdout=stdout,
                            use_json=args.json,
                            timeout_ms=args.message_timeout,
                            console=console,
                        )
                    else:
                        _wait_for_messages(
                            device,
                            stdout=stdout,
                            use_json=args.json,
                            timeout_ms=args.message_timeout,
                            console=console,
                        )
                finally:
                    if interactive_thread is not None:
                        interactive_thread.join(timeout=0.1)
                    _cleanup_wait_script(
                        device,
                        script,
                        script_id,
                        stdout,
                        stderr,
                        args.json,
                        console=console,
                    )
            return 0

        if args.command == "repl":
            context = _create_repl_context(args, device, console)
            return _run_repl(context, stdin=stdin, stdout=stdout, stderr=stderr)

        if args.command == "call":
            if not args.script_path:
                raise ValueError("call command requires -l/--load")
            if args.spawn == args.attach:
                raise ValueError("call command requires exactly one of --spawn or --attach")

            result = None
            script = None
            script_id = None
            if args.spawn:
                if not args.json:
                    console.info("Spawning '%s'..." % args.target)
                session = _spawn_session(
                    device,
                    args.target,
                    args.agent_ready_timeout,
                    _build_spawn_argv(args.spawn_symbi, getattr(args, "strict_zygote_control", False)),
                    console=None if args.json else console,
                )
                if not args.json:
                    _bind_console_script_origin(console, args.usb, session.process_name)
                if not args.json:
                    console.success("Spawned (pid: %d)" % session.pid)
                script, script_id = _load_script(session, args.script_path, None if args.json else console)
                if args.resume:
                    if not args.json:
                        console.info("Resuming pid %d..." % session.pid)
                    _resume_session(device, session.pid, session.process_name)
                    if not args.json:
                        console.success("Process resumed")
                result = script.call(args.method, *_parse_call_args(args.call_args))
                if args.json:
                    _emit_json(
                        stdout,
                        {
                            "ok": True,
                            "mode": "spawn",
                            "pid": session.pid,
                            "process_name": session.process_name,
                            "resumed": bool(args.resume),
                            "script": {
                                "id": script_id,
                                "name": script.name,
                                "loaded": True,
                            },
                            "rpc": {
                                "method": args.method,
                                "result": result,
                            },
                        },
                    )
                    return 0
                _emit_rpc_result(stdout, args.method, result, False, console)
                return 0

            target = int(args.target) if args.target.isdigit() else args.target
            if not args.json:
                console.info("Attaching to '%s'..." % target)
            session = _attach_session(device, target, None if args.json else console)
            if not args.json:
                _bind_console_script_origin(console, args.usb, session.process_name)
            if not args.json:
                console.success("Attached (pid: %d, session: %d)" % (session.pid, session.session_id))
            script, script_id = _load_script(session, args.script_path, None if args.json else console)
            result = script.call(args.method, *_parse_call_args(args.call_args))
            if args.json:
                _emit_json(
                    stdout,
                    {
                        "ok": True,
                        "mode": "attach",
                        "session_id": session.session_id,
                        "pid": session.pid,
                        "process_name": session.process_name,
                        "script": {
                            "id": script_id,
                            "name": script.name,
                            "loaded": True,
                        },
                        "rpc": {
                            "method": args.method,
                            "result": result,
                        },
                    },
                )
                return 0
            _emit_rpc_result(stdout, args.method, result, False, console)
            return 0

        if args.command == "detach":
            device.detach(args.session_id)
            if args.json:
                _emit_json(stdout, {"ok": True, "session_id": args.session_id})
                return 0
            console.info("Detaching...")
            console.success("Detached")
            return 0

        if args.command == "resume":
            if not args.json:
                console.info("Resuming pid %d..." % args.pid)
            _resume_session(device, args.pid, str(args.pid))
            if args.json:
                _emit_json(stdout, {"ok": True, "pid": args.pid})
                return 0
            console.success("Process resumed")
            return 0

        if args.command == "post":
            if not args.script_path:
                raise ValueError("post command requires -l/--load")
            if not args.json:
                console.info("Spawning '%s'..." % args.package)
            session = _spawn_session(
                device,
                args.package,
                args.agent_ready_timeout,
                _build_spawn_argv(args.spawn_symbi, getattr(args, "strict_zygote_control", False)),
                console=None if args.json else console,
            )
            if not args.json:
                _bind_console_script_origin(console, args.usb, session.process_name)
            if not args.json:
                console.success("Spawned (pid: %d)" % session.pid)
            script, script_id = _load_script(session, args.script_path, None if args.json else console)
            if not args.json:
                console.info("Resuming pid %d..." % session.pid)
            _resume_session(device, session.pid, session.process_name)
            if not args.json:
                console.success("Process resumed")
            script.post(args.message)
            if not args.json:
                console.info("Posted message to script")
            message = _wait_for_current_script_message(
                device,
                timeout_ms=args.message_timeout,
                script_id=script_id,
            )
            if args.json:
                _emit_json(
                    stdout,
                    {
                        "ok": True,
                        "pid": session.pid,
                        "process_name": session.process_name,
                        "resumed": True,
                        "script": {
                            "id": script_id,
                            "name": script.name,
                            "loaded": True,
                        },
                        "post_message": args.message,
                        "message": {
                            "script_id": message.script_id,
                            "message": message.message,
                            "data_len": len(message.data),
                        },
                    },
                )
                return 0
            _emit_script_message(stdout, message, False, console)
            return 0

        if args.command == "unload":
            if not args.json:
                console.info("Spawning '%s'..." % args.package)
            session = _spawn_session(
                device,
                args.package,
                args.agent_ready_timeout,
                _build_spawn_argv(args.spawn_symbi, getattr(args, "strict_zygote_control", False)),
                console=None if args.json else console,
            )
            if not args.json:
                _bind_console_script_origin(console, args.usb, session.process_name)
            if not args.json:
                console.success("Spawned (pid: %d)" % session.pid)
            script, script_id = _load_script(session, args.script_path, None if args.json else console)
            if not args.json:
                console.info("Resuming pid %d..." % session.pid)
            _resume_session(device, session.pid, session.process_name)
            if not args.json:
                console.success("Process resumed")
            script.unload()
            if args.json:
                _emit_json(
                    stdout,
                    {
                        "ok": True,
                        "pid": session.pid,
                        "process_name": session.process_name,
                        "resumed": True,
                        "script": {
                            "id": script_id,
                            "name": script.name,
                            "loaded": True,
                            "unloaded": True,
                        },
                    },
                )
                return 0
            console.success("Script unloaded (id: %d)" % script_id)
            return 0
    except Exception as exc:
        message = _rewrite_connection_error(str(exc) or exc.__class__.__name__)
        _emit_error(stderr, message, getattr(args, "json", False), console=console)
        return 1
    finally:
        if device is not None and hasattr(device, "close"):
            device.close()


if __name__ == "__main__":
    raise SystemExit(main())
