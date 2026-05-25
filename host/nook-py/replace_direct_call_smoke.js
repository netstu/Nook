(function () {
  const moduleName = "libnook-agent.so";
  const symbolName = "NookNativeFunctionSmokeAdd";
  const targetAddress = Module.getExportByName(moduleName, symbolName);
  const add = new NativeFunction(targetAddress, "uint32", ["uint32", "uint32"]);

  send({
    type: "send",
    payload: "replace-direct-target:" + String(targetAddress),
  });

  send({
    type: "send",
    payload: "replace-direct-before:" + String(add(7, 35)),
  });

  Interceptor.replace(add, function (left, right) {
    return add.original(left, right) + 1;
  });
  send({
    type: "send",
    payload: "replace-direct-after:" + String(add(7, 35)),
  });
  send({
    type: "send",
    payload: "replace-direct-original:" + String(add.original(7, 35)),
  });

  Interceptor.revert(add);
  send({
    type: "send",
    payload: "replace-direct-reverted:" + String(add(7, 35)),
  });
})();
