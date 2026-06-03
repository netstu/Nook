import hashlib
import io
import json
import os
import re
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Dict, List, Optional

from .output import Console
from .sofix import rebuild_loaded_elf_image


SODUMP_SCRIPT_NAME = "sodump.js"
_DUMP_CHUNK_SIZE = 32 * 1024


@dataclass
class SoDumpArtifact:
    module_name: str
    module_path: str
    address: str
    size: int
    raw_file_name: str
    raw_hash: str
    fix_applied: bool
    fix_success: bool
    fixed_file_name: Optional[str]
    fixed_hash: Optional[str]
    repair_error: Optional[str]
    repair_warnings: List[str]
    synthesized_sections: int


def _script_path() -> str:
    return os.path.join(os.path.dirname(__file__), SODUMP_SCRIPT_NAME)


def load_sodump_script_source() -> str:
    with open(_script_path(), "r", encoding="utf-8") as handle:
        return handle.read()


def default_output_dir(target: str) -> str:
    return os.path.abspath(f".\\{target}-sodump")


def _ensure_output_dir(path: str) -> str:
    os.makedirs(path, exist_ok=True)
    return path


def _emit_json_result(stdout, payload: dict) -> None:
    print(json.dumps(payload, ensure_ascii=False), file=stdout)


def _parse_script_message_json(message) -> Optional[dict]:
    try:
        payload = json.loads(message.message)
    except (TypeError, ValueError, json.JSONDecodeError):
        return None
    return payload if isinstance(payload, dict) else None


def collect_dump_bytes(script, token: str, expected_chunks: int, timeout_ms: int, console: Optional[Console] = None) -> bytes:
    chunks: Dict[int, bytes] = {}
    eof_seen = False

    while True:
        message = script.wait_for_message(timeout_ms=timeout_ms)
        payload = _parse_script_message_json(message)
        if payload is None:
            if console is not None:
                console.raw_script_message(message.script_id, message.message, len(message.data))
            continue

        message_type = payload.get("type")
        message_token = payload.get("token")

        if message_type == "sodump-error" and message_token == token:
            raise RuntimeError(payload.get("error") or "sodump export failed")

        if message_type != "sodump-chunk" or message_token != token:
            if console is not None:
                console.print_script_message_event(message)
            continue

        index = int(payload.get("index", -1))
        if index < 0:
            raise RuntimeError("sodump chunk index is invalid")
        chunks[index] = bytes(message.data)
        if bool(payload.get("eof", False)):
            eof_seen = True

        if eof_seen and len(chunks) >= expected_chunks:
            break

    return b"".join(chunks[index] for index in sorted(chunks))


def _safe_output_token(value: str) -> str:
    sanitized = re.sub(r'[<>:"/\\|?*]+', "_", value or "")
    return sanitized.strip(" .") or "module"


def _parse_address(value) -> Optional[int]:
    if value is None:
        return None
    if isinstance(value, int):
        return value
    text = str(value).strip()
    if not text:
        return None
    return int(text, 0)


def _normalize_module_list(modules) -> List[dict]:
    normalized = []
    for item in list(modules or []):
        if not isinstance(item, dict):
            continue
        normalized.append(
            {
                "name": str(item.get("name", "")),
                "path": str(item.get("path", "")),
                "base": str(item.get("base", "")),
                "size": int(item.get("size", 0) or 0),
            }
        )
    normalized.sort(key=lambda module: (module["name"].lower(), module["base"]))
    return normalized


def run_sodump(options, device, stdout=None, stderr=None, session=None, resume_after_load: bool = False, resume_subject: Optional[str] = None) -> dict:
    stdout = stdout or io.StringIO()
    stderr = stderr or io.StringIO()
    console = Console(stdout=stdout, stderr=stderr)

    target = getattr(options, "spawn_package", None) or getattr(options, "target", None)
    if not target:
        raise ValueError("sodump requires either <target> or --spawn <package>")

    module_name = getattr(options, "module", None)
    mode = "spawn" if getattr(options, "spawn_package", None) else "attach"
    output_dir = None
    if module_name:
        output_dir = _ensure_output_dir(getattr(options, "output_dir", None) or default_output_dir(target))

    if not getattr(options, "json", False):
        console.info(f"So dump mode: {mode}")

    if session is None:
        if mode == "spawn":
            if not getattr(options, "json", False):
                console.info("Spawning '%s'..." % target)
            session = device.spawn(target, argv=[], agent_ready_timeout_ms=getattr(options, "agent_ready_timeout", 10000))
        else:
            if not getattr(options, "json", False):
                console.info("Attaching to '%s'..." % target)
            session = device.attach(target)
    elif not getattr(options, "json", False):
        console.info("Attaching to '%s'..." % target)

    if not getattr(options, "json", False):
        console.info("Loading '%s'..." % SODUMP_SCRIPT_NAME)
    script = session.create_script(load_sodump_script_source(), name=SODUMP_SCRIPT_NAME)
    script.create()
    script.load()

    try:
        if mode == "spawn":
            if not getattr(options, "json", False):
                console.info("Resuming pid %d..." % session.pid)
            device.resume(session.pid)
            settle_ms = int(getattr(options, "sleep_ms", 0))
            if settle_ms > 0:
                time.sleep(settle_ms / 1000.0)
        elif resume_after_load:
            if not getattr(options, "json", False):
                console.info("Resuming pid %d..." % session.pid)
            device.resume(session.pid)
            if not getattr(options, "json", False):
                console.success("Process resumed")
            settle_ms = int(getattr(options, "sleep_ms", 0))
            if settle_ms > 0:
                time.sleep(settle_ms / 1000.0)

        if not module_name:
            modules = _normalize_module_list(
                script.call(
                    "listmodules",
                    timeout_ms=getattr(options, "message_timeout", 5000),
                )
            )
            result = {
                "ok": True,
                "target": target,
                "mode": mode,
                "listed": True,
                "module_count": len(modules),
                "modules": modules,
            }
            if getattr(options, "json", False):
                _emit_json_result(stdout, result)
            else:
                console.success("Found %d module(s)" % len(modules))
                for module in modules:
                    console.log(
                        "%s\t%s\t%d\t%s"
                        % (module["name"], module["base"], module["size"], module["path"])
                    )
            return result

        module_info = script.call(
            "findmodule",
            module_name,
            timeout_ms=getattr(options, "message_timeout", 5000),
        )
        if not isinstance(module_info, dict):
            module_info = None
        if not module_info:
            result = {
                "ok": False,
                "target": target,
                "mode": mode,
                "error": "module '%s' not found" % module_name,
            }
            if getattr(options, "json", False):
                _emit_json_result(stdout, result)
            else:
                console.error(result["error"])
            return result

        dump_meta = script.call(
            "beginmoduledump",
            module_name,
            {
                "chunk_size": _DUMP_CHUNK_SIZE,
                "try_protect": True,
            },
            timeout_ms=getattr(options, "message_timeout", 5000),
        )
        token = str(dump_meta["token"])
        expected_chunks = int(dump_meta.get("chunks", 0))
        raw_bytes = collect_dump_bytes(
            script,
            token=token,
            expected_chunks=expected_chunks,
            timeout_ms=getattr(options, "message_timeout", 5000),
            console=console if not getattr(options, "json", False) else None,
        )

        file_stem = _safe_output_token(str(module_info.get("name", module_name)))
        raw_file_name = f"{file_stem}.raw.so"
        raw_path = os.path.join(output_dir, raw_file_name)
        with open(raw_path, "wb") as handle:
            handle.write(raw_bytes)
        raw_hash = hashlib.sha256(raw_bytes).hexdigest()

        fixed_file_name = None
        fixed_hash = None
        repair_error = None
        repair_warnings: List[str] = []
        synthesized_sections = 0
        fix_applied = bool(getattr(options, "fix", True))
        fix_success = False

        if fix_applied:
            try:
                repaired = rebuild_loaded_elf_image(
                    raw_bytes,
                    base_address=_parse_address(module_info.get("base")),
                    debug=bool(getattr(options, "debug", False)),
                    base_so_path=getattr(options, "base_so", None),
                )
                fixed_file_name = f"{file_stem}.fix.so"
                fixed_path = os.path.join(output_dir, fixed_file_name)
                with open(fixed_path, "wb") as handle:
                    handle.write(repaired.data)
                fixed_hash = hashlib.sha256(repaired.data).hexdigest()
                repair_warnings = list(getattr(repaired, "warnings", []) or [])
                synthesized_sections = int(getattr(repaired, "synthesized_sections", 0) or 0)
                fix_success = True
            except Exception as exc:
                repair_error = str(exc) or exc.__class__.__name__

        artifact = SoDumpArtifact(
            module_name=str(module_info.get("name", module_name)),
            module_path=str(module_info.get("path", "")),
            address=str(module_info.get("base", "")),
            size=len(raw_bytes),
            raw_file_name=raw_file_name,
            raw_hash=raw_hash,
            fix_applied=fix_applied,
            fix_success=fix_success,
            fixed_file_name=fixed_file_name,
            fixed_hash=fixed_hash,
            repair_error=repair_error,
            repair_warnings=repair_warnings,
            synthesized_sections=synthesized_sections,
        )

        metadata = {
            "target": target,
            "mode": mode,
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "module": {
                "name": artifact.module_name,
                "path": artifact.module_path,
                "base": artifact.address,
                "size": int(module_info.get("size", artifact.size) or artifact.size),
            },
            "artifacts": [
                {
                    "raw_file_name": artifact.raw_file_name,
                    "raw_size": artifact.size,
                    "raw_hash": artifact.raw_hash,
                    "fix_applied": artifact.fix_applied,
                    "fix_success": artifact.fix_success,
                    "fixed_file_name": artifact.fixed_file_name,
                    "fixed_hash": artifact.fixed_hash,
                    "repair_error": artifact.repair_error,
                    "repair_warnings": artifact.repair_warnings,
                    "synthesized_sections": artifact.synthesized_sections,
                }
            ],
        }
        metadata_path = os.path.join(output_dir, f"{file_stem}.json")
        with open(metadata_path, "w", encoding="utf-8") as handle:
            json.dump(metadata, handle, indent=2, ensure_ascii=False)

        ok = (not fix_applied) or fix_success
        result = {
            "ok": ok,
            "target": target,
            "mode": mode,
            "output_dir": output_dir,
            "metadata_path": metadata_path,
            "artifact_count": 1,
            "metadata": metadata,
        }
        if getattr(options, "json", False):
            _emit_json_result(stdout, result)
        else:
            if ok:
                console.success("Wrote shared object dump to %s" % output_dir)
            else:
                console.warning("Raw dump written to %s but ELF repair failed: %s" % (output_dir, repair_error))
        return result
    finally:
        try:
            script.unload()
        except Exception:
            pass
