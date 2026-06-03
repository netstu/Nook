"use strict";

(function () {
  var nextTokenId = 1;

  function normalizeModule(module) {
    if (!module) {
      return null;
    }
    return {
      name: String(module.name || ""),
      path: String(module.path || ""),
      base: module.base.toString(),
      size: Number(module.size || 0)
    };
  }

  function makeToken() {
    var token = "sodump-" + String(nextTokenId++);
    return token;
  }

  function sendError(token, error) {
    send({
      type: "sodump-error",
      token: token,
      error: String(error || "module dump failed")
    });
  }

  function beginModuleDump(moduleName, options) {
    options = options || {};
    var token = makeToken();
    var module = Process.findModuleByName(moduleName);
    if (module === null) {
      sendError(token, "module '" + moduleName + "' not found");
      return {
        token: token,
        size: 0,
        chunk_size: 0,
        chunks: 0
      };
    }

    var chunkSize = Number(options.chunk_size || (32 * 1024));
    if (chunkSize <= 0) {
      chunkSize = 32 * 1024;
    }

    if (options.try_protect) {
      try {
        Memory.protect(ptr(module.base), module.size, "rwx");
      } catch (e) {
      }
    }

    var totalSize = Number(module.size || 0);
    var chunks = totalSize === 0 ? 0 : Math.ceil(totalSize / chunkSize);
    for (var index = 0; index < chunks; index++) {
      var offset = index * chunkSize;
      var currentSize = Math.min(chunkSize, totalSize - offset);
      var payload = ptr(module.base).add(offset).readByteArray(currentSize);
      send({
        type: "sodump-chunk",
        token: token,
        index: index,
        chunks: chunks,
        size: currentSize,
        eof: index === chunks - 1
      }, payload);
    }

    return {
      token: token,
      size: totalSize,
      chunk_size: chunkSize,
      chunks: chunks,
      module: normalizeModule(module),
      arch: Process.arch
    };
  }

  rpc.exports = {
    listmodules: function () {
      return (Process.enumerateModules() || []).map(normalizeModule);
    },
    findmodule: function (moduleName) {
      return normalizeModule(Process.findModuleByName(moduleName));
    },
    beginmoduledump: function (moduleName, options) {
      return beginModuleDump(moduleName, options);
    }
  };
}());
