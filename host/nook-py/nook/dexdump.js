"use strict";

(function () {
  var rangeListCache = {
    key: null,
    ranges: null
  };
  var procMapsCache = {
    loaded: false,
    entries: []
  };

  function normalizeOptions(options) {
    options = options || {};
    return {
      deep: !!options.deep,
      maxResults: Number(options.max_results || 64),
      startIndex: Number(options.start_index || 0),
      maxRanges: Number(options.max_ranges || 0),
      explicit_ranges: Array.isArray(options.explicit_ranges) ? options.explicit_ranges : null,
      minDexSize: Number(options.min_dex_size || 0x70),
      maxDexSize: Number(options.max_dex_size || (64 * 1024 * 1024)),
      maxRangeSize: Number(options.max_range_size || (256 * 1024 * 1024)),
      includeSystem: !!options.include_system,
      debug: !!options.debug,
      targetPackage: String(options.target_package || ""),
      forceChunkScan: !!options.force_chunk_scan
    };
  }

  function stringToHexPattern(value) {
    var parts = [];
    for (var index = 0; index < value.length; index++) {
      var code = value.charCodeAt(index).toString(16);
      if (code.length < 2) {
        code = "0" + code;
      }
      parts.push(code);
    }
    return parts.join(" ");
  }

  function patternWidth(pattern) {
    if (!pattern) {
      return 0;
    }
    return String(pattern).trim().split(/\s+/).length;
  }

  function parsePattern(pattern) {
    var parts = String(pattern || "").trim().split(/\s+/);
    var tokens = [];
    for (var i = 0; i < parts.length; i++) {
      var part = parts[i];
      if (!part) {
        continue;
      }
      if (part === "?" || part === "??") {
        tokens.push({ wildcard: true, value: 0 });
        continue;
      }
      tokens.push({ wildcard: false, value: parseInt(part, 16) & 0xff });
    }
    return tokens;
  }

  function getTargetPatterns(options) {
    if (!options.targetPackage) {
      return [];
    }
    var dot = options.targetPackage;
    var slash = dot.replace(/\./g, "/");
    var patterns = [stringToHexPattern(dot), stringToHexPattern(slash)];
    if (slash.charAt(0) !== "L") {
      patterns.push(stringToHexPattern("L" + slash));
    }
    return patterns;
  }

  function readRangePath(range) {
    if (!range) {
      return null;
    }
    if (range.file && range.file.path) {
      return String(range.file.path);
    }
    if (range.path) {
      return String(range.path);
    }
    if (range.name) {
      return String(range.name);
    }
    return lookupProcMapsPath(range);
  }

  function bytesToAsciiString(bytes) {
    var parts = [];
    var chunkSize = 0x4000;
    for (var offset = 0; offset < bytes.length; offset += chunkSize) {
      var slice = bytes.subarray(offset, Math.min(bytes.length, offset + chunkSize));
      parts.push(String.fromCharCode.apply(null, slice));
    }
    return parts.join("");
  }

  function loadProcMapsEntries() {
    if (procMapsCache.loaded) {
      return procMapsCache.entries;
    }
    procMapsCache.loaded = true;
    procMapsCache.entries = [];
    try {
      var openPtr = Module.getExportByName(null, "open");
      var readPtr = Module.getExportByName(null, "read");
      var closePtr = Module.getExportByName(null, "close");
      if (!openPtr || !readPtr || !closePtr) {
        return procMapsCache.entries;
      }
      var openFn = new NativeFunction(openPtr, "int", ["pointer", "int"]);
      var readFn = new NativeFunction(readPtr, "int", ["int", "pointer", "int"]);
      var closeFn = new NativeFunction(closePtr, "int", ["int"]);
      var mapsPath = Memory.allocUtf8String("/proc/self/maps");
      var fd = openFn(mapsPath, 0);
      if (fd < 0) {
        return procMapsCache.entries;
      }
      var bufferSize = 32 * 1024;
      var buffer = Memory.alloc(bufferSize);
      var chunks = [];
      while (true) {
        var count = readFn(fd, buffer, bufferSize);
        if (count <= 0) {
          break;
        }
        var data = buffer.readByteArray(count);
        if (!data) {
          break;
        }
        chunks.push(bytesToAsciiString(new Uint8Array(data)));
      }
      closeFn(fd);
      var text = chunks.join("");
      var lines = text.split("\n");
      for (var i = 0; i < lines.length; i++) {
        var line = lines[i];
        if (!line) {
          continue;
        }
        var match = /^([0-9a-f]+)-([0-9a-f]+)\s+([rwxps-]{4})\s+\S+\s+\S+\s+\S+\s*(.*)$/i.exec(line);
        if (!match) {
          continue;
        }
        var path = match[4] ? String(match[4]).trim() : "";
        procMapsCache.entries.push({
          start: parseInt(match[1], 16),
          end: parseInt(match[2], 16),
          protection: match[3],
          path: path || null
        });
      }
    } catch (e) {
    }
    return procMapsCache.entries;
  }

  function lookupProcMapsPath(range) {
    try {
      var entries = loadProcMapsEntries();
      if (!entries.length) {
        return null;
      }
      var start = pointerToNumber(range.base);
      var end = start + Number(range.size || 0);
      for (var i = 0; i < entries.length; i++) {
        var entry = entries[i];
        if (entry.start === start && (!end || entry.end >= end)) {
          return entry.path;
        }
      }
      for (var j = 0; j < entries.length; j++) {
        var candidate = entries[j];
        if (candidate.start <= start && candidate.end > start) {
          return candidate.path;
        }
      }
    } catch (e) {
    }
    return null;
  }

  function describeRange(range) {
    var description = {
      base: range.base.toString(),
      size: Number(range.size),
      protection: String(range.protection || ""),
      path: readRangePath(range)
    };
    if (range.range_base) {
      description.range_base = String(range.range_base);
      description.range_size = Number(range.range_size || 0);
    }
    if (range.scan_base) {
      description.scan_base = String(range.scan_base);
      description.scan_size = Number(range.scan_size || 0);
    }
    return description;
  }

  function splitLargeRangeForScan(range, maxSliceSize) {
    var totalSize = Number(range.size || 0);
    if (totalSize <= 0 || maxSliceSize <= 0 || totalSize <= maxSliceSize) {
      return [range];
    }
    var results = [];
    var offset = 0;
    var effectiveSliceSize = maxSliceSize > 7 ? (maxSliceSize - 7) : maxSliceSize;
    while (offset < totalSize) {
      var currentSize = Math.min(effectiveSliceSize, totalSize - offset);
      var overlap = Math.min(7, Math.max(0, totalSize - (offset + currentSize)));
      results.push({
        base: range.base.add(offset),
        size: currentSize,
        range_base: range.base.toString(),
        range_size: totalSize,
        scan_base: range.base.add(offset),
        scan_size: currentSize + overlap,
        protection: String(range.protection || ""),
        file: range.file || null,
        path: range.path || null,
        name: range.name || null
      });
      offset += currentSize;
    }
    return results;
  }

  function expandRangesForScan(ranges, options) {
    var maxSliceSize = Number(options.maxRangeSize || 0);
    var expanded = [];
    for (var i = 0; i < ranges.length; i++) {
      var parts = splitLargeRangeForScan(ranges[i], maxSliceSize);
      for (var j = 0; j < parts.length; j++) {
        expanded.push(parts[j]);
      }
    }
    return expanded;
  }

  function enumerateScannableRanges(options) {
    var allRanges = expandRangesForScan(enumerateCandidateRanges(options), options);
    var results = [];
    for (var i = 0; i < allRanges.length; i++) {
      if (!shouldScanRange(allRanges[i], options)) {
        continue;
      }
      results.push(describeRange(allRanges[i]));
    }
    return results;
  }

  function pointerToNumber(value) {
    return parseInt(String(value), 16);
  }

  function alignProtectWindow(address, size) {
    var pageSize = Number(Process.pageSize || 4096);
    if (!(pageSize > 0)) {
      pageSize = 4096;
    }
    var start = pointerToNumber(address);
    var end = start + Number(size);
    var alignedStart = Math.floor(start / pageSize) * pageSize;
    var alignedEnd = Math.ceil(end / pageSize) * pageSize;
    return {
      base: ptr("0x" + alignedStart.toString(16)),
      size: Math.max(alignedEnd - alignedStart, pageSize)
    };
  }

  function setReadPermission(base, size) {
    var start = ptr(base);
    var end = start.add(Number(size));
    var changed = false;
    (Process.enumerateRanges("---") || []).forEach(function (range) {
      var rangeEnd = range.base.add(Number(range.size));
      if (range.base.compare(start) < 0 || rangeEnd.compare(end) > 0) {
        return;
      }
      var protection = String(range.protection || "");
      if (protection.indexOf("r") === 0) {
        return;
      }
      if (Memory.protect(range.base, Number(range.size), "r" + protection.substr(1, 2))) {
        changed = true;
      }
    });
    return changed;
  }

  function normalizeExplicitRange(range) {
    return {
      base: ptr(String(range.range_base || range.base)),
      size: Number(range.range_size || range.size),
      scan_base: ptr(String(range.scan_base || range.base)),
      scan_size: Number(range.scan_size || range.size),
      protection: String(range.protection || "r--"),
      file: range.path ? { path: String(range.path) } : null,
      path: range.path ? String(range.path) : null,
      name: range.path ? String(range.path) : null
    };
  }

  function getScanBase(range) {
    return range.scan_base || range.base;
  }

  function getScanSize(range) {
    return Number(range.scan_size || range.size);
  }

  function isPrimaryScanSlice(range) {
    var rangeBase = String((range && (range.range_base || (range.base && range.base.toString && range.base.toString()) || range.base)) || "");
    var scanBase = String((range && (range.scan_base || (range.base && range.base.toString && range.base.toString()) || range.base)) || "");
    return !rangeBase || !scanBase || rangeBase === scanBase;
  }

  function enumerateCandidateRanges(options) {
    var seen = Object.create(null);
    var protections = ["r--"];
    var results = [];
    function appendRange(range, defaultProtection) {
      var protection = String(range.protection || defaultProtection || "");
      var key = range.base.toString() + ":" + String(range.size) + ":" + protection;
      if (seen[key]) {
        return;
      }
      seen[key] = true;
      results.push(range);
    }
    for (var i = 0; i < protections.length; i++) {
      var ranges = Process.enumerateRanges(protections[i]) || [];
      for (var j = 0; j < ranges.length; j++) {
        var range = ranges[j];
        appendRange(range, protections[i]);
      }
    }
    var procEntries = loadProcMapsEntries();
    for (var entryIndex = 0; entryIndex < procEntries.length; entryIndex++) {
      var entry = procEntries[entryIndex];
      if (!entry || !entry.protection || entry.protection.charAt(0) !== "r") {
        continue;
      }
      var entryStart = Number(entry.start || 0);
      var entryEnd = Number(entry.end || 0);
      if (!(entryEnd > entryStart)) {
        continue;
      }
      try {
        appendRange({
          base: ptr("0x" + entryStart.toString(16)),
          size: entryEnd - entryStart,
          protection: String(entry.protection).substr(0, 3),
          file: entry.path ? { path: entry.path } : null,
          path: entry.path || null,
          name: entry.path || null
        }, String(entry.protection || "r--").substr(0, 3));
      } catch (e) {
      }
    }
    return results;
  }

  function buildRangeCacheKey(options) {
    return JSON.stringify({
      minDexSize: Number(options.minDexSize || 0),
      maxRangeSize: Number(options.maxRangeSize || 0),
      includeSystem: !!options.includeSystem
    });
  }

  function getCachedScannableRanges(options) {
    var key = buildRangeCacheKey(options);
    if (rangeListCache.key === key && Array.isArray(rangeListCache.ranges)) {
      return rangeListCache.ranges;
    }
    var ranges = enumerateScannableRanges(options);
    rangeListCache.key = key;
    rangeListCache.ranges = ranges;
    return ranges;
  }

  function listRanges(options) {
    options = normalizeOptions(options);
    var ranges = getCachedScannableRanges(options);
    var startIndex = options.startIndex > 0 ? options.startIndex : 0;
    var endIndex = ranges.length;
    if (options.maxRanges > 0) {
      endIndex = Math.min(ranges.length, startIndex + options.maxRanges);
    }
    return {
      ranges: ranges.slice(startIndex, endIndex),
      window: {
        start_index: startIndex,
        end_index: endIndex,
        done: endIndex >= ranges.length
      },
      total: ranges.length
    };
  }

  function listTargetRanges(options) {
    options = normalizeOptions(options);
    var ranges = getCachedScannableRanges(options).map(normalizeExplicitRange);
    var cache = Object.create(null);
    var hits = [];
    for (var i = 0; i < ranges.length; i++) {
      var range = ranges[i];
      try {
        if (!shouldScanRange(range, options)) {
          continue;
        }
        if (!rangeContainsTargetHint(range, options, cache)) {
          continue;
        }
        hits.push({
          base: range.base.toString(),
          size: Number(range.size),
          scan_base: getScanBase(range).toString(),
          scan_size: getScanSize(range),
          protection: String(range.protection || ""),
          path: readRangePath(range)
        });
      } catch (e) {
      }
    }
    return {
      ranges: hits,
      total: hits.length
    };
  }

  function shouldScanRange(range, options) {
    var size = getScanSize(range);
    if (size < options.minDexSize) {
      return false;
    }
    if (size > options.maxRangeSize) {
      return false;
    }
    return true;
  }

  function shouldSkipMagicScanRange(range, options) {
    if (options.includeSystem) {
      return false;
    }
    var path = readRangePath(range);
    if (!path) {
      return false;
    }
    return path.indexOf("/system/") === 0 ||
      path.indexOf("/data/dalvik-cache/") === 0;
  }

  function safeReadU16(address) {
    try {
      return address.readU16();
    } catch (e) {
      return null;
    }
  }

  function safeReadU32(address) {
    try {
      return address.readU32();
    } catch (e) {
      return null;
    }
  }

  function safeReadCString(address, length) {
    try {
      return address.readCString(length);
    } catch (e) {
      return null;
    }
  }

  function rangeEnd(range) {
    return range.base.add(Number(range.size));
  }

  function isPointerWithinRange(address, range) {
    return address.compare(range.base) >= 0 && address.compare(rangeEnd(range)) < 0;
  }

  function getMapsAddress(base, range) {
    var mapsOffset = safeReadU32(base.add(0x34));
    if (mapsOffset === null || mapsOffset === 0) {
      return null;
    }
    var mapsAddress = base.add(mapsOffset);
    if (!isPointerWithinRange(mapsAddress, range)) {
      return null;
    }
    return mapsAddress;
  }

  function getMapsEnd(mapsAddress, range) {
    if (mapsAddress === null) {
      return null;
    }
    var mapsSize = safeReadU32(mapsAddress);
    if (mapsSize === null || mapsSize < 2 || mapsSize > 50) {
      return null;
    }
    var end = mapsAddress.add(4 + (mapsSize * 0x0c));
    if (end.compare(range.base) < 0 || end.compare(rangeEnd(range)) > 0) {
      return null;
    }
    return end;
  }

  function verifyByMaps(base, mapsAddress) {
    var mapsOffset = safeReadU32(base.add(0x34));
    var mapsSize = safeReadU32(mapsAddress);
    if (mapsOffset === null || mapsSize === null) {
      return false;
    }
    for (var index = 0; index < mapsSize; index++) {
      var itemBase = mapsAddress.add(4 + (index * 0x0c));
      var itemType = safeReadU16(itemBase);
      var itemOffset = safeReadU32(itemBase.add(8));
      if (itemType === 0x1000 && itemOffset === mapsOffset) {
        return true;
      }
    }
    return false;
  }

  function verifyIdsOff(base, dexSize) {
    if (dexSize < 0x70) {
      return false;
    }
    var stringIdsOff = safeReadU32(base.add(0x3c));
    var typeIdsOff = safeReadU32(base.add(0x44));
    var protoIdsOff = safeReadU32(base.add(0x4c));
    var fieldIdsOff = safeReadU32(base.add(0x54));
    var methodIdsOff = safeReadU32(base.add(0x5c));
    var offsets = [stringIdsOff, typeIdsOff, protoIdsOff, fieldIdsOff, methodIdsOff];
    for (var i = 0; i < offsets.length; i++) {
      var value = offsets[i];
      if (value === null || value < 0x70 || value >= dexSize) {
        return false;
      }
    }
    return true;
  }

  function resolveRealDexSize(base, range, declaredSize) {
    var mapsAddress = getMapsAddress(base, range);
    if (mapsAddress !== null) {
      var mapsEnd = getMapsEnd(mapsAddress, range);
      if (mapsEnd !== null) {
        return {
          size: Number(mapsEnd.sub(base)),
          mapsOk: verifyByMaps(base, mapsAddress)
        };
      }
    }
    return {
      size: declaredSize,
      mapsOk: false
    };
  }

  function verifyMagicCandidate(base, range) {
    if (base.add(0x70).compare(rangeEnd(range)) > 0) {
      return false;
    }
    return safeReadU32(base.add(0x3c)) === 0x70;
  }

  function verifyMappedCandidate(base, range) {
    if (base.add(0x70).compare(rangeEnd(range)) > 0) {
      return false;
    }
    var mapsAddress = getMapsAddress(base, range);
    if (mapsAddress === null) {
      return false;
    }
    var mapsEnd = getMapsEnd(mapsAddress, range);
    if (mapsEnd === null) {
      return false;
    }
    return verifyByMaps(base, mapsAddress);
  }

  function confidenceForCandidate(source, mapsOk, idsOk) {
    if (mapsOk && idsOk) {
      return "high";
    }
    if (source === "magic-scan" && (mapsOk || idsOk)) {
      return "high";
    }
    if (mapsOk || idsOk) {
      return "medium";
    }
    return "low";
  }

  function buildCandidate(base, range, source, deepMode, options, targetHintHit) {
    var offsetInRange = Number(base.sub(range.base));
    var totalRangeSize = Number(range.size);
    if (offsetInRange < 0 || offsetInRange >= totalRangeSize) {
      return null;
    }

    var declaredSize = safeReadU32(base.add(0x20));
    var fallbackSize = totalRangeSize - offsetInRange;
    var resolved = resolveRealDexSize(base, range, declaredSize);
    var realSize = Number(resolved.size || 0);
    var mapsOk = verifyMappedCandidate(base, range);
    var magicOk = verifyMagicCandidate(base, range);
    if (!magicOk && !mapsOk) {
      return null;
    }

    if (realSize < options.minDexSize || realSize > options.maxDexSize) {
      if (declaredSize !== null && declaredSize >= options.minDexSize && declaredSize <= options.maxDexSize) {
        realSize = declaredSize;
      } else if (fallbackSize >= options.minDexSize && fallbackSize <= options.maxDexSize) {
        realSize = fallbackSize;
      } else {
        return null;
      }
    }

    var idsOk = verifyIdsOff(base, realSize);
    var magic = safeReadCString(base, 4);
    var hasMagic = magic === "dex\n";

    if (source === "deep-scan") {
      if (!mapsOk || !idsOk) {
        return null;
      }
    } else if (source === "range-base") {
      if (!mapsOk) {
        return null;
      }
    } else if (!magicOk) {
      return null;
    }

    return {
      addr: base.toString(),
      size: realSize,
      declared_size: declaredSize === null ? 0 : declaredSize,
      real_size: realSize,
      fallback_size: fallbackSize,
      source: source,
      deep: !!deepMode,
      header_ok: magicOk,
      maps_ok: mapsOk,
      ids_ok: idsOk,
      confidence: confidenceForCandidate(source, mapsOk, idsOk),
      target_hint_hit: !!targetHintHit,
      range_base: range.base.toString(),
      range_size: Number(range.size),
      range_protection: String(range.protection || ""),
      range_path: readRangePath(range)
    };
  }

  function rangeContainsTargetHint(range, options, cache) {
    if (!options.targetPackage) {
      return false;
    }
    var key = getScanBase(range).toString() + ":" + String(getScanSize(range));
    if (cache[key] !== undefined) {
      return cache[key];
    }
    var patterns = getTargetPatterns(options);
    for (var i = 0; i < patterns.length; i++) {
      if ((scanRangePatternForRange(range, patterns[i], options) || []).length > 0) {
        cache[key] = true;
        return true;
      }
    }
    cache[key] = false;
    return false;
  }

  function shouldForceChunkScanForRange(range, options) {
    if (options.forceChunkScan) {
      return true;
    }
    return !readRangePath(range) &&
      !range.range_base &&
      Number(getScanSize(range)) > 0 &&
      Number(getScanSize(range)) <= (16 * 1024 * 1024);
  }

  function scanRangePatternForRange(range, pattern, options) {
    if (shouldForceChunkScanForRange(range, options)) {
      return scanRangePattern(
        getScanBase(range),
        getScanSize(range),
        pattern,
        Object.assign({}, options, {
          forceChunkScan: true
        })
      );
    }
    return scanRangePattern(getScanBase(range), getScanSize(range), pattern, options);
  }

  function scanRangePattern(base, size, pattern, options) {
    var totalSize = Number(size);
    if (totalSize <= 0) {
      return [];
    }

    if (!options.forceChunkScan && Memory && typeof Memory.scanSync === "function") {
      try {
        return Memory.scanSync(base, totalSize, pattern) || [];
      } catch (e) {
        try {
          if (setReadPermission(base, totalSize)) {
            return Memory.scanSync(base, totalSize, pattern) || [];
          }
        } catch (innerError) {
        }
      }
    }

    var chunkSize = 256 * 1024;
    var minChunkSize = 4 * 1024;
    var tokens = parsePattern(pattern);
    var overlap = Math.max(tokens.length - 1, 0);
    var results = [];
    var seen = Object.create(null);
    var offset = 0;

    while (offset < totalSize) {
      var currentChunkSize = Math.min(chunkSize, totalSize - offset);
      var scanned = false;

      while (!scanned) {
        var length = Math.min(currentChunkSize, totalSize - offset);
        var chunkBase = base.add(offset);
        try {
          var buffer = chunkBase.readByteArray(length);
          appendPatternMatches(chunkBase, new Uint8Array(buffer), tokens, results, seen);
          offset += Math.max(length - overlap, 1);
          scanned = true;
        } catch (e) {
          try {
            scanChunkPatternByReadU8(chunkBase, length, tokens, results, seen);
            offset += Math.max(length - overlap, 1);
            scanned = true;
            continue;
          } catch (readU8Error) {
          }
          try {
            if (setReadPermission(chunkBase, length)) {
              continue;
            }
          } catch (innerError) {
          }
          if (currentChunkSize <= minChunkSize) {
            offset += Math.max(length - overlap, 1);
            scanned = true;
          } else {
            currentChunkSize = Math.max(minChunkSize, Math.floor(currentChunkSize / 2));
          }
        }
      }
    }

    return results;
  }

  function appendPatternMatches(chunkBase, bytes, tokens, results, seen) {
    for (var i = 0; i + tokens.length <= bytes.length; i++) {
      var matched = true;
      for (var j = 0; j < tokens.length; j++) {
        var token = tokens[j];
        if (!token.wildcard && bytes[i + j] !== token.value) {
          matched = false;
          break;
        }
      }
      if (!matched) {
        continue;
      }
      var address = chunkBase.add(i);
      var key = address.toString();
      if (seen[key]) {
        continue;
      }
      seen[key] = true;
      results.push({
        address: address,
        size: tokens.length
      });
    }
  }

  function scanChunkPatternByReadU8(chunkBase, length, tokens, results, seen) {
    var bytes = new Uint8Array(length);
    for (var index = 0; index < length; index++) {
      bytes[index] = chunkBase.add(index).readU8();
    }
    appendPatternMatches(chunkBase, bytes, tokens, results, seen);
  }

  function readArrayBufferByU8(base, length) {
    var bytes = new Uint8Array(length);
    for (var index = 0; index < length; index++) {
      bytes[index] = base.add(index).readU8();
    }
    return bytes.buffer;
  }

  function scanMagicCandidates(range, options, results, seen, targetHintCache) {
    if (shouldSkipMagicScanRange(range, options)) {
      return {
        matches: 0,
        added: 0
      };
    }
    var matches = [];
    try {
      matches = scanRangePatternForRange(range, "64 65 78 0a 30 ?? ?? 00", options);
    } catch (e) {
      // Some anonymous / rewritten mappings throw from Memory.scanSync even though
      // Frida-style chunk scanning can still recover magic hits from the same bytes.
      matches = scanRangePattern(
        getScanBase(range),
        getScanSize(range),
        "64 65 78 0a 30 ?? ?? 00",
        Object.assign({}, options, {
          forceChunkScan: true
        })
      );
    }
    var targetHintHit = matches.length > 0 ? rangeContainsTargetHint(range, options, targetHintCache) : false;
    var added = 0;
    for (var i = 0; i < matches.length; i++) {
      var candidate = buildCandidate(matches[i].address, range, "magic-scan", false, options, targetHintHit);
      if (candidate === null || seen[candidate.addr]) {
        continue;
      }
      seen[candidate.addr] = true;
      results.push(candidate);
      added++;
    }
    return {
      matches: matches.length,
      added: added
    };
  }

  function scanDeepCandidates(range, options, results, seen, targetHintCache) {
    var matches = scanRangePatternForRange(range, "70 00 00 00", options);
    var targetHintHit = matches.length > 0 ? rangeContainsTargetHint(range, options, targetHintCache) : false;
    for (var i = 0; i < matches.length; i++) {
      var base = matches[i].address.sub(0x3c);
      if (base.compare(range.base) < 0) {
        continue;
      }
      var magic = safeReadCString(base, 4);
      if (magic === "dex\n") {
        continue;
      }
      var candidate = buildCandidate(base, range, "deep-scan", true, options, targetHintHit);
      if (candidate === null || seen[candidate.addr]) {
        continue;
      }
      seen[candidate.addr] = true;
      results.push(candidate);
    }
  }

  function scanRangeBaseCandidate(range, options, results, seen, targetHintCache) {
    if (!isPrimaryScanSlice(range)) {
      return;
    }
    var magic = safeReadCString(range.base, 4);
    if (magic === "dex\n") {
      return;
    }
    var candidate = buildCandidate(
      range.base,
      range,
      "range-base",
      true,
      options,
      rangeContainsTargetHint(range, options, targetHintCache)
    );
    if (candidate === null || seen[candidate.addr]) {
      return;
    }
    seen[candidate.addr] = true;
    results.push(candidate);
  }

  function confidenceRank(value) {
    if (value === "high") {
      return 0;
    }
    if (value === "medium") {
      return 1;
    }
    if (value === "low") {
      return 2;
    }
    return 3;
  }

  function rankCandidates(results, options) {
    return results;
  }

  function scanCompatPattern(range, pattern, options) {
    var matches = [];
    try {
      matches = Memory.scanSync(getScanBase(range), getScanSize(range), pattern) || [];
    } catch (e) {
      matches = [];
    }
    return matches;
  }

  function searchDexCompat(options) {
    var results = [];
    var seen = Object.create(null);
    var stats = {
      ranges_total: 0,
      ranges_scanned: 0,
      ranges_skipped: 0,
      magic_hits: 0,
      magic_matches: 0,
      deep_hits: 0,
      verified: 0,
      errors: 0,
      candidates_total: 0,
      error_samples: []
    };
    var ranges = expandRangesForScan(enumerateCandidateRanges(options), options);
    stats.ranges_total = ranges.length;

    for (var i = 0; i < ranges.length; i++) {
      var range = ranges[i];
      try {
        if (!shouldScanRange(range, options)) {
          stats.ranges_skipped++;
          continue;
        }

        stats.ranges_scanned++;
        var magicMatches = scanCompatPattern(range, "64 65 78 0a 30 ?? ?? 00", options);
        for (var matchIndex = 0; matchIndex < magicMatches.length; matchIndex++) {
          var match = magicMatches[matchIndex];
          var rangePath = readRangePath(range);
          if (rangePath && (
            rangePath.indexOf("/data/dalvik-cache/") === 0 ||
            rangePath.indexOf("/system/") === 0
          )) {
            continue;
          }
          stats.magic_matches++;
          var magicCandidate = buildCandidate(match.address, range, "magic-scan", false, options, false);
          if (magicCandidate === null || seen[magicCandidate.addr]) {
            continue;
          }
          seen[magicCandidate.addr] = true;
          results.push(magicCandidate);
          stats.magic_hits++;
        }

        if (options.deep) {
          var before = results.length;
          var deepMatches = scanCompatPattern(range, "70 00 00 00", options);
          for (var deepIndex = 0; deepIndex < deepMatches.length; deepIndex++) {
            var deepBase = deepMatches[deepIndex].address.sub(0x3c);
            if (deepBase.compare(range.base) < 0) {
              continue;
            }
            if (deepBase.readCString(4) === "dex\n") {
              continue;
            }
            var deepCandidate = buildCandidate(deepBase, range, "deep-scan", true, options, false);
            if (deepCandidate === null || seen[deepCandidate.addr]) {
              continue;
            }
            seen[deepCandidate.addr] = true;
            results.push(deepCandidate);
          }
          stats.deep_hits += results.length - before;
        } else {
          var beforeRangeBase = results.length;
          if (range.base.readCString(4) !== "dex\n") {
            var rangeBaseCandidate = buildCandidate(range.base, range, "range-base", true, options, false);
            if (rangeBaseCandidate !== null && !seen[rangeBaseCandidate.addr]) {
              seen[rangeBaseCandidate.addr] = true;
              results.push(rangeBaseCandidate);
            }
          }
          stats.deep_hits += results.length - beforeRangeBase;
        }
      } catch (e) {
        stats.errors++;
        if (options.debug && stats.error_samples.length < 8) {
          stats.error_samples.push({
            range: {
              base: range.base.toString(),
              size: Number(range.size)
            },
            error: String((e && e.stack) || (e && e.message) || e)
          });
        }
      }
    }

    stats.candidates_total = results.length;
    results = rankCandidates(results, options);
    stats.verified = results.length;
    return {
      results: results,
      stats: stats,
      window: {
        start_index: 0,
        end_index: ranges.length,
        scanned_range_count: ranges.length,
        done: true
      }
    };
  }

  function searchDex(options) {
    options = normalizeOptions(options);
    if (!Array.isArray(options.explicit_ranges) || options.explicit_ranges.length === 0) {
      return searchDexCompat(options);
    }
    var results = [];
    var seen = Object.create(null);
    var targetHintCache = Object.create(null);
    var stats = {
      ranges_total: 0,
      ranges_scanned: 0,
      ranges_skipped: 0,
      magic_hits: 0,
      magic_matches: 0,
      deep_hits: 0,
      verified: 0,
      errors: 0,
      candidates_total: 0,
      error_samples: []
    };

    var ranges;
    if (Array.isArray(options.explicit_ranges) && options.explicit_ranges.length > 0) {
      ranges = options.explicit_ranges.map(normalizeExplicitRange);
    } else {
      ranges = enumerateCandidateRanges(options);
    }
    stats.ranges_total = ranges.length;
    var startIndex = options.startIndex > 0 ? options.startIndex : 0;
    var endIndex = ranges.length;
    if (options.maxRanges > 0) {
      endIndex = Math.min(ranges.length, startIndex + options.maxRanges);
    }

    for (var i = startIndex; i < endIndex; i++) {
      var range = ranges[i];
      try {
        if (!shouldScanRange(range, options)) {
          stats.ranges_skipped++;
          continue;
        }

        stats.ranges_scanned++;
        var before;
        var magicScan = scanMagicCandidates(range, options, results, seen, targetHintCache);
        stats.magic_matches += Number((magicScan && magicScan.matches) || 0);
        stats.magic_hits += Number((magicScan && magicScan.added) || 0);

        if (options.deep) {
          before = results.length;
          scanDeepCandidates(range, options, results, seen, targetHintCache);
          stats.deep_hits += results.length - before;
        } else {
          before = results.length;
          scanRangeBaseCandidate(range, options, results, seen, targetHintCache);
          stats.deep_hits += results.length - before;
        }
      } catch (e) {
        stats.errors++;
        if (options.debug && stats.error_samples.length < 8) {
          stats.error_samples.push({
            range: {
              base: range.base.toString(),
              size: Number(range.size),
              scan_base: getScanBase(range).toString(),
              scan_size: getScanSize(range)
            },
            error: String((e && e.stack) || (e && e.message) || e)
          });
        }
      }
    }

    stats.candidates_total = results.length;
    results = rankCandidates(results, options);
    stats.verified = results.length;

    return {
      results: results,
      stats: stats,
      window: {
        start_index: startIndex,
        end_index: endIndex,
        scanned_range_count: endIndex > startIndex ? (endIndex - startIndex) : 0,
        done: endIndex >= ranges.length
      }
    };
  }

  var dumpSequence = 1;

  function beginMemoryDump(address, size, options) {
    options = options || {};
    var base = typeof address === "string" ? ptr(address) : address;
    var totalSize = Number(size);
    var chunkSize = Number(options.chunk_size || (32 * 1024));
    var tryProtect = !!options.try_protect;
    if (totalSize <= 0) {
      throw new Error("invalid dump size");
    }
    if (chunkSize <= 0) {
      chunkSize = 65536;
    }

    var token = "dump-" + String(dumpSequence++);
    var chunks = Math.ceil(totalSize / chunkSize);

    for (var index = 0; index < chunks; index++) {
      var offset = index * chunkSize;
      var length = Math.min(chunkSize, totalSize - offset);
      var current = base.add(offset);
      var data;
      try {
        data = current.readByteArray(length);
      } catch (e) {
        try {
          data = readArrayBufferByU8(current, length);
        } catch (readU8Error) {
        var alignedWindow = alignProtectWindow(current, length);
        var repaired = false;
        if (tryProtect) {
          try {
            repaired = setReadPermission(current, length);
          } catch (innerError) {
          }
        }
        if (!repaired && (!tryProtect || !Memory.protect(alignedWindow.base, alignedWindow.size, "r--"))) {
          send({
            type: "dexdump-error",
            token: token,
            error: String(e)
          });
          throw e;
        }
          try {
            data = current.readByteArray(length);
          } catch (retryError) {
            try {
              data = readArrayBufferByU8(current, length);
            } catch (finalReadError) {
              send({
                type: "dexdump-error",
                token: token,
                error: String(finalReadError)
              });
              throw finalReadError;
            }
          }
        }
      }

      send({
        type: "dexdump-chunk",
        token: token,
        index: index,
        chunks: chunks,
        size: length,
        eof: index === (chunks - 1)
      }, data);
    }

    return {
      token: token,
      size: totalSize,
      chunk_size: chunkSize,
      chunks: chunks
    };
  }

  rpc.exports = {
    enumerateranges: listRanges,
    listtargetranges: listTargetRanges,
    searchdex: searchDex,
    beginmemorydump: beginMemoryDump
  };
})();
