import io
import json
import os
import sys
import tempfile
import unittest
from types import SimpleNamespace


TEST_ROOT = os.path.dirname(__file__)
PACKAGE_ROOT = os.path.abspath(os.path.join(TEST_ROOT, ".."))
if PACKAGE_ROOT not in sys.path:
    sys.path.insert(0, PACKAGE_ROOT)


from nook import dexdump  # noqa: E402


class FakeMessage:
    def __init__(self, script_id: int, message: str, data: bytes = b"") -> None:
        self.script_id = script_id
        self.message = message
        self.data = data


class FakeDexDumpScript:
    def __init__(self, search_result, dump_messages, call_order, enumerated_ranges=None, begin_dump_results=None, create_results=None, load_results=None, enumerate_results=None) -> None:
        self.script_id = 321
        self.name = "dexdump.js"
        if isinstance(search_result, list):
            self._search_results = list(search_result)
        else:
            self._search_results = [search_result]
        self._dump_messages = list(dump_messages)
        self._call_order = call_order
        self._enumerated_ranges = list(enumerated_ranges or [])
        self._begin_dump_results = list(begin_dump_results or [])
        self._create_results = list(create_results or [])
        self._load_results = list(load_results or [])
        self._enumerate_results = list(enumerate_results or [])
        self.call_calls = []
        self.unloaded = False

    def create(self, timeout_ms=None) -> int:
        self._call_order.append("script.create")
        if self._create_results:
            current = self._create_results.pop(0)
            if isinstance(current, BaseException):
                raise current
        return self.script_id

    def load(self, timeout_ms=None):
        self._call_order.append("script.load")
        if self._load_results:
            current = self._load_results.pop(0)
            if isinstance(current, BaseException):
                raise current
        return None

    def unload(self):
        self._call_order.append("script.unload")
        self.unloaded = True
        return None

    def call(self, method: str, *args, timeout_ms=None):
        self._call_order.append(f"script.call:{method}")
        self.call_calls.append((method, args, timeout_ms))
        if method == "enumerateranges":
            if self._enumerate_results:
                current = self._enumerate_results.pop(0)
                if isinstance(current, BaseException):
                    raise current
                return current
            options = args[0] if args else {}
            start_index = int(options.get("start_index", 0))
            max_ranges = int(options.get("max_ranges", 0))
            end_index = len(self._enumerated_ranges)
            if max_ranges > 0:
                end_index = min(len(self._enumerated_ranges), start_index + max_ranges)
            return {
                "ranges": self._enumerated_ranges[start_index:end_index],
                "window": {
                    "start_index": start_index,
                    "end_index": end_index,
                    "done": end_index >= len(self._enumerated_ranges),
                },
                "total": len(self._enumerated_ranges),
            }
        if method == "searchdex":
            current = self._search_results.pop(0) if len(self._search_results) > 1 else self._search_results[0]
            if isinstance(current, BaseException):
                raise current
            return current
        if method == "beginmemorydump":
            if self._begin_dump_results:
                current = self._begin_dump_results.pop(0)
                if isinstance(current, BaseException):
                    raise current
                return current
            return {
                "token": "dump-0001",
                "size": 8,
                "chunk_size": 4,
                "chunks": 2,
            }
        raise AssertionError(f"unexpected rpc method: {method}")

    def wait_for_message(self, timeout_ms=None):
        self._call_order.append("script.wait_for_message")
        if not self._dump_messages:
            raise TimeoutError("operation timed out")
        return self._dump_messages.pop(0)


class FakeDexDumpSession:
    def __init__(self, pid: int, process_name: str, script: FakeDexDumpScript, call_order) -> None:
        self.pid = pid
        self.process_name = process_name
        self.session_id = 99
        self._script = script
        self._call_order = call_order
        self.detached = False

    def create_script(self, source: str, name: str = "script.js"):
        self._call_order.append("session.create_script")
        return self._script

    def detach(self, timeout_ms=None):
        self._call_order.append("session.detach")
        self.detached = True
        return None


class FakeDexDumpDevice:
    def __init__(self, session: FakeDexDumpSession, call_order, attach_sessions=None, spawn_session=None) -> None:
        self._session = session
        self._call_order = call_order
        self.attach_calls = []
        self.attach_timeout_calls = []
        self.spawn_calls = []
        self.resume_calls = []
        self._attach_sessions = list(attach_sessions or [])
        self._spawn_session = spawn_session or session

    def attach(self, target, timeout_ms=None):
        self._call_order.append("attach")
        self.attach_calls.append(target)
        self.attach_timeout_calls.append(timeout_ms)
        if self._attach_sessions:
            current = self._attach_sessions.pop(0)
            if isinstance(current, BaseException):
                raise current
            return current
        return self._session

    def spawn(self, identifier: str, argv=None, agent_ready_timeout_ms=None):
        self._call_order.append("spawn")
        self.spawn_calls.append((identifier, list(argv or []), agent_ready_timeout_ms))
        return self._spawn_session

    def resume(self, pid: int):
        self._call_order.append("resume")
        self.resume_calls.append(pid)


class DexDumpTests(unittest.TestCase):
    def test_device_script_scans_all_eligible_ranges_before_ranking(self) -> None:
        source = dexdump.load_dexdump_script_source()

        self.assertNotIn("smallFirstBudget", source)
        self.assertNotIn("largeRangeBudget", source)
        self.assertNotIn("scanBudget", source)
        self.assertNotIn("results.length === 0", source)

    def test_device_script_keeps_frida_style_lenient_candidate_checks(self) -> None:
        source = dexdump.load_dexdump_script_source()

        self.assertIn("return safeReadU32(base.add(0x3c)) === 0x70;", source)
        self.assertNotIn("headerSize !== 0x70", source)
        self.assertNotIn("endianTag !== 0x12345678", source)

    def test_device_script_prefers_frida_memory_scan_with_chunked_fallback(self) -> None:
        source = dexdump.load_dexdump_script_source()

        self.assertIn("function shouldForceChunkScanForRange(range, options)", source)
        self.assertIn("function scanRangePatternForRange(range, pattern, options)", source)
        self.assertIn("function setReadPermission(base, size)", source)
        self.assertIn("function scanRangePattern(base, size, pattern, options)", source)
        self.assertIn("function appendPatternMatches(chunkBase, bytes, tokens, results, seen)", source)
        self.assertIn("function scanChunkPatternByReadU8(chunkBase, length, tokens, results, seen)", source)
        self.assertIn("forceChunkScan: !!options.force_chunk_scan", source)
        self.assertIn("if (shouldForceChunkScanForRange(range, options)) {", source)
        self.assertIn('if (!options.forceChunkScan && Memory && typeof Memory.scanSync === "function") {', source)
        self.assertIn("return Memory.scanSync(base, totalSize, pattern) || [];", source)
        self.assertIn("if (setReadPermission(base, totalSize)) {", source)
        self.assertIn("var chunkSize = 256 * 1024;", source)
        self.assertIn("scanChunkPatternByReadU8(chunkBase, length, tokens, results, seen);", source)
        self.assertIn("if (setReadPermission(chunkBase, length)) {", source)

    def test_device_script_retries_magic_scan_with_forced_chunk_fallback_when_primary_scan_throws(self) -> None:
        source = dexdump.load_dexdump_script_source()

        self.assertIn("try {\n      matches = scanRangePatternForRange(range, \"64 65 78 0a 30 ?? ?? 00\", options);\n    } catch (e) {", source)
        self.assertIn("matches = scanRangePattern(\n        getScanBase(range),\n        getScanSize(range),\n        \"64 65 78 0a 30 ?? ?? 00\",", source)
        self.assertIn("forceChunkScan: true", source)

    def test_device_script_repairs_read_permissions_before_dump_retry(self) -> None:
        source = dexdump.load_dexdump_script_source()

        self.assertIn("function readArrayBufferByU8(base, length)", source)
        self.assertIn("data = readArrayBufferByU8(current, length);", source)
        self.assertIn("repaired = setReadPermission(current, length);", source)
        self.assertIn('Memory.protect(alignedWindow.base, alignedWindow.size, "r--")', source)

    def test_device_script_keeps_frida_style_path_filtering_only_for_magic_scan(self) -> None:
        source = dexdump.load_dexdump_script_source()

        self.assertIn("function shouldSkipMagicScanRange(range, options)", source)
        self.assertIn('path.indexOf("/system/") === 0 ||', source)
        self.assertIn('path.indexOf("/data/dalvik-cache/") === 0;', source)
        self.assertNotIn('path.indexOf("/apex/") === 0', source)

    def test_device_script_recovers_missing_range_paths_from_proc_maps(self) -> None:
        source = dexdump.load_dexdump_script_source()

        self.assertIn("var procMapsCache = {", source)
        self.assertIn("function bytesToAsciiString(bytes)", source)
        self.assertIn('var mapsPath = Memory.allocUtf8String("/proc/self/maps");', source)
        self.assertIn("function loadProcMapsEntries()", source)
        self.assertIn("function lookupProcMapsPath(range)", source)
        self.assertIn("return lookupProcMapsPath(range);", source)
        self.assertIn("var procEntries = loadProcMapsEntries();", source)
        self.assertIn('base: ptr("0x" + entryStart.toString(16)),', source)
        self.assertIn('protection: String(entry.protection).substr(0, 3),', source)
        self.assertIn("function splitLargeRangeForScan(range, maxSliceSize)", source)
        self.assertIn("function expandRangesForScan(ranges, options)", source)
        self.assertIn("scan_base: range.base.add(offset),", source)
        self.assertIn("scan_size: currentSize + overlap,", source)
        self.assertIn("description.scan_base = String(range.scan_base);", source)

    def test_device_script_keeps_frida_style_readable_range_enumeration_without_exact_protection_filter(self) -> None:
        source = dexdump.load_dexdump_script_source()

        self.assertIn('var protections = ["r--"];', source)
        self.assertNotIn('if (String(range.protection || protections[i]) !== protections[i]) {', source)
        self.assertNotIn('if (String(range.protection || "r--") !== "r--") {', source)

    def test_device_script_keeps_full_process_compat_scan_close_to_frida_without_anonymous_chunk_fallback(self) -> None:
        source = dexdump.load_dexdump_script_source()

        self.assertIn("var ranges = expandRangesForScan(enumerateCandidateRanges(options), options);", source)
        self.assertIn('matches = Memory.scanSync(getScanBase(range), getScanSize(range), pattern) || [];', source)
        self.assertIn("var rangePath = readRangePath(range);", source)
        self.assertIn('rangePath.indexOf("/data/dalvik-cache/") === 0 ||', source)
        self.assertIn('rangePath.indexOf("/system/") === 0', source)
        self.assertNotIn('if (range.file && range.file.path && (range.file.path.indexOf("/data/dalvik-cache/") === 0 || range.file.path.indexOf("/system/") === 0)) {', source)
        self.assertNotIn("function shouldRetryAnonymousCompatScan(range)", source)
        self.assertNotIn("if (matches.length === 0 && shouldRetryAnonymousCompatScan(range)) {", source)


    def test_device_script_exports_target_range_listing_rpc(self) -> None:
        source = dexdump.load_dexdump_script_source()

        self.assertIn("function listTargetRanges(options)", source)
        self.assertIn("listtargetranges: listTargetRanges,", source)

    def test_device_script_keeps_frida_style_maps_size_ceiling(self) -> None:
        source = dexdump.load_dexdump_script_source()

        self.assertIn("mapsSize === null || mapsSize < 2 || mapsSize > 50", source)
        self.assertNotIn("mapsSize > 4096", source)
        self.assertIn("if (mapsEnd !== null) {", source)
        self.assertNotIn("if (mapsEnd !== null && verifyByMaps(base, mapsAddress))", source)

    def test_build_search_options_derives_wider_max_range_size_for_default_targets(self) -> None:
        options = SimpleNamespace(
            deep=False,
            max_results=64,
            min_dex_size=0x1000,
            max_dex_size=64 * 1024 * 1024,
            include_system=False,
            debug=False,
        )

        result = dexdump.build_search_options(options)

        self.assertEqual(result["max_range_size"], 256 * 1024 * 1024)

    def test_build_search_options_preserves_zero_max_results_for_unlimited_mode(self) -> None:
        options = SimpleNamespace(
            deep=True,
            max_results=0,
            min_dex_size=0x1000,
            max_dex_size=64 * 1024 * 1024,
            include_system=False,
            debug=False,
        )

        result = dexdump.build_search_options(options)

        self.assertEqual(result["max_results"], 0)

    def test_build_search_options_omits_target_package_for_unlimited_mode(self) -> None:
        options = SimpleNamespace(
            deep=False,
            max_results=0,
            min_dex_size=0x1000,
            max_dex_size=64 * 1024 * 1024,
            include_system=False,
            debug=False,
            spawn_package="com.demo.target",
            target="com.demo.target",
        )

        result = dexdump.build_search_options(options)

        self.assertEqual(result["target_package"], "")

    def test_call_searchdex_uses_extended_timeout_floor(self) -> None:
        call_order = []
        script = FakeDexDumpScript(
            search_result={"results": [], "stats": {"verified": 0}},
            dump_messages=[],
            call_order=call_order,
        )
        options = SimpleNamespace(
            deep=False,
            max_results=64,
            min_dex_size=0x1000,
            max_dex_size=64 * 1024 * 1024,
            include_system=False,
            debug=False,
            message_timeout=1000,
        )

        dexdump._call_searchdex(script, [], options)

        self.assertEqual(script.call_calls[-1][0], "searchdex")
        self.assertEqual(script.call_calls[-1][2], 60000)

    def test_reattach_scan_session_uses_extended_attach_timeout(self) -> None:
        call_order = []
        script = FakeDexDumpScript(
            search_result={"results": [], "stats": {"verified": 0}},
            dump_messages=[],
            call_order=call_order,
        )
        session = FakeDexDumpSession(1234, "com.demo.target", script, call_order)
        device = FakeDexDumpDevice(session, call_order, attach_sessions=[session])
        options = SimpleNamespace(
            message_timeout=1000,
            agent_ready_timeout=10000,
            json=False,
        )

        dexdump._reattach_scan_session(device, 1234, options)

        self.assertEqual(device.attach_calls, [1234])
        self.assertEqual(device.attach_timeout_calls, [60000])

    def test_list_scan_ranges_reattaches_after_enumerate_timeout(self) -> None:
        call_order = []
        first_script = FakeDexDumpScript(
            search_result={"results": [], "stats": {"verified": 0}},
            dump_messages=[],
            call_order=call_order,
            enumerated_ranges=[
                {"base": "0x1000", "size": 0x1000, "protection": "r--", "path": None},
                {"base": "0x2000", "size": 0x1000, "protection": "r--", "path": None},
            ],
            enumerate_results=[TimeoutError("operation timed out")],
        )
        second_script = FakeDexDumpScript(
            search_result={"results": [], "stats": {"verified": 0}},
            dump_messages=[],
            call_order=call_order,
            enumerated_ranges=[
                {"base": "0x1000", "size": 0x1000, "protection": "r--", "path": None},
                {"base": "0x2000", "size": 0x1000, "protection": "r--", "path": None},
            ],
        )
        first_session = FakeDexDumpSession(1234, "com.demo.target", first_script, call_order)
        second_session = FakeDexDumpSession(1234, "com.demo.target", second_script, call_order)
        device = FakeDexDumpDevice(first_session, call_order, attach_sessions=[second_session])
        options = SimpleNamespace(
            message_timeout=1000,
            agent_ready_timeout=10000,
            max_results=0,
            deep=False,
            min_dex_size=0x1000,
            max_dex_size=64 * 1024 * 1024,
            include_system=False,
            debug=False,
            json=False,
        )

        new_device, new_session, new_script, ranges = dexdump._list_scan_ranges(
            device,
            first_session,
            first_script,
            1234,
            options,
        )

        self.assertIs(new_session, second_session)
        self.assertIs(new_script, second_script)
        self.assertEqual(len(ranges), 2)
        self.assertGreaterEqual(len(new_device.attach_calls), 1)

    def test_load_scan_script_retries_after_timeout_with_fresh_script_instance(self) -> None:
        call_order = []
        first_script = FakeDexDumpScript(
            search_result={"results": [], "stats": {"verified": 0}},
            dump_messages=[],
            call_order=call_order,
            create_results=[TimeoutError("operation timed out")],
        )
        second_script = FakeDexDumpScript(
            search_result={"results": [], "stats": {"verified": 0}},
            dump_messages=[],
            call_order=call_order,
        )

        class RetrySession(FakeDexDumpSession):
            def __init__(self, pid: int, process_name: str, scripts, call_order) -> None:
                super().__init__(pid, process_name, scripts[0], call_order)
                self._scripts = list(scripts)

            def create_script(self, source: str, name: str = "script.js"):
                self._call_order.append("session.create_script")
                if not self._scripts:
                    raise AssertionError("no scripts left")
                return self._scripts.pop(0)

        session = RetrySession(1234, "com.demo.target", [first_script, second_script], call_order)
        options = SimpleNamespace(
            message_timeout=5000,
            agent_ready_timeout=10000,
            max_results=0,
        )

        script = dexdump._load_scan_script(session, options=options)

        self.assertIs(script, second_script)
        self.assertEqual(call_order.count("session.create_script"), 2)
        self.assertGreaterEqual(call_order.count("script.create"), 2)
        self.assertIn("script.load", call_order)

    def test_script_setup_timeout_is_wider_for_unlimited_dexdump_scans(self) -> None:
        limited = SimpleNamespace(
            message_timeout=5000,
            agent_ready_timeout=10000,
            max_results=64,
        )
        unlimited = SimpleNamespace(
            message_timeout=5000,
            agent_ready_timeout=10000,
            max_results=0,
        )

        self.assertEqual(dexdump._script_setup_timeout_ms(limited), 30000)
        self.assertEqual(dexdump._script_setup_timeout_ms(unlimited), 120000)

    def test_scan_slice_bytes_is_wider_for_unlimited_dexdump_scans(self) -> None:
        limited = SimpleNamespace(max_results=64)
        unlimited = SimpleNamespace(max_results=0)

        self.assertEqual(dexdump._scan_slice_bytes(limited), 1 * 1024 * 1024)
        self.assertEqual(dexdump._scan_slice_bytes(unlimited), 8 * 1024 * 1024)

    def test_unlimited_dexdump_does_not_isolate_raw_anonymous_ranges_by_default(self) -> None:
        self.assertFalse(dexdump._should_isolate_raw_anonymous_ranges(SimpleNamespace(max_results=0)))
        self.assertTrue(dexdump._should_isolate_raw_anonymous_ranges(SimpleNamespace(isolate_raw_anonymous=True)))

    def test_device_script_does_not_truncate_candidates_by_max_results(self) -> None:
        source = dexdump.load_dexdump_script_source()

        self.assertNotIn("return results.slice(0, options.maxResults);", source)

    def test_device_script_does_not_drop_small_mapless_magic_candidates(self) -> None:
        source = dexdump.load_dexdump_script_source()

        self.assertNotIn("if (!mapsOk && fallbackSize < (256 * 1024)) {", source)

    def test_device_script_keeps_frida_style_range_base_maps_gate(self) -> None:
        source = dexdump.load_dexdump_script_source()

        self.assertIn('} else if (source === "range-base") {\n      if (!mapsOk) {\n        return null;\n      }\n', source)

    def test_device_script_only_checks_range_base_on_primary_slice(self) -> None:
        source = dexdump.load_dexdump_script_source()

        self.assertIn("function isPrimaryScanSlice(range)", source)
        self.assertIn("if (!isPrimaryScanSlice(range)) {", source)

    def test_device_script_does_not_pre_scan_all_range_base_candidates_before_magic(self) -> None:
        source = dexdump.load_dexdump_script_source()

        self.assertNotIn("if (!options.deep) {\n      for (var rangeBaseIndex = startIndex; rangeBaseIndex < endIndex; rangeBaseIndex++) {", source)

    def test_normalize_results_preserves_discovery_order(self) -> None:
        result = dexdump._normalize_results(
            {
                "results": [
                    {
                        "addr": "0x2000",
                        "real_size": 7 * 1024 * 1024,
                        "fallback_size": 7 * 1024 * 1024,
                        "confidence": "low",
                        "maps_ok": False,
                        "ids_ok": False,
                        "target_hint_hit": False,
                    },
                    {
                        "addr": "0x1000",
                        "real_size": 55332,
                        "fallback_size": 55332,
                        "confidence": "high",
                        "maps_ok": True,
                        "ids_ok": True,
                        "target_hint_hit": True,
                    },
                ]
            }
        )

        self.assertEqual([item["addr"] for item in result], ["0x2000", "0x1000"])

    def test_build_search_options_respects_smaller_max_dex_size(self) -> None:
        options = SimpleNamespace(
            deep=False,
            max_results=8,
            min_dex_size=0x1000,
            max_dex_size=8 * 1024 * 1024,
            include_system=False,
            debug=False,
        )

        result = dexdump.build_search_options(options)

        self.assertEqual(result["max_range_size"], 32 * 1024 * 1024)

    def test_build_search_options_preserves_minimum_floor_for_tiny_dex_targets(self) -> None:
        options = SimpleNamespace(
            deep=False,
            max_results=8,
            min_dex_size=0x1000,
            max_dex_size=2 * 1024 * 1024,
            include_system=False,
            debug=False,
        )

        result = dexdump.build_search_options(options)

        self.assertEqual(result["max_range_size"], 8 * 1024 * 1024)

    def test_split_scan_ranges_slices_large_range_into_fixed_windows(self) -> None:
        result = dexdump._split_scan_ranges(
            [
                {
                    "base": "0x1000",
                    "size": 20 * 1024 * 1024,
                    "protection": "r--",
                    "path": "/data/app/demo",
                }
            ],
            1 * 1024 * 1024,
        )

        self.assertEqual(len(result), 20)
        self.assertEqual([item["size"] for item in result[:3]], [1 * 1024 * 1024, 1 * 1024 * 1024, 1 * 1024 * 1024])
        self.assertEqual(result[0]["base"], "0x1000")
        self.assertEqual(result[1]["base"], "0x101000")
        self.assertEqual(result[2]["base"], "0x201000")

    def test_split_scan_ranges_preserves_parent_range_metadata(self) -> None:
        result = dexdump._split_scan_ranges(
            [
                {
                    "base": "0x1000",
                    "size": 3 * 1024 * 1024,
                    "protection": "r--",
                    "path": "/data/app/demo",
                }
            ],
            1 * 1024 * 1024,
        )

        self.assertEqual(result[0]["range_base"], "0x1000")
        self.assertEqual(result[0]["range_size"], 3 * 1024 * 1024)
        self.assertEqual(result[0]["scan_base"], "0x1000")
        self.assertEqual(result[0]["scan_size"], (1 * 1024 * 1024) + 7)
        self.assertEqual(result[1]["range_base"], "0x1000")
        self.assertEqual(result[1]["scan_base"], "0x101000")

    def test_split_scan_ranges_adds_forward_overlap_to_preserve_boundary_matches(self) -> None:
        result = dexdump._split_scan_ranges(
            [
                {
                    "base": "0x1000",
                    "size": 20,
                    "protection": "r--",
                    "path": "/data/app/demo",
                }
            ],
            8,
        )

        self.assertEqual([item["size"] for item in result], [8, 8, 4])
        self.assertEqual([item["scan_size"] for item in result], [15, 12, 4])
        self.assertEqual([item["scan_base"] for item in result], ["0x1000", "0x1008", "0x1010"])

    def test_split_single_scan_range_for_retry_halves_slice_and_preserves_parent_metadata(self) -> None:
        result = dexdump._split_single_scan_range_for_retry(
            {
                "base": "0x101000",
                "size": 1 * 1024 * 1024,
                "range_base": "0x1000",
                "range_size": 3 * 1024 * 1024,
                "scan_base": "0x101000",
                "scan_size": 1 * 1024 * 1024,
                "protection": "r--",
                "path": "/data/app/demo",
            },
            256 * 1024,
        )

        self.assertEqual(len(result), 2)
        self.assertEqual(result[0]["range_base"], "0x1000")
        self.assertEqual(result[0]["range_size"], 3 * 1024 * 1024)
        self.assertEqual(result[0]["scan_base"], "0x101000")
        self.assertEqual(result[0]["scan_size"], (512 * 1024) + 7)
        self.assertEqual(result[1]["scan_base"], "0x181000")
        self.assertEqual(result[1]["scan_size"], 512 * 1024)

    def test_sort_scan_ranges_for_search_uses_early_hit_ordering(self) -> None:
        result = dexdump._sort_scan_ranges_for_search(
            [
                {
                    "base": "0x1000",
                    "size": 0x100000,
                    "range_base": "0x1000",
                    "range_size": 0x300000,
                    "scan_base": "0x1000",
                    "scan_size": 0x100000,
                },
                {
                    "base": "0x101000",
                    "size": 0x100000,
                    "range_base": "0x1000",
                    "range_size": 0x300000,
                    "scan_base": "0x101000",
                    "scan_size": 0x100000,
                },
                {
                    "base": "0x4000",
                    "size": 0x80000,
                    "range_base": "0x4000",
                    "range_size": 0x80000,
                    "scan_base": "0x4000",
                    "scan_size": 0x80000,
                },
            ]
        )

        self.assertEqual([item["base"] for item in result], ["0x1000", "0x4000", "0x101000"])

    def test_sort_scan_ranges_for_search_deprioritizes_raw_anonymous_ranges_when_requested(self) -> None:
        result = dexdump._sort_scan_ranges_for_search(
            [
                {
                    "base": "0x5000",
                    "size": 1044480,
                    "protection": "r--",
                    "path": None,
                },
                {
                    "base": "0x1000",
                    "size": 0x100000,
                    "range_base": "0x1000",
                    "range_size": 0x300000,
                    "scan_base": "0x1000",
                    "scan_size": 0x100000,
                    "protection": "r--",
                    "path": None,
                },
                {
                    "base": "0x101000",
                    "size": 0x100000,
                    "range_base": "0x1000",
                    "range_size": 0x300000,
                    "scan_base": "0x101000",
                    "scan_size": 0x100000,
                    "protection": "r--",
                    "path": None,
                },
            ],
            deprioritize_raw_anonymous=True,
        )

        self.assertEqual([item["base"] for item in result], ["0x1000", "0x101000", "0x5000"])

    def test_plan_scan_batches_isolates_raw_anonymous_ranges_when_requested(self) -> None:
        batches = dexdump._plan_scan_batches(
            [
                {
                    "base": "0x1000",
                    "size": 0x20000,
                    "range_base": "0x1000",
                    "range_size": 0x40000,
                    "scan_base": "0x1000",
                    "scan_size": 0x20000,
                    "path": None,
                },
                {
                    "base": "0x3000",
                    "size": 0x20000,
                    "range_base": "0x1000",
                    "range_size": 0x40000,
                    "scan_base": "0x3000",
                    "scan_size": 0x20000,
                    "path": None,
                },
                {
                    "base": "0x5000",
                    "size": 1044480,
                    "path": None,
                },
                {
                    "base": "0x7000",
                    "size": 0x20000,
                    "range_base": "0x7000",
                    "range_size": 0x20000,
                    "scan_base": "0x7000",
                    "scan_size": 0x20000,
                    "path": None,
                },
            ],
            max_ranges_per_batch=4,
            max_bytes_per_batch=64 * 1024 * 1024,
            isolate_raw_anonymous=True,
        )

        self.assertEqual(
            [[item["base"] for item in batch] for _, batch in batches],
            [["0x1000", "0x3000"], ["0x5000"], ["0x7000"]],
        )

    def test_sort_scan_ranges_for_early_hit_interleaves_first_slices_before_later_slices(self) -> None:
        result = dexdump._sort_scan_ranges_for_early_hit(
            [
                {
                    "base": "0x1000",
                    "size": 0x100000,
                    "range_base": "0x1000",
                    "range_size": 0x300000,
                    "scan_base": "0x1000",
                    "scan_size": 0x100000,
                },
                {
                    "base": "0x101000",
                    "size": 0x100000,
                    "range_base": "0x1000",
                    "range_size": 0x300000,
                    "scan_base": "0x101000",
                    "scan_size": 0x100000,
                },
                {
                    "base": "0x4000",
                    "size": 0x80000,
                    "range_base": "0x4000",
                    "range_size": 0x80000,
                    "scan_base": "0x4000",
                    "scan_size": 0x80000,
                },
            ]
        )

        self.assertEqual([item["base"] for item in result], ["0x1000", "0x4000", "0x101000"])

    def test_export_candidate_rank_prefers_higher_confidence_smaller_candidate(self) -> None:
        ordered = sorted(
            [
                {
                    "addr": "0x3000",
                    "real_size": 10,
                    "size": 10,
                    "confidence": "low",
                    "maps_ok": False,
                    "ids_ok": False,
                    "target_hint_hit": False,
                },
                {
                    "addr": "0x1000",
                    "real_size": 20,
                    "size": 20,
                    "confidence": "high",
                    "maps_ok": True,
                    "ids_ok": True,
                    "target_hint_hit": False,
                },
                {
                    "addr": "0x2000",
                    "real_size": 5,
                    "size": 5,
                    "confidence": "high",
                    "maps_ok": True,
                    "ids_ok": True,
                    "target_hint_hit": False,
                },
            ],
            key=dexdump._export_candidate_rank,
        )

        self.assertEqual([item["addr"] for item in ordered], ["0x2000", "0x1000", "0x3000"])

    def test_export_candidate_rank_deprioritizes_pathologically_truncated_target_hint_candidate(self) -> None:
        ordered = sorted(
            [
                {
                    "addr": "0x1000",
                    "real_size": 55332,
                    "size": 55332,
                    "declared_size": 13857572,
                    "fallback_size": 13860816,
                    "confidence": "high",
                    "maps_ok": True,
                    "ids_ok": True,
                    "target_hint_hit": True,
                },
                {
                    "addr": "0x2000",
                    "real_size": 3907576,
                    "size": 3907576,
                    "declared_size": 3907576,
                    "fallback_size": 3907584,
                    "confidence": "high",
                    "maps_ok": True,
                    "ids_ok": True,
                    "target_hint_hit": False,
                },
                {
                    "addr": "0x3000",
                    "real_size": 6957836,
                    "size": 6957836,
                    "declared_size": 6957836,
                    "fallback_size": 6959104,
                    "confidence": "high",
                    "maps_ok": True,
                    "ids_ok": True,
                    "target_hint_hit": False,
                },
            ],
            key=dexdump._export_candidate_rank,
        )

        self.assertEqual([item["addr"] for item in ordered], ["0x2000", "0x3000", "0x1000"])

    def test_export_candidate_rank_preserves_discovery_order_for_same_quality_non_truncated_candidates(self) -> None:
        ordered = sorted(
            [
                {
                    "addr": "0x1000",
                    "real_size": 3907576,
                    "size": 3907576,
                    "declared_size": 3907576,
                    "fallback_size": 3907584,
                    "confidence": "high",
                    "maps_ok": True,
                    "ids_ok": True,
                    "target_hint_hit": False,
                },
                {
                    "addr": "0x2000",
                    "real_size": 6957836,
                    "size": 6957836,
                    "declared_size": 6957836,
                    "fallback_size": 6959104,
                    "confidence": "high",
                    "maps_ok": True,
                    "ids_ok": True,
                    "target_hint_hit": False,
                },
            ],
            key=dexdump._export_candidate_rank,
        )

        self.assertEqual([item["addr"] for item in ordered], ["0x1000", "0x2000"])

    def test_select_export_candidates_prefers_size_band_diversity_before_fill(self) -> None:
        candidates = [
            {
                "addr": "0x1000",
                "real_size": 9899528,
                "size": 9899528,
                "declared_size": 9899528,
                "fallback_size": 9901788,
                "confidence": "high",
                "maps_ok": True,
                "ids_ok": True,
                "target_hint_hit": False,
            },
            {
                "addr": "0x2000",
                "real_size": 9625540,
                "size": 9625540,
                "declared_size": 9625540,
                "fallback_size": 9625556,
                "confidence": "high",
                "maps_ok": True,
                "ids_ok": True,
                "target_hint_hit": False,
            },
            {
                "addr": "0x3000",
                "real_size": 9525468,
                "size": 9525468,
                "declared_size": 9525468,
                "fallback_size": 9527268,
                "confidence": "high",
                "maps_ok": True,
                "ids_ok": True,
                "target_hint_hit": False,
            },
            {
                "addr": "0x4000",
                "real_size": 6957836,
                "size": 6957836,
                "declared_size": 6957836,
                "fallback_size": 6959104,
                "confidence": "high",
                "maps_ok": True,
                "ids_ok": True,
                "target_hint_hit": False,
            },
            {
                "addr": "0x5000",
                "real_size": 5348352,
                "size": 5348352,
                "declared_size": 5348352,
                "fallback_size": 5351592,
                "confidence": "high",
                "maps_ok": True,
                "ids_ok": True,
                "target_hint_hit": False,
            },
            {
                "addr": "0x6000",
                "real_size": 4970972,
                "size": 4970972,
                "declared_size": 4970972,
                "fallback_size": 4971180,
                "confidence": "high",
                "maps_ok": True,
                "ids_ok": True,
                "target_hint_hit": False,
            },
            {
                "addr": "0x7000",
                "real_size": 3907576,
                "size": 3907576,
                "declared_size": 3907576,
                "fallback_size": 3907584,
                "confidence": "high",
                "maps_ok": True,
                "ids_ok": True,
                "target_hint_hit": False,
            },
            {
                "addr": "0x6800",
                "real_size": 3614496,
                "size": 3614496,
                "declared_size": 3614496,
                "fallback_size": 3616584,
                "confidence": "high",
                "maps_ok": True,
                "ids_ok": True,
                "target_hint_hit": False,
            },
        ]

        result = dexdump._select_export_candidates(candidates, max_results=4)

        self.assertEqual(
            [item["addr"] for item in result],
            ["0x7000", "0x6000", "0x5000", "0x4000"],
        )

    def test_collects_binary_chunks_for_matching_token(self) -> None:
        script = FakeDexDumpScript(
            search_result={},
            dump_messages=[
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"ABCD",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"EFGH",
                ),
            ],
            call_order=[],
        )

        result = dexdump.collect_dump_bytes(
            script,
            token="dump-0001",
            expected_chunks=2,
            timeout_ms=1000,
        )

        self.assertEqual(result, b"ABCDEFGH")

    def test_candidate_export_sizes_include_declared_size_in_deep_mode(self) -> None:
        options = SimpleNamespace(deep=True)

        result = dexdump._candidate_export_sizes(
            {
                "size": 1172,
                "declared_size": 1172,
                "real_size": 2395081,
                "fallback_size": 2395081,
            },
            options,
        )

        self.assertEqual(result, [2395081, 1172])

    def test_candidate_export_sizes_keep_non_deep_mode_single_primary_size(self) -> None:
        options = SimpleNamespace(deep=False)

        result = dexdump._candidate_export_sizes(
            {
                "size": 1172,
                "declared_size": 1172,
                "real_size": 2395081,
                "fallback_size": 2395081,
            },
            options,
        )

        self.assertEqual(result, [2395081])

    def test_candidate_export_sizes_prioritize_expected_size_for_pathologically_truncated_non_deep_candidate(self) -> None:
        options = SimpleNamespace(deep=False)

        result = dexdump._candidate_export_sizes(
            {
                "size": 55332,
                "declared_size": 13857572,
                "real_size": 55332,
                "fallback_size": 13860816,
            },
            options,
        )

        self.assertEqual(result, [55332])

    def test_attach_mode_writes_dex_and_metadata(self) -> None:
        call_order = []
        script = FakeDexDumpScript(
            search_result={
                "results": [
                    {
                        "addr": "0x1000",
                        "size": 8,
                        "declared_size": 12,
                        "real_size": 8,
                        "fallback_size": 16,
                        "source": "magic-scan",
                        "deep": False,
                        "header_ok": True,
                        "maps_ok": True,
                        "ids_ok": True,
                        "target_hint_hit": False,
                        "confidence": "high",
                    }
                ],
                "stats": {"verified": 1},
            },
            dump_messages=[
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"000\x00",
                ),
            ],
            call_order=call_order,
            enumerated_ranges=[{"base": "0x1000", "size": 8, "protection": "r--", "path": None}],
        )
        session = FakeDexDumpSession(1337, "com.demo.target", script, call_order)
        device = FakeDexDumpDevice(session, call_order)
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=True,
                sleep_ms=0,
                fix_header=False,
                dedupe="md5",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=64,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=48,
                include_system=False,
                debug=False,
                fast_full_scan=False,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=stdout, stderr=stderr)

            classes_path = os.path.join(temp_dir, "classes.dex")
            metadata_path = os.path.join(temp_dir, "metadata.json")
            self.assertTrue(os.path.exists(classes_path))
            self.assertTrue(os.path.exists(metadata_path))
            with open(classes_path, "rb") as handle:
                self.assertEqual(handle.read(), b"dex\n000\x00")
            with open(metadata_path, "r", encoding="utf-8") as handle:
                metadata = json.load(handle)
            self.assertEqual(metadata["target"], "com.demo.target")
            self.assertEqual(metadata["mode"], "attach")
            self.assertEqual(len(metadata["artifacts"]), 1)
            self.assertNotIn("candidates", metadata)
            self.assertEqual(metadata["artifacts"][0]["declared_size"], 12)
            self.assertEqual(metadata["artifacts"][0]["real_size"], 8)
            self.assertEqual(metadata["artifacts"][0]["fallback_size"], 16)
            self.assertTrue(metadata["artifacts"][0]["maps_ok"])
            self.assertTrue(metadata["artifacts"][0]["ids_ok"])
            self.assertFalse(metadata["artifacts"][0]["target_hint_hit"])
            self.assertGreaterEqual(len(device.attach_calls), 1)
            self.assertEqual(result["artifact_count"], 1)
            self.assertIn("Wrote 1 dex artifact(s)", stdout.getvalue())

    def test_attach_mode_debug_metadata_includes_discovered_candidate_ledger(self) -> None:
        call_order = []
        script = FakeDexDumpScript(
            search_result={
                "results": [
                    {
                        "addr": "0x1000",
                        "size": 8,
                        "declared_size": 12,
                        "real_size": 8,
                        "fallback_size": 16,
                        "source": "magic-scan",
                        "deep": False,
                        "header_ok": True,
                        "maps_ok": True,
                        "ids_ok": True,
                        "target_hint_hit": False,
                        "confidence": "high",
                        "range_base": "0x1000",
                        "range_size": 32,
                        "range_path": None,
                    }
                ],
                "stats": {"verified": 1},
            },
            dump_messages=[
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"000\x00",
                ),
            ],
            call_order=call_order,
            enumerated_ranges=[{"base": "0x1000", "size": 8, "protection": "r--", "path": None}],
        )
        session = FakeDexDumpSession(1337, "com.demo.target", script, call_order)
        device = FakeDexDumpDevice(session, call_order)

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=False,
                sleep_ms=0,
                fix_header=False,
                dedupe="md5",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=64,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=48,
                include_system=False,
                debug=True,
                fast_full_scan=False,
                json=False,
            )

            dexdump.run_dexdump(options, device, stdout=io.StringIO(), stderr=io.StringIO())

            with open(os.path.join(temp_dir, "metadata.json"), "r", encoding="utf-8") as handle:
                metadata = json.load(handle)

            self.assertIn("candidates", metadata)
            self.assertEqual(len(metadata["candidates"]), 1)
            self.assertEqual(metadata["candidates"][0]["address"], "0x1000")
            self.assertEqual(metadata["candidates"][0]["declared_size"], 12)
            self.assertEqual(metadata["candidates"][0]["real_size"], 8)
            self.assertEqual(metadata["candidates"][0]["fallback_size"], 16)
            self.assertEqual(metadata["candidates"][0]["range_base"], "0x1000")
            self.assertEqual(metadata["candidates"][0]["range_size"], 32)

    def test_attach_mode_skips_failed_export_size_and_keeps_successful_dump(self) -> None:
        call_order = []
        script = FakeDexDumpScript(
            search_result={
                "results": [
                    {
                        "addr": "0x1000",
                        "size": 8,
                        "declared_size": 16,
                        "real_size": 8,
                        "fallback_size": 8,
                        "source": "deep-scan",
                        "deep": True,
                        "header_ok": True,
                        "maps_ok": True,
                        "ids_ok": True,
                        "target_hint_hit": False,
                        "confidence": "high",
                    }
                ],
                "stats": {"verified": 1},
            },
            dump_messages=[
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"000\x00",
                ),
            ],
            call_order=call_order,
            enumerated_ranges=[{"base": "0x1000", "size": 8, "protection": "r--", "path": None}],
            begin_dump_results=[
                {"token": "dump-0001", "size": 8, "chunk_size": 4, "chunks": 2},
                RuntimeError("readByteArray unreadable pointer"),
            ],
        )
        session = FakeDexDumpSession(1337, "com.demo.target", script, call_order)
        device = FakeDexDumpDevice(session, call_order)
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=True,
                sleep_ms=0,
                fix_header=False,
                dedupe="addr",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=64,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=48,
                include_system=False,
                debug=False,
                fast_full_scan=False,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=stdout, stderr=stderr)

            self.assertTrue(os.path.exists(os.path.join(temp_dir, "classes.dex")))
            self.assertEqual(result["artifact_count"], 1)
            self.assertIn("Skipping dex candidate at 0x1000 size=16", stderr.getvalue())

    def test_attach_mode_dedupes_on_fixed_header_bytes_for_md5_mode(self) -> None:
        call_order = []
        script = FakeDexDumpScript(
            search_result={
                "results": [
                    {
                        "addr": "0x1000",
                        "size": 8,
                        "declared_size": 8,
                        "real_size": 8,
                        "fallback_size": 8,
                        "source": "magic-scan",
                        "deep": False,
                        "header_ok": False,
                        "maps_ok": True,
                        "ids_ok": True,
                        "target_hint_hit": False,
                        "confidence": "high",
                    },
                    {
                        "addr": "0x2000",
                        "size": 8,
                        "declared_size": 8,
                        "real_size": 8,
                        "fallback_size": 8,
                        "source": "magic-scan",
                        "deep": False,
                        "header_ok": False,
                        "maps_ok": True,
                        "ids_ok": True,
                        "target_hint_hit": False,
                        "confidence": "high",
                    },
                ],
                "stats": {"verified": 2},
            },
            dump_messages=[
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 1,
                            "size": 8,
                            "eof": True,
                        }
                    ),
                    b"bad!\x00\x00\x00\x00",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0002",
                            "index": 0,
                            "chunks": 1,
                            "size": 8,
                            "eof": True,
                        }
                    ),
                    b"bad!\x00\x00\x00\x00",
                ),
            ],
            call_order=call_order,
            enumerated_ranges=[{"base": "0x1000", "size": 8, "protection": "r--", "path": None}],
            begin_dump_results=[
                {"token": "dump-0001", "size": 8, "chunk_size": 8, "chunks": 1},
                {"token": "dump-0002", "size": 8, "chunk_size": 8, "chunks": 1},
            ],
        )
        session = FakeDexDumpSession(1337, "com.demo.target", script, call_order)
        device = FakeDexDumpDevice(session, call_order)

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=False,
                sleep_ms=0,
                fix_header=True,
                dedupe="md5",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=64,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=48,
                include_system=False,
                debug=False,
                fast_full_scan=False,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=io.StringIO(), stderr=io.StringIO())

            self.assertEqual(result["artifact_count"], 1)
            self.assertTrue(os.path.exists(os.path.join(temp_dir, "classes.dex")))
            self.assertFalse(os.path.exists(os.path.join(temp_dir, "classes2.dex")))

    def test_spawn_mode_resumes_before_scanning(self) -> None:
        call_order = []
        enumerate_script = FakeDexDumpScript(
            search_result={"results": [], "stats": {"verified": 0}},
            dump_messages=[],
            call_order=call_order,
            enumerated_ranges=[],
        )
        spawn_script = FakeDexDumpScript(
            search_result={"results": [], "stats": {"verified": 0}},
            dump_messages=[],
            call_order=call_order,
        )
        spawn_session = FakeDexDumpSession(2442, "com.demo.target", spawn_script, call_order)
        device = FakeDexDumpDevice(
            spawn_session,
            call_order,
            spawn_session=spawn_session,
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package="com.demo.target",
                target=None,
                output_dir=temp_dir,
                deep=False,
                sleep_ms=0,
                fix_header=False,
                dedupe="md5",
                agent_ready_timeout=9000,
                message_timeout=1000,
                max_results=64,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=48,
                include_system=False,
                debug=False,
                fast_full_scan=False,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=io.StringIO(), stderr=io.StringIO())

            self.assertEqual(device.spawn_calls, [("com.demo.target", [], 9000)])
            self.assertEqual(device.resume_calls, [2442])
            self.assertEqual(
                call_order[:5],
                [
                    "spawn",
                    "resume",
                    "session.create_script",
                    "script.create",
                    "script.load",
                ],
            )
            self.assertIn("script.call:enumerateranges", call_order)
            self.assertIn("script.unload", call_order)
            self.assertIn("session.detach", call_order)
            self.assertEqual(device.attach_calls, [])
            self.assertEqual(result["artifact_count"], 0)

    def test_attach_mode_uses_full_process_scan_for_small_scan_sets(self) -> None:
        call_order = []
        script = FakeDexDumpScript(
            search_result={
                "results": [
                    {
                        "addr": "0x1000",
                        "size": 8,
                        "declared_size": 8,
                        "real_size": 8,
                        "fallback_size": 8,
                        "source": "magic-scan",
                        "deep": False,
                        "header_ok": True,
                        "maps_ok": True,
                        "ids_ok": True,
                        "target_hint_hit": False,
                        "confidence": "high",
                    }
                ],
                "stats": {"verified": 1, "ranges_total": 1, "ranges_scanned": 1},
            },
            dump_messages=[
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"000\x00",
                ),
            ],
            call_order=call_order,
            enumerated_ranges=[{"base": "0x1000", "size": 0x1000, "protection": "r--", "path": None}],
        )
        session = FakeDexDumpSession(1888, "com.demo.target", script, call_order)
        device = FakeDexDumpDevice(session, call_order)

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=False,
                sleep_ms=0,
                fix_header=False,
                dedupe="md5",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=64,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=48,
                include_system=False,
                debug=False,
                fast_full_scan=False,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=io.StringIO(), stderr=io.StringIO())

            search_calls = [call for call in script.call_calls if call[0] == "searchdex"]
            enumerate_calls = [call for call in script.call_calls if call[0] == "enumerateranges"]
            self.assertEqual(len(search_calls), 1)
            self.assertEqual(len(enumerate_calls), 1)
            self.assertNotIn("explicit_ranges", search_calls[0][1][0])
            self.assertEqual(result["artifact_count"], 1)

    def test_attach_mode_fast_full_scan_does_not_reexport_same_candidate_twice(self) -> None:
        call_order = []
        script = FakeDexDumpScript(
            search_result={
                "results": [
                    {
                        "addr": "0x1000",
                        "size": 8,
                        "declared_size": 8,
                        "real_size": 8,
                        "fallback_size": 8,
                        "source": "magic-scan",
                        "deep": False,
                        "header_ok": True,
                        "maps_ok": True,
                        "ids_ok": True,
                        "target_hint_hit": False,
                        "confidence": "high",
                    }
                ],
                "stats": {"verified": 1, "ranges_total": 1, "ranges_scanned": 1},
            },
            dump_messages=[
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"000\x00",
                ),
            ],
            call_order=call_order,
            enumerated_ranges=[{"base": "0x1000", "size": 0x1000, "protection": "r--", "path": None}],
        )
        session = FakeDexDumpSession(1888, "com.demo.target", script, call_order)
        device = FakeDexDumpDevice(session, call_order)

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=False,
                sleep_ms=0,
                fix_header=False,
                dedupe="addr",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=64,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=48,
                include_system=False,
                debug=False,
                fast_full_scan=True,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=io.StringIO(), stderr=io.StringIO())

            self.assertEqual(result["artifact_count"], 1)
            self.assertEqual(len(result["metadata"]["artifacts"]), 1)

    def test_attach_mode_skips_full_process_scan_for_large_scan_sets(self) -> None:
        call_order = []
        script = FakeDexDumpScript(
            search_result=[
                {
                    "results": [
                        {
                            "addr": "0x1000",
                            "size": 8,
                            "declared_size": 8,
                            "real_size": 8,
                            "fallback_size": 8,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        }
                    ],
                    "stats": {"verified": 1, "ranges_total": 1, "ranges_scanned": 1},
                },
            ],
            dump_messages=[
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"000\x00",
                ),
            ],
            call_order=call_order,
            enumerated_ranges=[
                {"base": f"0x{0x1000 + (i * 0x1000):x}", "size": 0x1000, "protection": "r--", "path": None}
                for i in range(128)
            ],
        )
        session = FakeDexDumpSession(1999, "com.demo.target", script, call_order)
        device = FakeDexDumpDevice(session, call_order)
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=False,
                sleep_ms=0,
                fix_header=False,
                dedupe="md5",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=64,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=2,
                include_system=False,
                debug=False,
                fast_full_scan=False,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=stdout, stderr=stderr)

            search_calls = [call for call in script.call_calls if call[0] == "searchdex"]
            enumerate_calls = [call for call in script.call_calls if call[0] == "enumerateranges"]
            self.assertGreaterEqual(len(search_calls), 1)
            self.assertEqual(len(enumerate_calls), 1)
            for _, args, _ in search_calls:
                self.assertIn("explicit_ranges", args[0])
            self.assertNotIn("Full-process scan timed out; falling back to windowed scan", stderr.getvalue())
            self.assertEqual(result["artifact_count"], 1)

    def test_attach_mode_scans_multiple_windows_in_single_attach(self) -> None:
        call_order = []
        enumerate_script = FakeDexDumpScript(
            search_result=[
                {"results": [], "stats": {"ranges_total": 100, "ranges_scanned": 48, "verified": 0}},
                {"results": [], "stats": {"ranges_total": 100, "ranges_scanned": 48, "verified": 0}},
                {"results": [], "stats": {"ranges_total": 100, "ranges_scanned": 4, "verified": 0}},
            ],
            dump_messages=[],
            call_order=call_order,
            enumerated_ranges=[
                {"base": f"0x{1000 + i:x}", "size": 0x1000, "protection": "r--", "path": None}
                for i in range(100)
            ],
        )
        enumerate_session = FakeDexDumpSession(1555, "com.demo.target", enumerate_script, call_order)
        device = FakeDexDumpDevice(
            enumerate_session,
            call_order,
            attach_sessions=[enumerate_session],
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=False,
                sleep_ms=0,
                fix_header=False,
                dedupe="md5",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=64,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=48,
                include_system=False,
                debug=False,
                fast_full_scan=False,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=io.StringIO(), stderr=io.StringIO())

            self.assertGreaterEqual(len(device.attach_calls), 1)
            self.assertEqual(result["metadata"]["stats"]["ranges_total"], 100)
            self.assertEqual(result["metadata"]["stats"]["ranges_scanned"], 100)

    def test_attach_mode_splits_scan_windows_by_total_range_bytes(self) -> None:
        call_order = []
        enumerate_script = FakeDexDumpScript(
            search_result=[
                {"results": [], "stats": {"ranges_total": 5, "ranges_scanned": 8, "verified": 0}},
                {"results": [], "stats": {"ranges_total": 5, "ranges_scanned": 5, "verified": 0}},
            ],
            dump_messages=[],
            call_order=call_order,
            enumerated_ranges=[
                {"base": "0x1000", "size": 40 * 1024 * 1024, "protection": "r--", "path": None},
                {"base": "0x2000", "size": 20 * 1024 * 1024, "protection": "r--", "path": None},
                {"base": "0x3000", "size": 8 * 1024 * 1024, "protection": "r--", "path": None},
                {"base": "0x4000", "size": 8 * 1024 * 1024, "protection": "r--", "path": None},
                {"base": "0x5000", "size": 8 * 1024 * 1024, "protection": "r--", "path": None},
            ],
        )
        enumerate_session = FakeDexDumpSession(1666, "com.demo.target", enumerate_script, call_order)
        device = FakeDexDumpDevice(
            enumerate_session,
            call_order,
            attach_sessions=[enumerate_session],
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=False,
                sleep_ms=0,
                fix_header=False,
                dedupe="md5",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=64,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=8,
                scan_window_max_bytes=64 * 1024 * 1024,
                include_system=False,
                debug=False,
                fast_full_scan=False,
                json=False,
            )

            dexdump.run_dexdump(options, device, stdout=io.StringIO(), stderr=io.StringIO())

            search_calls = [call for call in enumerate_script.call_calls if call[0] == "searchdex"]
            self.assertGreaterEqual(len(search_calls), 2)
            self.assertEqual(len([call for call in enumerate_script.call_calls if call[0] == "enumerateranges"]), 1)
            for _, args, _ in search_calls:
                batch = args[0]["explicit_ranges"]
                total_bytes = sum(int(item["size"]) for item in batch)
                self.assertLessEqual(total_bytes, 64 * 1024 * 1024)

    def test_attach_mode_aborts_after_timed_out_scan_window(self) -> None:
        call_order = []
        enumerate_script = FakeDexDumpScript(
            search_result=[
                TimeoutError("operation timed out"),
            ],
            dump_messages=[],
            call_order=call_order,
            enumerated_ranges=[
                {"base": f"0x{1000 + i:x}", "size": 0x1000, "protection": "r--", "path": None}
                for i in range(128)
            ],
        )
        enumerate_session = FakeDexDumpSession(1666, "com.demo.target", enumerate_script, call_order)
        device = FakeDexDumpDevice(
            enumerate_session,
            call_order,
            attach_sessions=[enumerate_session],
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=False,
                sleep_ms=0,
                fix_header=False,
                dedupe="md5",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=64,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=8,
                include_system=False,
                debug=False,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=io.StringIO(), stderr=io.StringIO())

            search_calls = [call for call in enumerate_script.call_calls if call[0] == "searchdex"]
            self.assertGreaterEqual(len(search_calls), 15)
            self.assertGreaterEqual(len(device.attach_calls), 2)
            self.assertEqual(result["metadata"]["stats"]["errors"], 240)
            self.assertFalse(result["scan_aborted"])
            self.assertIsNone(result["scan_abort_reason"])

    def test_attach_mode_skips_single_range_timeout_after_reload(self) -> None:
        call_order = []
        enumerate_script = FakeDexDumpScript(
            search_result=[TimeoutError("operation timed out")],
            dump_messages=[],
            call_order=call_order,
            enumerated_ranges=[
                {"base": f"0x{1000 + i:x}", "size": 0x1000, "protection": "r--", "path": None}
                for i in range(128)
            ],
        )
        enumerate_session = FakeDexDumpSession(1666, "com.demo.target", enumerate_script, call_order)
        device = FakeDexDumpDevice(
            enumerate_session,
            call_order,
            attach_sessions=[enumerate_session],
        )
        stderr = io.StringIO()

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=True,
                sleep_ms=0,
                fix_header=False,
                dedupe="md5",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=64,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=2,
                include_system=False,
                debug=False,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=io.StringIO(), stderr=stderr)

            self.assertEqual(result["artifact_count"], 0)
            self.assertEqual(result["metadata"]["stats"]["errors"], 192)
            self.assertFalse(result["scan_aborted"])
            self.assertIn("timed out at minimum batch size; skipping range block 0..1", stderr.getvalue())

    def test_attach_mode_skips_rpc_timeout_error_window(self) -> None:
        call_order = []
        enumerate_script = FakeDexDumpScript(
            search_result=[
                RuntimeError("operation timed out"),
            ],
            dump_messages=[],
            call_order=call_order,
            enumerated_ranges=[
                {"base": f"0x{1000 + i:x}", "size": 0x1000, "protection": "r--", "path": None}
                for i in range(128)
            ],
        )
        enumerate_session = FakeDexDumpSession(1666, "com.demo.target", enumerate_script, call_order)
        device = FakeDexDumpDevice(
            enumerate_session,
            call_order,
            attach_sessions=[enumerate_session],
        )
        stderr = io.StringIO()

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=False,
                sleep_ms=0,
                fix_header=False,
                dedupe="md5",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=64,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=2,
                include_system=False,
                debug=False,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=io.StringIO(), stderr=stderr)

            self.assertEqual(result["artifact_count"], 0)
            self.assertFalse(result["scan_aborted"])
            self.assertIn("timed out at minimum batch size; skipping range block 0..1", stderr.getvalue())

    def test_attach_mode_preserves_incremental_exports_when_reattach_retry_fails(self) -> None:
        call_order = []
        enumerate_script = FakeDexDumpScript(
            search_result=[
                {
                    "results": [
                        {
                            "addr": "0x1000",
                            "size": 8,
                            "declared_size": 8,
                            "real_size": 8,
                            "fallback_size": 8,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        }
                    ],
                    "stats": {"verified": 1},
                },
                TimeoutError("operation timed out"),
            ],
            dump_messages=[
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"111\x00",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"222\x00",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"333\x00",
                ),
            ],
            call_order=call_order,
            enumerated_ranges=[
                {"base": "0x1000", "size": 1 * 1024 * 1024, "protection": "r--", "path": None},
                {"base": "0x2000", "size": 1 * 1024 * 1024, "protection": "r--", "path": None},
                {"base": "0x3000", "size": 1 * 1024 * 1024, "protection": "r--", "path": None},
                {"base": "0x4000", "size": 1 * 1024 * 1024, "protection": "r--", "path": None},
            ],
        )
        session = FakeDexDumpSession(1888, "com.demo.target", enumerate_script, call_order)
        device = FakeDexDumpDevice(
            session,
            call_order,
            attach_sessions=[
                session,
                TimeoutError("operation timed out"),
                TimeoutError("operation timed out"),
                TimeoutError("operation timed out"),
            ],
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=True,
                sleep_ms=0,
                fix_header=False,
                dedupe="addr",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=0,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=2,
                include_system=False,
                debug=False,
                fast_full_scan=False,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=io.StringIO(), stderr=io.StringIO())

            self.assertFalse(result["scan_aborted"])
            self.assertEqual(result["artifact_count"], 1)
            self.assertGreaterEqual(result["metadata"]["stats"]["ranges_skipped"], 1)
            self.assertEqual(
                [artifact["address"] for artifact in result["metadata"]["artifacts"]],
                ["0x1000"],
            )

    def test_attach_mode_best_effort_continues_when_followup_reattach_fails_in_unlimited_mode(self) -> None:
        call_order = []
        enumerate_script = FakeDexDumpScript(
            search_result=[
                TimeoutError("operation timed out"),
                {
                    "results": [
                        {
                            "addr": "0x1000",
                            "size": 8,
                            "declared_size": 8,
                            "real_size": 8,
                            "fallback_size": 8,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        }
                    ],
                    "stats": {"ranges_total": 2, "ranges_scanned": 1, "verified": 1},
                },
                {
                    "results": [
                        {
                            "addr": "0x2000",
                            "size": 8,
                            "declared_size": 8,
                            "real_size": 8,
                            "fallback_size": 8,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        }
                    ],
                    "stats": {"ranges_total": 2, "ranges_scanned": 1, "verified": 1},
                },
            ],
            dump_messages=[
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"777\x00",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"888\x00",
                ),
            ],
            call_order=call_order,
            enumerated_ranges=[
                {"base": "0x1000", "size": 1 * 1024 * 1024, "protection": "r--", "path": None},
                {"base": "0x2000", "size": 1 * 1024 * 1024, "protection": "r--", "path": None},
                {"base": "0x3000", "size": 1 * 1024 * 1024, "protection": "r--", "path": None},
                {"base": "0x4000", "size": 1 * 1024 * 1024, "protection": "r--", "path": None},
            ],
        )
        session = FakeDexDumpSession(1889, "com.demo.target", enumerate_script, call_order)
        device = FakeDexDumpDevice(
            session,
            call_order,
            attach_sessions=[
                session,
                TimeoutError("operation timed out"),
                TimeoutError("operation timed out"),
                TimeoutError("operation timed out"),
            ],
        )
        stderr = io.StringIO()

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=True,
                sleep_ms=0,
                fix_header=False,
                dedupe="addr",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=0,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=2,
                include_system=False,
                debug=False,
                fast_full_scan=False,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=io.StringIO(), stderr=stderr)

            self.assertFalse(result["scan_aborted"])
            self.assertGreaterEqual(result["artifact_count"], 1)
            self.assertGreaterEqual(result["metadata"]["stats"]["ranges_skipped"], 1)
            self.assertIn(
                "skipping range block and continuing",
                stderr.getvalue(),
            )
            self.assertIn("0x1000", [artifact["address"] for artifact in result["metadata"]["artifacts"]])

    def test_attach_mode_retries_timed_out_minimum_slice_with_smaller_subslices(self) -> None:
        call_order = []
        enumerate_script = FakeDexDumpScript(
            search_result=[
                TimeoutError("operation timed out"),
                {
                    "results": [
                        {
                            "addr": "0x1000",
                            "size": 8,
                            "declared_size": 8,
                            "real_size": 8,
                            "fallback_size": 8,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        }
                    ],
                    "stats": {"ranges_total": 1, "ranges_scanned": 1, "verified": 1},
                },
                {
                    "results": [],
                    "stats": {"ranges_total": 1, "ranges_scanned": 1, "verified": 0},
                },
            ],
            dump_messages=[
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"555\x00",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"666\x00",
                ),
            ],
            call_order=call_order,
            enumerated_ranges=[
                {"base": "0x1000", "size": 1 * 1024 * 1024, "protection": "r--", "path": None},
            ],
        )
        session = FakeDexDumpSession(1999, "com.demo.target", enumerate_script, call_order)
        device = FakeDexDumpDevice(
            session,
            call_order,
            attach_sessions=[session, session],
        )
        stderr = io.StringIO()

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=True,
                sleep_ms=0,
                fix_header=False,
                dedupe="addr",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=0,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=1,
                include_system=False,
                debug=False,
                fast_full_scan=False,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=io.StringIO(), stderr=stderr)

            self.assertFalse(result["scan_aborted"])
            self.assertEqual(result["artifact_count"], 1)
            self.assertEqual(result["metadata"]["stats"]["ranges_skipped"], 0)
            self.assertIn("retrying timed-out slice as 2 smaller sub-slice(s)", stderr.getvalue())
            self.assertEqual(
                [artifact["address"] for artifact in result["metadata"]["artifacts"]],
                ["0x1000"],
            )

    def test_attach_mode_retries_timed_out_scan_window_with_smaller_batches(self) -> None:
        call_order = []
        enumerate_script = FakeDexDumpScript(
            search_result=[
                TimeoutError("operation timed out"),
                {
                    "results": [
                        {
                            "addr": "0x1000",
                            "size": 8,
                            "declared_size": 8,
                            "real_size": 8,
                            "fallback_size": 8,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        }
                    ],
                    "stats": {"ranges_total": 2, "ranges_scanned": 1, "verified": 1},
                },
                {
                    "results": [
                        {
                            "addr": "0x2000",
                            "size": 8,
                            "declared_size": 8,
                            "real_size": 8,
                            "fallback_size": 8,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        }
                    ],
                    "stats": {"ranges_total": 2, "ranges_scanned": 1, "verified": 1},
                },
            ],
            dump_messages=[
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"000\x00",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"000\x00",
                ),
            ],
            call_order=call_order,
            enumerated_ranges=[
                {"base": "0x1000", "size": 0x1000, "protection": "r--", "path": None},
                {"base": "0x2000", "size": 0x1000, "protection": "r--", "path": None},
            ],
        )
        session = FakeDexDumpSession(1666, "com.demo.target", enumerate_script, call_order)
        device = FakeDexDumpDevice(session, call_order, attach_sessions=[session])
        stderr = io.StringIO()

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=True,
                sleep_ms=0,
                fix_header=False,
                dedupe="addr",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=64,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=2,
                include_system=False,
                debug=False,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=io.StringIO(), stderr=stderr)

            search_calls = [call for call in enumerate_script.call_calls if call[0] == "searchdex"]
            self.assertEqual(len(search_calls), 3)
            self.assertEqual(result["artifact_count"], 2)
            self.assertIn("retrying as 2 smaller batch(es)", stderr.getvalue())

    def test_attach_mode_reattaches_after_timeout_before_retrying(self) -> None:
        call_order = []
        enumerate_script = FakeDexDumpScript(
            search_result=[
                TimeoutError("operation timed out"),
                {
                    "results": [
                        {
                            "addr": "0x1000",
                            "size": 8,
                            "declared_size": 8,
                            "real_size": 8,
                            "fallback_size": 8,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        }
                    ],
                    "stats": {"ranges_total": 1, "ranges_scanned": 1, "verified": 1},
                },
            ],
            dump_messages=[
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"000\x00",
                ),
            ],
            call_order=call_order,
            enumerated_ranges=[
                {"base": "0x1000", "size": 20 * 1024 * 1024, "protection": "r--", "path": None},
                {"base": "0x2000", "size": 20 * 1024 * 1024, "protection": "r--", "path": None},
            ],
        )
        first_session = FakeDexDumpSession(1779, "com.demo.target", enumerate_script, call_order)
        second_session = FakeDexDumpSession(1780, "com.demo.target", enumerate_script, call_order)
        device = FakeDexDumpDevice(first_session, call_order, attach_sessions=[second_session])

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=True,
                sleep_ms=0,
                fix_header=False,
                dedupe="addr",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=64,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=2,
                include_system=False,
                debug=False,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=io.StringIO(), stderr=io.StringIO())

            self.assertGreaterEqual(len(device.attach_calls), 2)
            self.assertEqual(result["artifact_count"], 1)

    def test_attach_mode_stops_scanning_after_reaching_export_limit(self) -> None:
        call_order = []
        enumerate_script = FakeDexDumpScript(
            search_result=[
                {
                    "results": [
                        {
                            "addr": "0x1000",
                            "size": 8,
                            "declared_size": 8,
                            "real_size": 8,
                            "fallback_size": 8,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        }
                    ],
                    "stats": {"verified": 1},
                },
                {
                    "results": [
                        {
                            "addr": "0x2000",
                            "size": 8,
                            "declared_size": 8,
                            "real_size": 8,
                            "fallback_size": 8,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        }
                    ],
                    "stats": {"verified": 1},
                },
            ],
            dump_messages=[
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"000\x00",
                ),
            ],
            call_order=call_order,
            enumerated_ranges=[
                {"base": "0x1000", "size": 0x1000, "protection": "r--", "path": None},
                {"base": "0x2000", "size": 0x1000, "protection": "r--", "path": None},
            ],
        )
        session = FakeDexDumpSession(1777, "com.demo.target", enumerate_script, call_order)
        device = FakeDexDumpDevice(session, call_order)

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=False,
                sleep_ms=0,
                fix_header=False,
                dedupe="md5",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=1,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=1,
                include_system=False,
                fast_full_scan=False,
                scan_grace_windows=0,
                debug=False,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=io.StringIO(), stderr=io.StringIO())

            search_calls = [call for call in enumerate_script.call_calls if call[0] == "searchdex"]
            self.assertEqual(len(search_calls), 1)
            self.assertEqual(result["artifact_count"], 1)

    def test_attach_mode_defers_early_export_when_limit_is_filled_by_truncated_candidate(self) -> None:
        call_order = []
        enumerate_script = FakeDexDumpScript(
            search_result=[
                {
                    "results": [
                        {
                            "addr": "0x1000",
                            "size": 8,
                            "declared_size": 64,
                            "real_size": 8,
                            "fallback_size": 64,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": True,
                            "confidence": "high",
                        },
                        {
                            "addr": "0x2000",
                            "size": 32,
                            "declared_size": 32,
                            "real_size": 32,
                            "fallback_size": 32,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        },
                    ],
                    "stats": {"verified": 2},
                },
                {
                    "results": [
                        {
                            "addr": "0x3000",
                            "size": 48,
                            "declared_size": 48,
                            "real_size": 48,
                            "fallback_size": 48,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        }
                    ],
                    "stats": {"verified": 1},
                },
            ],
            dump_messages=[
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"111\x00",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"222\x00",
                ),
            ],
            call_order=call_order,
            enumerated_ranges=[
                {"base": "0x1000", "size": 20 * 1024 * 1024, "protection": "r--", "path": None},
                {"base": "0x2000", "size": 20 * 1024 * 1024, "protection": "r--", "path": None},
            ],
        )
        session = FakeDexDumpSession(1888, "com.demo.target", enumerate_script, call_order)
        device = FakeDexDumpDevice(session, call_order)

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=False,
                sleep_ms=0,
                fix_header=False,
                dedupe="addr",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=2,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=1,
                include_system=False,
                fast_full_scan=False,
                scan_grace_windows=0,
                debug=False,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=io.StringIO(), stderr=io.StringIO())

            search_calls = [call for call in enumerate_script.call_calls if call[0] == "searchdex"]
            self.assertEqual(len(search_calls), 2)
            self.assertEqual(result["artifact_count"], 2)
            self.assertEqual(
                [artifact["address"] for artifact in result["metadata"]["artifacts"]],
                ["0x2000", "0x3000"],
            )

    def test_attach_mode_scans_one_extra_window_before_stopping_at_result_limit(self) -> None:
        call_order = []
        enumerate_script = FakeDexDumpScript(
            search_result=[
                {
                    "results": [
                        {
                            "addr": "0x1000",
                            "size": 500,
                            "declared_size": 500,
                            "real_size": 500,
                            "fallback_size": 500,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        },
                        {
                            "addr": "0x2000",
                            "size": 600,
                            "declared_size": 600,
                            "real_size": 600,
                            "fallback_size": 600,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        },
                    ],
                    "stats": {"verified": 2},
                },
                {
                    "results": [
                        {
                            "addr": "0x3000",
                            "size": 300,
                            "declared_size": 300,
                            "real_size": 300,
                            "fallback_size": 300,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        },
                        {
                            "addr": "0x4000",
                            "size": 400,
                            "declared_size": 400,
                            "real_size": 400,
                            "fallback_size": 400,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        },
                    ],
                    "stats": {"verified": 2},
                },
                {
                    "results": [
                        {
                            "addr": "0x5000",
                            "size": 200,
                            "declared_size": 200,
                            "real_size": 200,
                            "fallback_size": 200,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        }
                    ],
                    "stats": {"verified": 1},
                },
            ],
            dump_messages=[
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"555\x00",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"333\x00",
                ),
            ],
            call_order=call_order,
            enumerated_ranges=[
                {"base": "0x1000", "size": 20 * 1024 * 1024, "protection": "r--", "path": None},
                {"base": "0x2000", "size": 20 * 1024 * 1024, "protection": "r--", "path": None},
                {"base": "0x3000", "size": 20 * 1024 * 1024, "protection": "r--", "path": None},
            ],
        )
        session = FakeDexDumpSession(1999, "com.demo.target", enumerate_script, call_order)
        device = FakeDexDumpDevice(session, call_order)

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=False,
                sleep_ms=0,
                fix_header=False,
                dedupe="addr",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=2,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=1,
                include_system=False,
                fast_full_scan=False,
                debug=False,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=io.StringIO(), stderr=io.StringIO())

            search_calls = [call for call in enumerate_script.call_calls if call[0] == "searchdex"]
            self.assertEqual(len(search_calls), 2)
            self.assertEqual(result["artifact_count"], 2)
            self.assertEqual(
                [artifact["address"] for artifact in result["metadata"]["artifacts"]],
                ["0x3000", "0x4000"],
            )

    def test_attach_mode_stops_early_after_reaching_unique_candidate_limit(self) -> None:
        call_order = []
        enumerate_script = FakeDexDumpScript(
            search_result=[
                {
                    "results": [
                        {
                            "addr": "0x1000",
                            "size": 8,
                            "declared_size": 8,
                            "real_size": 8,
                            "fallback_size": 8,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        },
                        {
                            "addr": "0x1000",
                            "size": 8,
                            "declared_size": 8,
                            "real_size": 8,
                            "fallback_size": 8,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        },
                    ],
                    "stats": {"verified": 2},
                },
                {
                    "results": [
                        {
                            "addr": "0x2000",
                            "size": 8,
                            "declared_size": 8,
                            "real_size": 8,
                            "fallback_size": 8,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        }
                    ],
                    "stats": {"verified": 1},
                },
                {
                    "results": [
                        {
                            "addr": "0x3000",
                            "size": 8,
                            "declared_size": 8,
                            "real_size": 8,
                            "fallback_size": 8,
                            "source": "magic-scan",
                            "deep": False,
                            "header_ok": True,
                            "maps_ok": True,
                            "ids_ok": True,
                            "target_hint_hit": False,
                            "confidence": "high",
                        }
                    ],
                    "stats": {"verified": 1},
                },
            ],
            dump_messages=[
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"000\x00",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"dex\n",
                ),
                FakeMessage(
                    321,
                    json.dumps(
                        {
                            "type": "dexdump-chunk",
                            "token": "dump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"111\x00",
                ),
            ],
            call_order=call_order,
            enumerated_ranges=[
                {"base": "0x1000", "size": 0x1000, "protection": "r--", "path": None},
                {"base": "0x2000", "size": 0x1000, "protection": "r--", "path": None},
                {"base": "0x3000", "size": 0x1000, "protection": "r--", "path": None},
            ],
        )
        session = FakeDexDumpSession(1778, "com.demo.target", enumerate_script, call_order)
        device = FakeDexDumpDevice(session, call_order)

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                output_dir=temp_dir,
                deep=True,
                sleep_ms=0,
                fix_header=False,
                dedupe="addr",
                agent_ready_timeout=10000,
                message_timeout=1000,
                max_results=2,
                min_dex_size=4,
                max_dex_size=1024 * 1024,
                scan_window_ranges=1,
                include_system=False,
                scan_grace_windows=0,
                debug=False,
                json=False,
            )

            result = dexdump.run_dexdump(options, device, stdout=io.StringIO(), stderr=io.StringIO())

            search_calls = [call for call in enumerate_script.call_calls if call[0] == "searchdex"]
            self.assertEqual(len(search_calls), 2)
            self.assertEqual(result["artifact_count"], 2)


if __name__ == "__main__":
    unittest.main()
