import hashlib
import io
import json
import os
import struct
import time
import zlib
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Dict, List, Optional

from ._transport import TcpConnection
from .device import Device
from .output import Console


DEXDUMP_SCRIPT_NAME = "dexdump.js"
_MIN_SCAN_RANGE_SIZE = 8 * 1024 * 1024
_MAX_SCAN_RANGE_SIZE = 256 * 1024 * 1024
_SCAN_RANGE_MULTIPLIER = 4
_DUMP_CHUNK_SIZE = 32 * 1024
_SCAN_WINDOW_RANGE_BATCH = 4
_SCAN_WINDOW_MAX_BYTES = 64 * 1024 * 1024
_SCAN_RANGE_SLICE_BYTES = 1 * 1024 * 1024
_SCAN_RANGE_SLICE_BYTES_UNLIMITED = 8 * 1024 * 1024
_SCAN_RANGE_RETRY_MIN_SLICE_BYTES = 256 * 1024
_SCAN_PATTERN_OVERLAP_BYTES = 7
_FULL_SCAN_MAX_RANGES = 64
_FULL_SCAN_MAX_BYTES = 32 * 1024 * 1024
_FAST_FULL_SCAN_TIMEOUT_MS = 30_000
_DEXDUMP_MESSAGE_TIMEOUT_MIN_MS = 60_000
_DEXDUMP_DUMP_TIMEOUT_MIN_MS = 180_000
_DEXDUMP_SCRIPT_SETUP_TIMEOUT_MIN_MS = 30_000
_DEXDUMP_SCRIPT_SETUP_TIMEOUT_UNLIMITED_MIN_MS = 120_000


@dataclass
class DexDumpArtifact:
    file_name: str
    address: str
    size: int
    hash: str
    raw_hash: str
    declared_size: int
    real_size: int
    fallback_size: int
    source: str
    confidence: str
    deep: bool
    repaired: bool
    maps_ok: bool
    ids_ok: bool
    target_hint_hit: bool


def _script_path() -> str:
    return os.path.join(os.path.dirname(__file__), DEXDUMP_SCRIPT_NAME)


def load_dexdump_script_source() -> str:
    with open(_script_path(), "r", encoding="utf-8") as handle:
        return handle.read()


def default_output_dir(target: str) -> str:
    return os.path.abspath(f".\\{target}-dexdump")


def build_search_options(options) -> dict:
    max_dex_size = int(getattr(options, "max_dex_size", 64 * 1024 * 1024))
    max_range_size = min(
        max(max_dex_size * _SCAN_RANGE_MULTIPLIER, _MIN_SCAN_RANGE_SIZE),
        _MAX_SCAN_RANGE_SIZE,
    )
    target_package = str(getattr(options, "spawn_package", None) or getattr(options, "target", None) or "")
    max_results = getattr(options, "max_results", 64)
    if max_results is None:
        max_results = 64
    if int(max_results) == 0:
        target_package = ""
    return {
        "deep": bool(getattr(options, "deep", False)),
        "max_results": int(max_results),
        "min_dex_size": int(getattr(options, "min_dex_size", 0x70)),
        "max_dex_size": max_dex_size,
        "max_range_size": max_range_size,
        "include_system": bool(getattr(options, "include_system", False)),
        "debug": bool(getattr(options, "debug", False)),
        "target_package": target_package,
        "force_chunk_scan": bool(getattr(options, "force_chunk_scan", False)),
    }


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

        if message_type == "dexdump-error" and message_token == token:
            raise RuntimeError(payload.get("error") or "dexdump export failed")

        if message_type != "dexdump-chunk" or message_token != token:
            if console is not None:
                console.print_script_message_event(message)
            continue

        index = int(payload.get("index", -1))
        if index < 0:
            raise RuntimeError("dexdump chunk index is invalid")
        chunks[index] = bytes(message.data)
        if bool(payload.get("eof", False)):
            eof_seen = True

        if eof_seen and len(chunks) >= expected_chunks:
            break

    return b"".join(chunks[index] for index in sorted(chunks))


def repair_dex_header(data: bytes) -> bytes:
    if len(data) < 0x70:
        return data

    buffer = bytearray(data)
    if buffer[0:4] != b"dex\n" or buffer[7] != 0:
        version = buffer[4:7]
        if not all(48 <= value <= 57 for value in version):
            version = b"035"
        buffer[0:8] = b"dex\n" + bytes(version) + b"\x00"

    struct.pack_into("<I", buffer, 0x20, len(buffer))
    struct.pack_into("<I", buffer, 0x24, 0x70)
    struct.pack_into("<I", buffer, 0x28, 0x12345678)

    signature = hashlib.sha1(buffer[32:]).digest()
    buffer[12:32] = signature
    checksum = zlib.adler32(buffer[12:]) & 0xFFFFFFFF
    struct.pack_into("<I", buffer, 8, checksum)
    return bytes(buffer)


def _confidence_rank(value: str) -> int:
    return {"high": 0, "medium": 1, "low": 2}.get(str(value or "").lower(), 3)


def _is_timeout_error(exc: BaseException) -> bool:
    lowered = (str(exc) or exc.__class__.__name__).lower()
    return any(
        marker in lowered
        for marker in (
            "timed out",
            "timeout",
            "operation timed out",
        )
    )


def _normalize_results(search_result: dict) -> List[dict]:
    return list(search_result.get("results") or [])


def _candidate_export_sizes(candidate: dict, options) -> List[int]:
    primary = int(candidate.get("real_size") or candidate.get("size") or 0)
    declared = int(candidate.get("declared_size") or candidate.get("size") or 0)
    fallback = int(candidate.get("fallback_size") or primary)
    if not bool(getattr(options, "deep", False)):
        return [primary] if primary > 0 else []
    sizes: List[int] = []
    for value in (primary, declared, fallback):
        if value > 0 and value not in sizes:
            sizes.append(value)
    return sizes


def _artifact_file_name(index: int) -> str:
    return "classes.dex" if index == 0 else f"classes{index + 1}.dex"


def _ensure_output_dir(path: str) -> str:
    os.makedirs(path, exist_ok=True)
    return path


def _emit_json_result(stdout, payload: dict) -> None:
    print(json.dumps(payload, ensure_ascii=False), file=stdout)


def _merge_scan_stats(accumulator: dict, search_result: dict) -> dict:
    stats = dict(search_result.get("stats") or {})
    if not accumulator:
        return stats

    accumulator["ranges_total"] = max(int(accumulator.get("ranges_total", 0)), int(stats.get("ranges_total", 0)))
    for key in ("ranges_scanned", "ranges_skipped", "magic_hits", "magic_matches", "deep_hits", "verified", "errors", "candidates_total"):
        accumulator[key] = int(accumulator.get(key, 0)) + int(stats.get(key, 0))
    return accumulator


def _script_setup_timeout_ms(options) -> int:
    min_timeout_ms = _DEXDUMP_SCRIPT_SETUP_TIMEOUT_MIN_MS
    max_results = getattr(options, "max_results", None)
    if max_results is not None and int(max_results) == 0:
        min_timeout_ms = _DEXDUMP_SCRIPT_SETUP_TIMEOUT_UNLIMITED_MIN_MS
    return max(
        min_timeout_ms,
        int(getattr(options, "agent_ready_timeout", 10000)),
        int(getattr(options, "message_timeout", 5000)),
    )


def _dump_timeout_ms(options) -> int:
    return max(
        _DEXDUMP_DUMP_TIMEOUT_MIN_MS,
        int(getattr(options, "message_timeout", 5000)),
    )


def _scan_timeout_ms(options) -> int:
    return max(
        _DEXDUMP_MESSAGE_TIMEOUT_MIN_MS,
        int(getattr(options, "message_timeout", 5000)),
    )


def _scan_slice_bytes(options) -> int:
    configured = int(getattr(options, "scan_slice_bytes", 0) or 0)
    if configured > 0:
        return configured
    max_results = getattr(options, "max_results", None)
    if max_results is not None and int(max_results) == 0:
        return _SCAN_RANGE_SLICE_BYTES_UNLIMITED
    return _SCAN_RANGE_SLICE_BYTES


def _should_isolate_raw_anonymous_ranges(options) -> bool:
    configured = getattr(options, "isolate_raw_anonymous", None)
    if configured is not None:
        return bool(configured)
    return False


def _load_scan_script(session, options=None):
    source = load_dexdump_script_source()
    timeout_ms = _script_setup_timeout_ms(options) if options is not None else None
    last_error = None
    for attempt in range(2):
        script = session.create_script(source, name=DEXDUMP_SCRIPT_NAME)
        try:
            if timeout_ms is None:
                script.create()
                script.load()
            else:
                try:
                    script.create(timeout_ms=timeout_ms)
                    script.load(timeout_ms=timeout_ms)
                except TypeError:
                    script.create()
                    script.load()
            return script
        except Exception as exc:
            last_error = exc
            if attempt >= 1 or not _is_timeout_error(exc):
                raise
            try:
                script.unload()
            except Exception:
                pass
    raise last_error if last_error is not None else RuntimeError("script setup failed")


def _reload_scan_script(session, script, options=None):
    try:
        script.unload()
    except Exception:
        pass
    return _load_scan_script(session, options=options)


def _reattach_scan_session(device, pid, options, console=None):
    if not getattr(options, "json", False) and console is not None:
        try:
            pid_value = int(pid)
        except Exception:
            pid_value = pid
        console.info("Reattaching pid %s after scan timeout..." % pid_value)
    device = _reconnect_device(device)
    last_error = None
    session = None
    for attempt in range(3):
        try:
            session = device.attach(pid, timeout_ms=_scan_timeout_ms(options))
            break
        except Exception as exc:
            last_error = exc
            if attempt >= 2:
                raise
            time.sleep(0.5)
            device = _reconnect_device(device)
    if session is None:
        raise last_error if last_error is not None else RuntimeError("reattach failed")
    script = _load_scan_script(session, options=options)
    return device, session, script


def _reconnect_device(device):
    connection = getattr(device, "_connection", None)
    if connection is None:
        return device
    host = getattr(connection, "_host", None)
    port = getattr(connection, "_port", None)
    timeout_ms = getattr(connection, "_default_timeout_ms", None)
    default_timeout_ms = getattr(device, "_default_timeout_ms", timeout_ms)
    if host is None or port is None or default_timeout_ms is None:
        return device
    try:
        device.close()
    except Exception:
        pass
    return Device(
        TcpConnection(host=host, port=port, timeout_ms=default_timeout_ms),
        default_timeout_ms=default_timeout_ms,
    )


def _call_searchdex(script, batch, options):
    search_options = build_search_options(options)
    search_options["explicit_ranges"] = batch
    return script.call(
        "searchdex",
        search_options,
        timeout_ms=_scan_timeout_ms(options),
        )


def _call_searchdex_full(script, options):
    search_options = build_search_options(options)
    return script.call(
        "searchdex",
        search_options,
        timeout_ms=_scan_timeout_ms(options),
    )


def _call_searchdex_full_with_timeout(script, options, timeout_ms: int):
    search_options = build_search_options(options)
    return script.call(
        "searchdex",
        search_options,
        timeout_ms=timeout_ms,
    )


def _is_process_alive(device, pid) -> bool:
    try:
        pid_value = int(pid)
    except Exception:
        return True
    try:
        for process in device.enumerate_processes():
            if int(getattr(process, "pid", -1)) == pid_value:
                return True
    except Exception:
        return True
    return False


def _total_range_bytes(scan_ranges: List[dict]) -> int:
    return sum(max(0, int(item.get("size", 0))) for item in scan_ranges)


def _should_try_full_scan(scan_ranges: List[dict], options) -> bool:
    if bool(getattr(options, "deep", False)):
        return False
    if not scan_ranges:
        return False
    if len(scan_ranges) > _FULL_SCAN_MAX_RANGES:
        return False
    if _total_range_bytes(scan_ranges) > _FULL_SCAN_MAX_BYTES:
        return False
    return True


def _list_scan_ranges(device, session, script, pid, options):
    all_ranges: List[dict] = []
    start_index = 0
    page_size = 128
    enumerate_reattach_budget = 1
    while True:
        enumerate_options = build_search_options(options)
        enumerate_options["start_index"] = start_index
        enumerate_options["max_ranges"] = page_size
        try:
            payload = script.call(
                "enumerateranges",
                enumerate_options,
                timeout_ms=getattr(options, "message_timeout", 5000),
            )
        except Exception as exc:
            if not _is_timeout_error(exc) or enumerate_reattach_budget <= 0:
                raise
            enumerate_reattach_budget -= 1
            device, session, script = _reattach_scan_session(device, pid, options)
            continue
        ranges = list((payload or {}).get("ranges") or [])
        window = dict((payload or {}).get("window") or {})
        all_ranges.extend(ranges)
        next_index = int(window.get("end_index", start_index + len(ranges)))
        if bool(window.get("done", False)):
            break
        if next_index <= start_index:
            raise RuntimeError("dexdump range enumeration did not advance")
        start_index = next_index
    return device, session, script, all_ranges


def _plan_scan_batches(
    scan_ranges: List[dict],
    max_ranges_per_batch: int,
    max_bytes_per_batch: int,
    isolate_raw_anonymous: bool = False,
):
    batches = []
    current_batch: List[dict] = []
    current_start_index = 0
    current_size = 0

    for index, scan_range in enumerate(scan_ranges):
        range_size = max(0, int(scan_range.get("size", 0)))
        if isolate_raw_anonymous and _is_raw_anonymous_scan_range(scan_range):
            if current_batch:
                batches.append((current_start_index, current_batch))
                current_batch = []
                current_size = 0
            batches.append((index, [scan_range]))
            continue
        would_hit_range_limit = len(current_batch) >= max_ranges_per_batch
        would_hit_size_limit = (
            current_batch and
            max_bytes_per_batch > 0 and
            (current_size + range_size) > max_bytes_per_batch
        )

        if would_hit_range_limit or would_hit_size_limit:
            batches.append((current_start_index, current_batch))
            current_batch = []
            current_start_index = index
            current_size = 0

        if not current_batch:
            current_start_index = index

        current_batch.append(scan_range)
        current_size += range_size

    if current_batch:
        batches.append((current_start_index, current_batch))
    return batches


def _split_batch_for_retry(start_index: int, batch: List[dict]):
    if len(batch) <= 1:
        return []
    midpoint = max(1, len(batch) // 2)
    left = batch[:midpoint]
    right = batch[midpoint:]
    split_batches = []
    if left:
        split_batches.append((start_index, left))
    if right:
        split_batches.append((start_index + len(left), right))
    return split_batches


def _split_single_scan_range_for_retry(scan_range: dict, min_slice_bytes: int) -> List[dict]:
    current_base_text = str(scan_range.get("scan_base") or scan_range.get("base", ""))
    current_size = max(0, int(scan_range.get("size", 0) or scan_range.get("scan_size") or 0))
    if not current_base_text or current_size <= min_slice_bytes:
        return []

    parent_base_text = str(scan_range.get("range_base") or scan_range.get("base", ""))
    parent_size = max(0, int(scan_range.get("range_size") or scan_range.get("size", 0)))
    current_base_value = int(current_base_text, 0)
    left_size = max(min_slice_bytes, current_size // 2)
    if left_size >= current_size:
        return []
    right_size = current_size - left_size
    if right_size <= 0:
        return []

    result: List[dict] = []
    offset = 0
    for slice_size in (left_size, right_size):
        child = dict(scan_range)
        child["range_base"] = parent_base_text
        child["range_size"] = parent_size
        child["scan_base"] = "0x%x" % (current_base_value + offset)
        overlap = min(_SCAN_PATTERN_OVERLAP_BYTES, max(0, current_size - (offset + slice_size)))
        child["scan_size"] = slice_size + overlap
        child["base"] = child["scan_base"]
        child["size"] = slice_size
        result.append(child)
        offset += slice_size
    return result


def _split_batch_for_timeout_retry(start_index: int, batch: List[dict]):
    split_batches = _split_batch_for_retry(start_index, batch)
    if split_batches:
        return split_batches
    if len(batch) != 1:
        return []
    sub_slices = _split_single_scan_range_for_retry(
        batch[0],
        _SCAN_RANGE_RETRY_MIN_SLICE_BYTES,
    )
    if not sub_slices:
        return []
    return [
        (start_index + index, [sub_slice])
        for index, sub_slice in enumerate(sub_slices)
    ]


def _append_unique_candidates(discovered_candidates: List[dict], candidates: List[dict], seen_addrs: set) -> int:
    added = 0
    for candidate in candidates:
        addr = str(candidate.get("addr", ""))
        if not addr or addr in seen_addrs:
            continue
        seen_addrs.add(addr)
        discovered_candidates.append(candidate)
        added += 1
    return added


def _filter_new_candidates(candidates: List[dict], seen_addrs: set) -> List[dict]:
    unique_candidates: List[dict] = []
    local_seen = set()
    for candidate in candidates:
        addr = str(candidate.get("addr", ""))
        if not addr or addr in seen_addrs or addr in local_seen:
            continue
        local_seen.add(addr)
        unique_candidates.append(candidate)
    return unique_candidates


def _split_scan_ranges(scan_ranges: List[dict], max_bytes_per_slice: int):
    if max_bytes_per_slice <= 0:
        return list(scan_ranges)

    split_ranges: List[dict] = []
    for scan_range in scan_ranges:
        base_text = str(scan_range.get("base", ""))
        size = max(0, int(scan_range.get("size", 0)))
        if size <= 0:
            continue
        if size <= max_bytes_per_slice:
            split_ranges.append(dict(scan_range))
            continue

        base_value = int(base_text, 0)
        remaining = size
        offset = 0
        while remaining > 0:
            current_size = min(remaining, max_bytes_per_slice)
            sliced_range = dict(scan_range)
            sliced_range["range_base"] = base_text
            sliced_range["range_size"] = size
            sliced_range["scan_base"] = "0x%x" % (base_value + offset)
            overlap = min(_SCAN_PATTERN_OVERLAP_BYTES, max(0, remaining - current_size))
            sliced_range["scan_size"] = current_size + overlap
            sliced_range["base"] = "0x%x" % (base_value + offset)
            sliced_range["size"] = current_size
            split_ranges.append(sliced_range)
            remaining -= current_size
            offset += current_size
    return split_ranges


def _scan_range_parent_key(scan_range: dict):
    return (
        str(scan_range.get("range_base") or scan_range.get("base", "")),
        max(0, int(scan_range.get("range_size") or scan_range.get("size", 0))),
    )


def _scan_range_slice_offset(scan_range: dict) -> int:
    parent_base_text = str(scan_range.get("range_base") or scan_range.get("base", "0"))
    current_base_text = str(scan_range.get("scan_base") or scan_range.get("base", "0"))
    try:
        return max(0, int(current_base_text, 0) - int(parent_base_text, 0))
    except Exception:
        return 0


def _scan_range_slice_index(scan_range: dict) -> int:
    slice_size = max(1, int(scan_range.get("size", 0) or scan_range.get("scan_size") or 1))
    return _scan_range_slice_offset(scan_range) // slice_size


def _sort_scan_ranges_for_early_hit(scan_ranges: List[dict]) -> List[dict]:
    parent_rank = {}
    ordered_parents = sorted(
        {_scan_range_parent_key(item) for item in scan_ranges},
        key=lambda item: (
            -item[1],
            item[0],
        ),
    )
    for index, parent_key in enumerate(ordered_parents):
        parent_rank[parent_key] = index

    return sorted(
        scan_ranges,
        key=lambda item: (
            _scan_range_slice_index(item),
            parent_rank.get(_scan_range_parent_key(item), len(parent_rank)),
            _scan_range_slice_offset(item),
            str(item.get("base", "")),
        ),
    )


def _is_raw_anonymous_scan_range(scan_range: dict) -> bool:
    return (
        scan_range.get("range_base") is None
        and not scan_range.get("path")
    )


def _sort_scan_ranges_for_search(
    scan_ranges: List[dict],
    deprioritize_raw_anonymous: bool = False,
) -> List[dict]:
    ordered = _sort_scan_ranges_for_early_hit(scan_ranges)
    if not deprioritize_raw_anonymous:
        return ordered
    stable_ranges = [item for item in ordered if not _is_raw_anonymous_scan_range(item)]
    raw_anonymous_ranges = [item for item in ordered if _is_raw_anonymous_scan_range(item)]
    return stable_ranges + raw_anonymous_ranges


def _is_pathologically_truncated_candidate(candidate: dict) -> bool:
    real_size = max(0, int(candidate.get("real_size") or candidate.get("size") or 0))
    if real_size <= 0 or real_size >= (256 * 1024):
        return False
    declared_size = max(0, int(candidate.get("declared_size") or candidate.get("size") or 0))
    fallback_size = max(0, int(candidate.get("fallback_size") or real_size))
    expected_size = max(declared_size, fallback_size)
    return expected_size >= (real_size * 4)


def _export_candidate_rank(candidate: dict):
    pathologically_truncated = _is_pathologically_truncated_candidate(candidate)
    return (
        1 if pathologically_truncated else 0,
        0 if bool(candidate.get("target_hint_hit", False)) and not pathologically_truncated else 1,
        _confidence_rank(str(candidate.get("confidence", ""))),
        0 if bool(candidate.get("maps_ok", False)) else 1,
        0 if bool(candidate.get("ids_ok", False)) else 1,
        max(0, int(candidate.get("real_size") or candidate.get("size") or 0)),
        str(candidate.get("addr", "")),
    )


def _export_candidate_quality_rank(candidate: dict):
    pathologically_truncated = _is_pathologically_truncated_candidate(candidate)
    return (
        1 if pathologically_truncated else 0,
        0 if bool(candidate.get("target_hint_hit", False)) and not pathologically_truncated else 1,
        _confidence_rank(str(candidate.get("confidence", ""))),
        0 if bool(candidate.get("maps_ok", False)) else 1,
        0 if bool(candidate.get("ids_ok", False)) else 1,
    )


def _count_non_pathologically_truncated_candidates(candidates: List[dict]) -> int:
    return sum(1 for candidate in candidates if not _is_pathologically_truncated_candidate(candidate))


def _candidate_size_band(candidate: dict) -> int:
    real_size = max(0, int(candidate.get("real_size") or candidate.get("size") or 0))
    if real_size < (4 * 1024 * 1024):
        return 0
    if real_size < (5 * 1024 * 1024):
        return 1
    if real_size < (6 * 1024 * 1024):
        return 2
    if real_size < (8 * 1024 * 1024):
        return 3
    return 4


def _select_export_candidates(candidates: List[dict], max_results: int) -> List[dict]:
    ordered_candidates = sorted(list(candidates), key=_export_candidate_rank)
    if max_results <= 0 or len(ordered_candidates) <= max_results:
        return ordered_candidates
    if max_results < 4:
        return ordered_candidates

    selected: List[dict] = []
    seen_addr = set()
    band_representatives = {}

    for candidate in candidates:
        if _is_pathologically_truncated_candidate(candidate):
            continue
        band = _candidate_size_band(candidate)
        candidate_quality = _export_candidate_quality_rank(candidate)
        current = band_representatives.get(band)
        if current is None or candidate_quality < current[0]:
            band_representatives[band] = (candidate_quality, candidate)

    for band in sorted(band_representatives):
        candidate = band_representatives[band][1]
        addr = str(candidate.get("addr", ""))
        if addr in seen_addr:
            continue
        selected.append(candidate)
        seen_addr.add(addr)
        if len(selected) >= max_results:
            return selected

    for candidate in ordered_candidates:
        addr = str(candidate.get("addr", ""))
        if addr in seen_addr:
            continue
        selected.append(candidate)
        seen_addr.add(addr)
        if len(selected) >= max_results:
            break

    return selected


def _dump_candidates_from_script(script, candidates, options, output_dir, seen_keys, artifacts, console):
    max_results = int(getattr(options, "max_results", 64))
    dump_timeout_ms = _dump_timeout_ms(options)
    ordered_candidates = _select_export_candidates(list(candidates), max_results)
    for candidate in ordered_candidates:
        for export_size in _candidate_export_sizes(candidate, options):
            try:
                dump_meta = script.call(
                    "beginmemorydump",
                    candidate["addr"],
                    export_size,
                    {
                        "chunk_size": _DUMP_CHUNK_SIZE,
                        "try_protect": True,
                    },
                    timeout_ms=dump_timeout_ms,
                )
                token = str(dump_meta["token"])
                expected_chunks = int(dump_meta.get("chunks", 0))
                raw_bytes = collect_dump_bytes(
                    script,
                    token=token,
                    expected_chunks=expected_chunks,
                    timeout_ms=dump_timeout_ms,
                    console=console if not getattr(options, "json", False) else None,
                )
            except Exception as exc:
                if not getattr(options, "json", False):
                    console.warning(
                        "Skipping dex candidate at %s size=%d: %s"
                        % (candidate.get("addr", ""), export_size, str(exc) or exc.__class__.__name__)
                    )
                continue

            raw_hash = hashlib.md5(raw_bytes).hexdigest()
            dumped_bytes = repair_dex_header(raw_bytes) if bool(getattr(options, "fix_header", False)) else raw_bytes
            content_hash = hashlib.md5(dumped_bytes).hexdigest()
            dedupe_key = (
                "%s:%d" % (candidate["addr"], export_size)
                if getattr(options, "dedupe", "md5") == "addr"
                else content_hash
            )
            if dedupe_key in seen_keys:
                continue
            seen_keys.add(dedupe_key)

            file_name = _artifact_file_name(len(artifacts))
            with open(os.path.join(output_dir, file_name), "wb") as handle:
                handle.write(dumped_bytes)
            artifacts.append(
                DexDumpArtifact(
                    file_name=file_name,
                    address=str(candidate.get("addr", "")),
                    size=len(dumped_bytes),
                    hash=content_hash,
                    raw_hash=raw_hash,
                    declared_size=int(candidate.get("declared_size") or candidate.get("size") or len(dumped_bytes)),
                    real_size=int(candidate.get("real_size") or candidate.get("size") or len(dumped_bytes)),
                    fallback_size=int(candidate.get("fallback_size") or candidate.get("size") or len(dumped_bytes)),
                    source=str(candidate.get("source", "")),
                    confidence=str(candidate.get("confidence", "")),
                    deep=bool(candidate.get("deep", False)),
                    repaired=bool(getattr(options, "fix_header", False)),
                    maps_ok=bool(candidate.get("maps_ok", False)),
                    ids_ok=bool(candidate.get("ids_ok", False)),
                    target_hint_hit=bool(candidate.get("target_hint_hit", False)),
                )
            )
            if max_results > 0 and len(artifacts) >= max_results:
                return True
    return False


def _build_metadata(
    target: str,
    mode: str,
    options,
    aggregate_stats: dict,
    artifacts: List[DexDumpArtifact],
    discovered_candidates: Optional[List[dict]] = None,
    scan_aborted: bool = False,
    scan_abort_reason: Optional[str] = None,
    elapsed_ms: Optional[int] = None,
) -> dict:
    metadata = {
        "target": target,
        "mode": mode,
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "deep": bool(getattr(options, "deep", False)),
        "elapsed_ms": elapsed_ms,
        "scan_aborted": bool(scan_aborted),
        "scan_abort_reason": scan_abort_reason,
        "stats": aggregate_stats,
        "artifacts": [
            {
                "file_name": artifact.file_name,
                "address": artifact.address,
                "size": artifact.size,
                "hash": artifact.hash,
                "raw_hash": artifact.raw_hash,
                "declared_size": artifact.declared_size,
                "real_size": artifact.real_size,
                "fallback_size": artifact.fallback_size,
                "source": artifact.source,
                "confidence": artifact.confidence,
                "deep": artifact.deep,
                "repaired": artifact.repaired,
                "maps_ok": artifact.maps_ok,
                "ids_ok": artifact.ids_ok,
                "target_hint_hit": artifact.target_hint_hit,
            }
            for artifact in artifacts
        ],
    }
    if bool(getattr(options, "debug", False)) and discovered_candidates:
        metadata["candidates"] = [
            {
                "address": str(candidate.get("addr", "")),
                "size": int(candidate.get("size") or 0),
                "declared_size": int(candidate.get("declared_size") or candidate.get("size") or 0),
                "real_size": int(candidate.get("real_size") or candidate.get("size") or 0),
                "fallback_size": int(candidate.get("fallback_size") or candidate.get("size") or 0),
                "source": str(candidate.get("source", "")),
                "confidence": str(candidate.get("confidence", "")),
                "deep": bool(candidate.get("deep", False)),
                "maps_ok": bool(candidate.get("maps_ok", False)),
                "ids_ok": bool(candidate.get("ids_ok", False)),
                "target_hint_hit": bool(candidate.get("target_hint_hit", False)),
                "range_base": candidate.get("range_base"),
                "range_size": candidate.get("range_size"),
                "range_path": candidate.get("range_path"),
            }
            for candidate in discovered_candidates
        ]
    return metadata


def run_dexdump(options, device, stdout=None, stderr=None) -> dict:
    stdout = stdout or io.StringIO()
    stderr = stderr or io.StringIO()
    console = Console(stdout=stdout, stderr=stderr)
    started_at = time.monotonic()

    target = getattr(options, "spawn_package", None) or getattr(options, "target", None)
    if not target:
        raise ValueError("dexdump requires either <target> or --spawn <package>")

    output_dir = _ensure_output_dir(getattr(options, "output_dir", None) or default_output_dir(target))
    mode = "spawn" if getattr(options, "spawn_package", None) else "attach"

    if not getattr(options, "json", False):
        console.info(f"Dex dump mode: {mode}")

    if mode == "spawn":
        if not getattr(options, "json", False):
            console.info("Spawning '%s'..." % target)
        session = device.spawn(target, argv=[], agent_ready_timeout_ms=getattr(options, "agent_ready_timeout", 10000))
        if not getattr(options, "json", False):
            console.info("Resuming pid %d..." % session.pid)
        device.resume(session.pid)
        settle_ms = int(getattr(options, "sleep_ms", 0))
        if settle_ms > 0:
            time.sleep(settle_ms / 1000.0)
    else:
        session = device.attach(target)

    artifacts: List[DexDumpArtifact] = []
    discovered_candidates: List[dict] = []
    seen_candidate_addrs = set()
    seen_keys = set()
    aggregate_stats = {}
    scan_aborted = False
    scan_abort_reason = None
    reattach_budget = 1
    scan_resume_required = False
    scan_window_ranges = max(1, int(getattr(options, "scan_window_ranges", _SCAN_WINDOW_RANGE_BATCH)))
    scan_window_max_bytes = max(0, int(getattr(options, "scan_window_max_bytes", _SCAN_WINDOW_MAX_BYTES)))
    scan_grace_windows = max(0, int(getattr(options, "scan_grace_windows", 1)))
    max_results = int(getattr(options, "max_results", 64))
    limit_grace_windows_remaining = None
    target_pid = session.pid

    if not getattr(options, "json", False):
        console.info("Loading '%s'..." % DEXDUMP_SCRIPT_NAME)
        console.info("Enumerating scannable ranges...")

    script = _load_scan_script(session, options=options)
    try:
        fast_full_scan_done = False
        if (
            mode == "attach"
            and not bool(getattr(options, "deep", False))
            and bool(getattr(options, "fast_full_scan", True))
        ):
            fast_full_scan_timeout_ms = max(
                _FAST_FULL_SCAN_TIMEOUT_MS,
                int(getattr(options, "message_timeout", 5000)),
            )
            if not getattr(options, "json", False):
                console.info("Attempting fast full-process dex scan...")
            try:
                fast_full_search_result = _call_searchdex_full_with_timeout(
                    script,
                    options,
                    timeout_ms=fast_full_scan_timeout_ms,
                )
                fast_full_scan_done = True
                fast_candidates = _normalize_results(fast_full_search_result)
                _append_unique_candidates(
                    discovered_candidates,
                    fast_candidates,
                    seen_candidate_addrs,
                )
                aggregate_stats = _merge_scan_stats(aggregate_stats, fast_full_search_result)
                reached_limit = _dump_candidates_from_script(
                    script,
                    fast_candidates,
                    options,
                    output_dir,
                    seen_keys,
                    artifacts,
                    console,
                )
                if not getattr(options, "json", False):
                    console.success(
                        "Fast full-process scan found %d dex candidate(s)"
                        % len(fast_candidates)
                    )
                if reached_limit:
                    if not getattr(options, "json", False):
                        console.success("Reached max-results limit (%d) during fast full-process export" % max_results)
                if fast_candidates or reached_limit:
                    metadata = _build_metadata(
                        target,
                        mode,
                        options,
                        aggregate_stats,
                        artifacts,
                        discovered_candidates=discovered_candidates,
                        scan_aborted=scan_aborted,
                        scan_abort_reason=scan_abort_reason,
                        elapsed_ms=int((time.monotonic() - started_at) * 1000),
                    )
                    with open(os.path.join(output_dir, "metadata.json"), "w", encoding="utf-8") as handle:
                        json.dump(metadata, handle, indent=2, ensure_ascii=False)
                    result = {
                        "ok": True,
                        "target": target,
                        "mode": mode,
                        "output_dir": output_dir,
                        "artifact_count": len(artifacts),
                        "scan_aborted": scan_aborted,
                        "scan_abort_reason": scan_abort_reason,
                        "metadata": metadata,
                    }
                    if getattr(options, "json", False):
                        _emit_json_result(stdout, result)
                    else:
                        console.success("Wrote %d dex artifact(s) to %s" % (len(artifacts), output_dir))
                    return result
            except Exception as fast_full_exc:
                if not _is_timeout_error(fast_full_exc):
                    raise
                if not getattr(options, "json", False):
                    console.warning("Fast full-process scan timed out; falling back to windowed scan")

        device, session, script, listed_ranges = _list_scan_ranges(
            device,
            session,
            script,
            target_pid,
            options,
        )
        scan_ranges = _sort_scan_ranges_for_search(
            _split_scan_ranges(listed_ranges, _scan_slice_bytes(options)),
            deprioritize_raw_anonymous=(max_results <= 0),
        )
        aggregate_stats["ranges_total"] = len(scan_ranges)
        if not getattr(options, "json", False):
            console.success("Planned %d scannable range(s)" % len(scan_ranges))

        if _should_try_full_scan(scan_ranges, options):
            if not getattr(options, "json", False):
                console.info("Attempting full-process dex scan...")
            full_search_result = _call_searchdex_full(script, options)
            _append_unique_candidates(
                discovered_candidates,
                _normalize_results(full_search_result),
                seen_candidate_addrs,
            )
            aggregate_stats = _merge_scan_stats(aggregate_stats, full_search_result)
            reached_limit = _dump_candidates_from_script(
                script,
                _normalize_results({"results": discovered_candidates}),
                options,
                output_dir,
                seen_keys,
                artifacts,
                console,
            )
            if not getattr(options, "json", False):
                console.success(
                    "Full-process scan found %d dex candidate(s)"
                    % len(full_search_result.get("results") or [])
                )
            if reached_limit and not getattr(options, "json", False):
                console.success("Reached max-results limit (%d) during full-process export" % max_results)
        else:
            pending_batches = list(
                _plan_scan_batches(
                    scan_ranges,
                    scan_window_ranges,
                    scan_window_max_bytes,
                    isolate_raw_anonymous=_should_isolate_raw_anonymous_ranges(options),
                )
            )
            total_batches = len(pending_batches)
            processed_batches = 0
            while pending_batches:
                if scan_resume_required or session is None or script is None:
                    try:
                        device, session, script = _reattach_scan_session(device, target_pid, options, console)
                        scan_resume_required = False
                    except Exception as reattach_exc:
                        if max_results <= 0:
                            skipped_pending = 1 if pending_batches else 0
                            if not getattr(options, "json", False):
                                console.warning(
                                    "Reattach failed before continuing scan; skipping one deferred range block and continuing best-effort: %s"
                                    % (str(reattach_exc or "unknown error"))
                                )
                            if pending_batches:
                                pending_batches.pop(0)
                            aggregate_stats["ranges_skipped"] = int(aggregate_stats.get("ranges_skipped", 0)) + skipped_pending
                            scan_resume_required = True
                            session = None
                            script = None
                            continue
                        scan_aborted = True
                        scan_abort_reason = "reattach failed before continuing scan: %s" % (
                            str(reattach_exc or "unknown error")
                        )
                        if not getattr(options, "json", False):
                            console.warning(scan_abort_reason)
                        break
                start_index, batch = pending_batches.pop(0)
                processed_batches += 1
                if not getattr(options, "json", False) and bool(getattr(options, "debug", False)):
                    batch_bytes = sum(max(0, int(item.get("size", 0))) for item in batch)
                    console.info(
                        "Scanning dex candidates (window start=%d, size=%d, bytes=%d)..."
                        % (start_index, len(batch), batch_bytes)
                    )
                current_end_index = start_index + len(batch)
                try:
                    search_result = _call_searchdex(script, batch, options)
                except Exception as inner_exc:
                    if not _is_timeout_error(inner_exc):
                        raise
                    aggregate_stats["errors"] = int(aggregate_stats.get("errors", 0)) + 1
                    if not _is_process_alive(device, session.pid):
                        scan_aborted = True
                        scan_abort_reason = "target process exited during scan timeout at window %d..%d" % (
                            start_index,
                            current_end_index,
                        )
                        if not getattr(options, "json", False):
                            console.warning(scan_abort_reason)
                        break
                    retry_batches = _split_batch_for_timeout_retry(start_index, batch)
                    if retry_batches:
                        reattach_error = None
                        reattach_attempted = False
                        if reattach_budget > 0:
                            reattach_attempted = True
                            try:
                                device, session, script = _reattach_scan_session(device, session.pid, options, console)
                                reattach_budget -= 1
                            except Exception as reattach_exc:
                                reattach_error = reattach_exc
                        if reattach_attempted and reattach_error is not None:
                            if max_results <= 0:
                                aggregate_stats["ranges_skipped"] = int(aggregate_stats.get("ranges_skipped", 0)) + len(batch)
                                if not getattr(options, "json", False):
                                    console.warning(
                                        "Reattach failed after scan timeout at window %d..%d; skipping range block and continuing: %s"
                                        % (start_index, current_end_index, str(reattach_error or "unknown error"))
                                    )
                                session = None
                                script = None
                                scan_resume_required = True
                                continue
                            scan_aborted = True
                            scan_abort_reason = (
                                "reattach failed after scan timeout at window %d..%d: %s"
                                % (start_index, current_end_index, str(reattach_error or "unknown error"))
                            )
                            if not getattr(options, "json", False):
                                console.warning(scan_abort_reason)
                            break
                        if not getattr(options, "json", False):
                            if len(batch) == 1 and len(retry_batches) > 1:
                                console.warning(
                                    "Scan window %d..%d timed out; retrying timed-out slice as %d smaller sub-slice(s)"
                                    % (start_index, current_end_index, len(retry_batches))
                                )
                            else:
                                console.warning(
                                    "Scan window %d..%d timed out; retrying as %d smaller batch(es)"
                                    % (start_index, current_end_index, len(retry_batches))
                                )
                        pending_batches = retry_batches + pending_batches
                        continue
                    aggregate_stats["ranges_skipped"] = int(aggregate_stats.get("ranges_skipped", 0)) + len(batch)
                    if not getattr(options, "json", False):
                        console.warning(
                            "Scan window %d..%d timed out at minimum batch size; skipping range block %d..%d"
                            % (start_index, current_end_index, start_index, current_end_index)
                        )
                    continue

                candidates = _normalize_results(search_result)
                aggregate_stats = _merge_scan_stats(aggregate_stats, search_result)

                if not getattr(options, "json", False):
                    if (
                        len(candidates) > 0
                        or bool(getattr(options, "debug", False))
                        or processed_batches == 1
                        or processed_batches == total_batches
                        or (processed_batches % 50) == 0
                    ):
                        console.success(
                            "Window %d..%d found %d dex candidate(s) [%d/%d]" % (
                                start_index,
                                current_end_index,
                                len(candidates),
                                processed_batches,
                                total_batches,
                            )
                        )
                new_candidates = _filter_new_candidates(candidates, seen_candidate_addrs)
                _append_unique_candidates(discovered_candidates, new_candidates, seen_candidate_addrs)
                if max_results <= 0 and new_candidates:
                    _dump_candidates_from_script(
                        script,
                        _normalize_results({"results": new_candidates}),
                        options,
                        output_dir,
                        seen_keys,
                        artifacts,
                        console,
                    )
                if (
                    max_results > 0
                    and _count_non_pathologically_truncated_candidates(discovered_candidates) >= max_results
                ):
                    if limit_grace_windows_remaining is None:
                        limit_grace_windows_remaining = scan_grace_windows
                    elif limit_grace_windows_remaining > 0:
                        limit_grace_windows_remaining -= 1

                    if limit_grace_windows_remaining <= 0:
                        reached_limit = _dump_candidates_from_script(
                            script,
                            _normalize_results({"results": discovered_candidates}),
                            options,
                            output_dir,
                            seen_keys,
                            artifacts,
                            console,
                        )
                        if reached_limit:
                            if not getattr(options, "json", False):
                                console.success("Reached max-results limit (%d); stopping scan early" % max_results)
                            break
                if scan_aborted:
                    break
        if (
            discovered_candidates
            and not artifacts
            and not scan_aborted
            and not getattr(options, "json", False)
        ):
            console.info("Exporting %d discovered dex candidate(s)..." % len(discovered_candidates))
        if discovered_candidates and not artifacts and not scan_aborted:
            reached_limit = _dump_candidates_from_script(
                script,
                _normalize_results({"results": discovered_candidates}),
                options,
                output_dir,
                seen_keys,
                artifacts,
                console,
            )
            if reached_limit and not getattr(options, "json", False):
                console.success("Reached max-results limit (%d) after unique exports" % max_results)
    finally:
        try:
            script.unload()
        except Exception:
            pass
        try:
            session.detach(timeout_ms=getattr(options, "message_timeout", 5000))
        except Exception:
            pass

    metadata = _build_metadata(
        target,
        mode,
        options,
        aggregate_stats,
        artifacts,
        discovered_candidates=discovered_candidates,
        scan_aborted=scan_aborted,
        scan_abort_reason=scan_abort_reason,
        elapsed_ms=int((time.monotonic() - started_at) * 1000),
    )
    with open(os.path.join(output_dir, "metadata.json"), "w", encoding="utf-8") as handle:
        json.dump(metadata, handle, indent=2, ensure_ascii=False)

    result = {
        "ok": True,
        "target": target,
        "mode": mode,
        "output_dir": output_dir,
        "artifact_count": len(artifacts),
        "scan_aborted": scan_aborted,
        "scan_abort_reason": scan_abort_reason,
        "metadata": metadata,
    }
    if getattr(options, "json", False):
        _emit_json_result(stdout, result)
    else:
        console.success("Wrote %d dex artifact(s) to %s" % (len(artifacts), output_dir))
    return result
