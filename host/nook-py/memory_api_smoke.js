(() => {
  send({
    type: 'send',
    payload: `features:${typeof Memory}:${typeof ptr}:${typeof NativeFunction}:${typeof NativeCallback}:${typeof ModuleMap}:${typeof Module.load}:${typeof Module.ensureInitialized}:${typeof Module.enumerateModules}:${typeof Module.enumerateImports}:${typeof Module.findImportByName}:${typeof Module.getImportByName}:${typeof Module.enumerateExports}:${typeof Module.enumerateSymbols}:${typeof Module.findSymbolByName}:${typeof Module.getSymbolByName}:${typeof Module.findGlobalExportByName}:${typeof Module.getGlobalExportByName}:${typeof Module.findExportByName}:${typeof Module.getExportByName}:${typeof Module.findBaseAddress}:${typeof Module.getBaseAddress}:${typeof Module.findRangeByAddress}:${typeof Memory.protect}:${typeof Memory.patchCode}:${typeof Memory.scanSync}:${typeof Memory.scan}:${typeof Process.enumerateRanges}:${typeof Process.findRangeByAddress}:${typeof Process.getModuleByAddress}:${typeof Process.enumerateModules}:${typeof Process.findModuleByName}:${typeof Process.getModuleByName}:${typeof Process.mainModule}:${typeof Memory.allocAnsiString}:${typeof Memory.allocUtf16String}:${typeof ptr('0').readAnsiString}:${typeof ptr('0').readUtf16String}:${typeof ptr('0').writeAnsiString}:${typeof ptr('0').writeUtf16String}:${typeof ptr('0').readFloat}:${typeof ptr('0').readDouble}:${typeof ptr('0').writeFloat}:${typeof ptr('0').writeDouble}:${typeof Process.pointerSize}:${typeof Process.pageSize}:${typeof Process.arch}:${typeof Process.platform}:${typeof Process.id}:${typeof Process.isDebuggerAttached}:${typeof Process.getCurrentThreadId}:${typeof Process.enumerateThreads}:${typeof Thread.id}:${typeof Thread.sleep}:${typeof DebugSymbol}:${typeof DebugSymbol.fromAddress}`
  });

  send({
    type: 'send',
    payload: `process-meta:${Process.pointerSize}:${Process.pageSize > 0}:${Process.arch}:${Process.platform}`
  });

  send({
    type: 'send',
    payload: `process-identity:${Process.id > 0}:${typeof Process.isDebuggerAttached()}:${Process.isDebuggerAttached()}`
  });

  const currentThreadId = Process.getCurrentThreadId();
  const threads = Process.enumerateThreads();
  const currentThread = threads.find(function (thread) {
    return thread.id === currentThreadId;
  });
  send({
    type: 'send',
    payload: `process-threads:${currentThreadId > 0}:${threads.length > 0}:${currentThread === undefined ? 'missing' : currentThread.state}:${currentThread === undefined ? 'missing' : typeof currentThread.name}:${Thread.id > 0}:${Thread.id === currentThreadId}`
  });

  const sleepStartedAt = Date.now();
  Thread.sleep(0.02);
  send({
    type: 'send',
    payload: `thread-sleep:${typeof Thread.sleep}:${Date.now() - sleepStartedAt >= 10}`
  });

  const processModules = Process.enumerateModules();
  const processMainModule = Process.mainModule;
  const foundMainModule = processMainModule === null ? null : Process.findModuleByName(processMainModule.name);
  const gotMainModule = processMainModule === null ? null : Process.getModuleByName(processMainModule.name);
  send({
    type: 'send',
    payload: `process-modules:${processModules.length > 0}:${processMainModule === null ? 'null' : processMainModule.name}:${foundMainModule === null ? 'null' : foundMainModule.name}:${gotMainModule === null ? 'null' : gotMainModule.name}`
  });

  if (typeof Memory === 'undefined') {
    throw new Error('Memory is not defined');
  }

  const block = Memory.alloc(24);
  const text = Memory.allocUtf8String('hello-memory');

  block.writeU8(18);
  block.add(2).writeU16(13398);
  block.add(8).writeU32(2023406814);
  block.add(16).writePointer(text);

  send({
    type: 'send',
    payload: [
      typeof Memory.alloc,
      String(block.readU8()),
      String(block.add(2).readU16()),
      String(block.add(8).readU32()),
      block.add(16).readPointer().readUtf8String(),
      Memory.allocUtf8String('ok').readUtf8String()
    ].join(':')
  });

  const wide = Memory.alloc(16);
  wide.writeU64(305419896);
  send({
    type: 'send',
    payload: `u64:${wide.readU64()}`
  });

  const floatBlock = Memory.alloc(16);
  const floatWriteResult = floatBlock.writeFloat(1.25);
  const doubleWriteResult = floatBlock.add(8).writeDouble(2.5);
  send({
    type: 'send',
    payload: `float-scalars:${floatBlock.readFloat().toFixed(2)}:${floatBlock.add(8).readDouble().toFixed(2)}:${floatWriteResult.equals(floatBlock)}:${doubleWriteResult.equals(floatBlock.add(8))}`
  });

  const copySrc = Memory.allocUtf8String('hello-copy');
  const copyDst = Memory.alloc(32);
  Memory.copy(copyDst, copySrc, 11);
  send({
    type: 'send',
    payload: `copy:${copyDst.readUtf8String()}`
  });

  send({
    type: 'send',
    payload: `cstring:${copySrc.readCString(5)}:${copySrc.readUtf8String(5)}`
  });

  copyDst.writeUtf8String('hello-write');
  send({
    type: 'send',
    payload: `write-utf8:${copyDst.readCString()}:${copyDst.readUtf8String(5)}`
  });

  const ansiPtr = Memory.allocAnsiString('ansi-memory');
  send({
    type: 'send',
    payload: `ansi-read:${ansiPtr.readAnsiString()}:${ansiPtr.readAnsiString(4)}`
  });

  const ansiDst = Memory.alloc(16);
  ansiDst.writeAnsiString('ansi-write');
  send({
    type: 'send',
    payload: `ansi-write:${ansiDst.readAnsiString()}:${ansiDst.readUtf8String(4)}`
  });

  const utf16Text = String.fromCharCode(0x41, 0x4f60, 0x597d);
  const utf16Ptr = Memory.allocUtf16String(utf16Text);
  const utf16Read = utf16Ptr.readUtf16String();
  send({
    type: 'send',
    payload: `alloc-utf16:${utf16Ptr.readU16()}:${utf16Ptr.add(2).readU16()}:${utf16Ptr.add(4).readU16()}:${utf16Ptr.add(6).readU16()}:${utf16Read.charCodeAt(0)}:${utf16Read.charCodeAt(1)}:${utf16Read.charCodeAt(2)}`
  });

  const utf16Dst = Memory.alloc(16);
  utf16Dst.writeUtf16String(utf16Text);
  const utf16Short = utf16Dst.readUtf16String(1);
  send({
    type: 'send',
    payload: `write-utf16:${utf16Dst.readU16()}:${utf16Dst.add(2).readU16()}:${utf16Dst.add(4).readU16()}:${utf16Dst.add(6).readU16()}:${utf16Short.charCodeAt(0)}`
  });

  send({
    type: 'send',
    payload: `equals:${copySrc.equals(copySrc)}:${copySrc.equals(copyDst)}:${copySrc.equals(String(copySrc))}:${copySrc.equals(ptr(String(copySrc)))}`
  });

  send({
    type: 'send',
    payload: `compare:${copySrc.compare(copySrc)}:${copySrc.compare(copySrc.add(1))}:${copySrc.compare(String(copySrc))}:${copySrc.add(1).compare(ptr(String(copySrc)))}`
  });

  const bitPtr = ptr('0x12f0');
  send({
    type: 'send',
    payload: `bitops:${bitPtr.and('0xff')}:${bitPtr.or(0x0f)}:${bitPtr.xor(ptr('0xff'))}`
  });

  const patchTarget = Memory.alloc(8);
  patchTarget.writeUtf8String('ABCD');
  Memory.patchCode(patchTarget, 4, function(code, size) {
    code.writeByteArray([87, 88, 89, 90]);
    send({
      type: 'send',
      payload: `patch-code:${code.equals(patchTarget)}:${size}:${code.readUtf8String(4)}:${patchTarget.readUtf8String(4)}`
    });
  });
  send({
    type: 'send',
    payload: `patch-code-after:${patchTarget.readUtf8String(4)}`
  });

  send({
    type: 'send',
    payload: `protect-rw:${Memory.protect(copyDst, 1, 'rw-')}`
  });

  const scanText = Memory.allocUtf8String('hello hello');
  const scanMatches = Memory.scanSync(scanText, 11, '68 65 6c 6c 6f');
  send({
    type: 'send',
    payload: `scan:${scanMatches.length}:${scanMatches[0].address.sub(scanText)}:${scanMatches[1].address.sub(scanText)}:${scanMatches[0].size}`
  });

  const scanEvents = [];
  Memory.scan(scanText, 11, '68 65 6c 6c 6f', {
    onMatch(address, size) {
      scanEvents.push(`match:${address.sub(scanText)}:${size}`);
    },
    onComplete() {
      scanEvents.push('complete');
      send({
        type: 'send',
        payload: `scan-callback:${scanEvents.join('|')}`
      });
    }
  });

  const rwRanges = Process.enumerateRanges('rw-');
  send({
    type: 'send',
    payload: `ranges-rw:${rwRanges.length}`
  });

  const foundRange = Process.findRangeByAddress(copyDst);
  send({
    type: 'send',
    payload: `find-range:${foundRange === null ? 'null' : foundRange.protection}`
  });

  const moduleFoundRange = Module.findRangeByAddress(copyDst);
  send({
    type: 'send',
    payload: `module-find-range:${moduleFoundRange === null ? 'null' : moduleFoundRange.protection}`
  });

  const agentBase = Module.findBaseAddress('libnook-agent.so');
  const strictAgentBase = Module.getBaseAddress('libnook-agent.so');
  const agentBaseRange = Module.findRangeByAddress(strictAgentBase);
  send({
    type: 'send',
    payload: `module-base:${agentBase === null ? 'null' : String(agentBase)}:${String(strictAgentBase)}:${agentBaseRange === null ? 'null' : agentBaseRange.protection}`
  });

  const modules = Module.enumerateModules();
  let hasAgentModule = false;
  for (let i = 0; i < modules.length; i++) {
    if (modules[i].name === 'libnook-agent.so') {
      hasAgentModule = true;
      break;
    }
  }
  send({
    type: 'send',
    payload: `modules:${modules.length}:${hasAgentModule}`
  });

  const moduleByBase = Process.getModuleByAddress(strictAgentBase);
  send({
    type: 'send',
    payload: `module-by-address:${moduleByBase === null ? 'null' : moduleByBase.name}:${moduleByBase !== null && moduleByBase.path.indexOf('libnook-agent.so') >= 0}`
  });

  const moduleMap = new ModuleMap();
  const mapFound = moduleMap.find(strictAgentBase);
  const mapGot = moduleMap.get(strictAgentBase);
  send({
    type: 'send',
    payload: `module-map:${moduleMap.has(strictAgentBase)}:${mapFound === null ? 'null' : mapFound.name}:${mapGot.name}:${moduleMap.values().length > 0}`
  });

  const updatedMap = moduleMap.update();
  const updatedFound = updatedMap.find(strictAgentBase);
  send({
    type: 'send',
    payload: `module-map-update:${updatedMap === moduleMap}:${updatedFound === null ? 'null' : updatedFound.name}:${updatedMap.values().length > 0}`
  });

  const loadedModule = Module.load('liblog.so');
  const loadedModuleFound = Process.findModuleByName(loadedModule.name);
  send({
    type: 'send',
    payload: `module-load:${loadedModule.name.length > 0}:${loadedModule.base.isNull()}:${loadedModule.size > 0}:${loadedModuleFound !== null && String(loadedModuleFound.base) === String(loadedModule.base)}`
  });

  const ensureInitializedResult = Module.ensureInitialized(loadedModule.name);
  send({
    type: 'send',
    payload: `module-ensure-init:${ensureInitializedResult === undefined}:${Process.findModuleByName(loadedModule.name) !== null}`
  });

  const inlineHookExport = Module.findExportByName('libnook-agent.so', 'NookInlineHookAddress');
  const strictInlineHookExport = Module.getExportByName('libnook-agent.so', 'NookInlineHookAddress');
  send({
    type: 'send',
    payload: `module-export:${inlineHookExport === null ? 'null' : String(inlineHookExport)}:${String(strictInlineHookExport)}:${String(inlineHookExport !== null && String(inlineHookExport) === String(strictInlineHookExport))}`
  });

  const globalInlineHookExport = Module.findGlobalExportByName('NookInlineHookAddress');
  const strictGlobalInlineHookExport = Module.getGlobalExportByName('NookInlineHookAddress');
  send({
    type: 'send',
    payload: `module-global-export:${globalInlineHookExport === null ? 'null' : String(globalInlineHookExport.isNull())}:${String(strictGlobalInlineHookExport.isNull())}:${String(globalInlineHookExport !== null && globalInlineHookExport.equals(strictGlobalInlineHookExport))}`
  });

  const agentExports = Module.enumerateExports('libnook-agent.so');
  let smokeExport = null;
  for (let i = 0; i < agentExports.length; i++) {
    if (agentExports[i].name === 'NookNativeFunctionSmokeAdd') {
      smokeExport = agentExports[i];
      break;
    }
  }
  send({
    type: 'send',
    payload: `module-exports:${agentExports.length > 0}:${smokeExport === null ? 'null' : smokeExport.type}:${smokeExport === null ? 'null' : smokeExport.name}:${smokeExport === null ? 'null' : String(smokeExport.address.isNull())}`
  });

  const symbolFind = Module.findSymbolByName('libnook-agent.so', 'NookNativeFunctionSmokeAdd');
  const symbolGet = Module.getSymbolByName('libnook-agent.so', 'NookNativeFunctionSmokeAdd');
  send({
    type: 'send',
    payload: `module-symbols:${symbolFind === null ? 'null' : String(symbolFind.isNull())}:${String(symbolGet.isNull())}:${String(symbolFind !== null && symbolFind.equals(symbolGet))}`
  });

  const debugSymbol = DebugSymbol.fromAddress(symbolGet);
  send({
    type: 'send',
    payload: `debug-symbol:${debugSymbol.address.equals(symbolGet)}:${debugSymbol.name === null ? 'null' : debugSymbol.name}:${debugSymbol.moduleName === null ? 'null' : debugSymbol.moduleName}:${debugSymbol.toString().indexOf('NookNativeFunctionSmokeAdd') >= 0}`
  });

  const symbolEntries = Module.enumerateSymbols('libnook-agent.so');
  let smokeSymbol = null;
  for (let i = 0; i < symbolEntries.length; i++) {
    if (symbolEntries[i].name === 'NookNativeFunctionSmokeAdd') {
      smokeSymbol = symbolEntries[i];
      break;
    }
  }
  send({
    type: 'send',
    payload: `module-symbol-enum:${symbolEntries.length > 0}:${smokeSymbol === null ? 'null' : smokeSymbol.name}:${smokeSymbol === null ? 'null' : String(smokeSymbol.address.isNull())}`
  });

  const importEntries = Module.enumerateImports('libnook-agent.so');
  let namedImport = null;
  for (let i = 0; i < importEntries.length; i++) {
    if (typeof importEntries[i].name === 'string' && importEntries[i].name.length > 0) {
      namedImport = importEntries[i];
      break;
    }
  }
  send({
    type: 'send',
    payload: `module-imports:${importEntries.length > 0}:${namedImport === null ? 'null' : namedImport.type}:${namedImport === null ? 'null' : typeof namedImport.module}:${namedImport === null ? 'null' : String(namedImport.slot.isNull())}:${namedImport === null ? 'null' : String(namedImport.address.isNull())}`
  });

  const importFind = namedImport === null ? null : Module.findImportByName('libnook-agent.so', namedImport.name);
  const importGet = namedImport === null ? null : Module.getImportByName('libnook-agent.so', namedImport.name);
  send({
    type: 'send',
    payload: `module-import-find:${namedImport === null ? 'null' : String(importFind === null)}:${namedImport === null ? 'null' : String(importGet.isNull())}:${namedImport === null ? 'null' : String(importFind !== null && importFind.equals(importGet))}`
  });

  const nativeFunctionPtr = Module.getExportByName('libnook-agent.so', 'NookNativeFunctionSmokeAdd');
  const nativeFunctionAdd = new NativeFunction(nativeFunctionPtr, 'uint32', ['uint32', 'uint32']);
  send({
    type: 'send',
    payload: `native-function:${nativeFunctionAdd(7, 35)}:${String(nativeFunctionPtr)}`
  });

  const nativeCallback = new NativeCallback(function(left, right) {
    return left + right;
  }, 'uint32', ['uint32', 'uint32']);
  const nativeCallbackInvoke = new NativeFunction(nativeCallback, 'uint32', ['uint32', 'uint32']);
  send({
    type: 'send',
    payload: `native-callback:${nativeCallbackInvoke(7, 35)}:${String(nativeCallback)}`
  });

  const extExportNames = [
    'NookNativeFunctionSmokeBoolNot',
    'NookNativeFunctionSmokeAddS16',
    'NookNativeFunctionSmokeAddU64',
    'NookNativeFunctionSmokeAddFloat',
    'NookNativeFunctionSmokeAddDouble',
    'NookNativeFunctionSmokeMixU64Double',
    'NookNativeFunctionSmokeMixFloatU32',
    'NookNativeFunctionSmokeMixDoubleU32'
  ];
  const extExports = {};
  const missingExtExports = [];
  for (let i = 0; i < extExportNames.length; i++) {
    const name = extExportNames[i];
    const address = Module.findExportByName('libnook-agent.so', name);
    if (address === null) {
      missingExtExports.push(name);
    } else {
      extExports[name] = address;
    }
  }

  if (missingExtExports.length !== 0) {
    send({
      type: 'send',
      payload: `native-function-ext-missing:${missingExtExports.join(',')}:restart-target-process-required`
    });
  } else {
    const boolNot = new NativeFunction(extExports.NookNativeFunctionSmokeBoolNot, 'bool', ['bool']);
    const addS16 = new NativeFunction(extExports.NookNativeFunctionSmokeAddS16, 'int16', ['int16', 'int16']);
    const addU64 = new NativeFunction(extExports.NookNativeFunctionSmokeAddU64, 'uint64', ['uint64', 'uint64']);
    const addFloat = new NativeFunction(extExports.NookNativeFunctionSmokeAddFloat, 'float', ['float', 'float']);
    const addDouble = new NativeFunction(extExports.NookNativeFunctionSmokeAddDouble, 'double', ['double', 'double']);
    const mixU64Double = new NativeFunction(extExports.NookNativeFunctionSmokeMixU64Double, 'uint64', ['uint64', 'double']);
    const mixFloatU32 = new NativeFunction(extExports.NookNativeFunctionSmokeMixFloatU32, 'float', ['float', 'uint32']);
    const mixDoubleU32 = new NativeFunction(extExports.NookNativeFunctionSmokeMixDoubleU32, 'double', ['double', 'uint32']);
    send({
      type: 'send',
      payload: `native-function-ext:${typeof boolNot(true)}:${String(boolNot(true))}:${addS16(-7, 35)}:${addU64(uint64('4294967296'), 10).toString()}:${addFloat(1.25, 2.5).toFixed(2)}:${addDouble(1.5, 2.25).toFixed(2)}`
    });
    send({
      type: 'send',
      payload: `native-function-mixed-ext:${mixU64Double(uint64('4294967296'), 2.5).toString()}:${mixFloatU32(1.25, 2).toFixed(2)}:${mixDoubleU32(4.5, 2).toFixed(2)}`
    });

    const boolCallback = new NativeCallback(function(value) {
      return !value;
    }, 'bool', ['bool']);
    const boolCallbackInvoke = new NativeFunction(boolCallback, 'bool', ['bool']);
    const u64Callback = new NativeCallback(function(left, right) {
      return (left.toString() === '4294967296' && right === 9) ? uint64('4294967305') : uint64('0');
    }, 'uint64', ['uint64', 'uint32']);
    const u64CallbackInvoke = new NativeFunction(u64Callback, 'uint64', ['uint64', 'uint32']);
    const floatCallback = new NativeCallback(function(left, right) {
      return left + right;
    }, 'float', ['float', 'float']);
    const floatCallbackInvoke = new NativeFunction(floatCallback, 'float', ['float', 'float']);
    const doubleCallback = new NativeCallback(function(left, right) {
      return left + right;
    }, 'double', ['double', 'double']);
    const doubleCallbackInvoke = new NativeFunction(doubleCallback, 'double', ['double', 'double']);
    send({
      type: 'send',
      payload: `native-callback-ext:${typeof boolCallbackInvoke(true)}:${String(boolCallbackInvoke(true))}:${u64CallbackInvoke(uint64('4294967296'), 9).toString()}:${floatCallbackInvoke(1.25, 2.5).toFixed(2)}:${doubleCallbackInvoke(1.5, 2.25).toFixed(2)}`
    });
  }

  const readBlob = copyDst.readByteArray(5);
  const readBytes = new Uint8Array(readBlob);
  send({
    type: 'send',
    payload: `byte-array-read:${readBlob.byteLength}:${readBytes[0]}:${readBytes[1]}:${readBytes[2]}:${readBytes[3]}:${readBytes[4]}`
  });

  copyDst.writeByteArray([79, 75]);
  send({
    type: 'send',
    payload: `byte-array-write:${copyDst.readUtf8String()}`
  });

  const dupBlob = Memory.dup(copySrc, 5);
  const dupBytes = new Uint8Array(dupBlob);
  send({
    type: 'send',
    payload: `dup:${dupBlob.byteLength}:${dupBytes[0]}:${dupBytes[1]}:${dupBytes[2]}:${dupBytes[3]}:${dupBytes[4]}`
  });

  send({
    type: 'send',
    payload: `hex:${hexdump(dupBlob)}`
  });

  send({
    type: 'send',
    payload: `hex-styled:${hexdump(dupBlob, { header: true })}`
  });

  rpc.exports = {
    roundtrip(value) {
      const pointer = Memory.alloc(8);
      pointer.writeU64(value);
      return {
        value: pointer.readU64()
      };
    }
  };
})();
