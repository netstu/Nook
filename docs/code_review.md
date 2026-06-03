# Nook Current Progress Review

## Scope

This note summarizes the recent runtime and host-side work done to move Nook closer to Frida-style scripting behavior, with emphasis on:

- `Module` / `Process` / `Memory` / `NativePointer` script APIs
- `Interceptor.attach(...)` and deferred module+symbol install
- `Thread.backtrace(...)` and `DebugSymbol.fromAddress(...)`
- observer-mode hook execution and end-to-end smoke validation

The goal of this review is not to restate every commit, but to capture the current state, the main problems encountered, and the concrete fixes that were applied.

## What Has Been Implemented

### 1. Frida-style JS runtime surface expanded substantially

The QuickJS runtime now exposes a much broader script-facing API surface than the initial smoke-only stage. The currently validated subset includes:

- `Module.enumerateModules()`, `find/getBaseAddress()`, `find/getExportByName()`, `find/getSymbolByName()`, `enumerateExports()`, `enumerateSymbols()`, `enumerateImports()`, `find/getImportByName()`, `load()`, `ensureInitialized()`, `find/getGlobalExportByName()`
- `Process.enumerateRanges()`, `findRangeByAddress()`, `getModuleByAddress()`, `enumerateModules()`, `find/getModuleByName()`, `mainModule`, `pointerSize`, `pageSize`, `arch`, `platform`, `id`, `isDebuggerAttached()`, `getCurrentThreadId()`, `enumerateThreads()`
- `Memory.alloc*()`, `copy()`, `dup()`, `protect()`, `patchCode()`, `scanSync()`, `scan()`
- `NativePointer` arithmetic and guarded reads/writes for pointer, integer, float, double, byte-array, UTF-8, and UTF-16 operations
- `NativeFunction`, `NativeCallback`, `Interceptor.attach()`, `Interceptor.replace()`, `Interceptor.revert()`

This is already enough to support meaningful Frida-like smoke scripts against the demo target instead of only unit tests.

### 2. Deferred module+symbol hook flow is working

The runtime and bridge now support attaching by `{ module, symbol }` even when the target `.so` is not yet loaded. This was exercised repeatedly with:

- `native_hook.js`
- `interceptor_hook.js`
- `strcmp_hook.js`
- login-related native target functions in `libnative-lib.so`

Observed result:

- scripts can load before the target library appears
- hook install completes later when the module becomes available
- reload and unhook flows also work in the CLI / REPL path

### 3. `Thread.backtrace(...)` now has Frida-style call shapes and real mode split

The runtime now accepts and validates:

- `Thread.backtrace()`
- `Thread.backtrace(Backtracer.ACCURATE)`
- `Thread.backtrace(Backtracer.FUZZY)`
- `Thread.backtrace(this.context, Backtracer.ACCURATE)`
- `Thread.backtrace(this.context, Backtracer.FUZZY)`

Behavioral split:

- `ACCURATE` uses current-thread unwind or hook-context `pc/lr/fp` reconstruction
- `FUZZY` uses stack-pointer-based executable-address scanning

This removed the earlier fake-enum situation where both modes behaved the same.

### 4. Observer-mode native hooks were added with `blocking: false`

A major runtime change was introduced for native hook callbacks:

- default remains `blocking: true`
- `blocking: false` is now supported for observer-only hook callbacks

Current semantics:

- `blocking: true`: hooked thread waits for JS callback completion and may consume arg / retval mutations
- `blocking: false`: JS callback still runs, but the hooked thread does not wait and mutation results are ignored for that invocation

Relevant implementation points:

- hook option parsing in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp#L7315)
- JS event dispatch branching in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp#L11027)
- native bridge wait-skipping in [nook_native_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_native_js_bridge.cpp#L815)

This was necessary because backtrace-heavy observer scripts were stalling the target thread too much under the original synchronous model.

### 5. `DebugSymbol.fromAddress(...)` caching was added

Another major runtime fix was reducing repeated symbolization cost. The current implementation now keeps:

- a runtime-local loaded-module cache
- a per-module export cache keyed by module base

Cache management:

- populated on first demand
- reused for repeated symbolization
- invalidated on `Module.load(...)`
- invalidated on runtime init and shutdown

Relevant implementation points:

- cache state in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp#L153)
- cache helpers in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp#L2084)
- symbol lookup path in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp#L9528)

This materially improved `DebugSymbol.fromAddress(...)` behavior inside backtrace-heavy scripts.

### 6. Host smoke scripts and docs were updated to reflect the runtime reality

Notable host-side adjustments:

- `thread_backtrace_hook.js` now uses `blocking: false`
- fuzzy backtrace previews symbolize fewer frames than accurate previews
- deferred native-js installs now emit host-visible `hook-status` events so attach / repl workflows can observe `pending`, `installed`, and `failed`
- docs were updated to describe:
  - the `Backtracer` mode split
  - `blocking: false` observer semantics
  - `DebugSymbol.fromAddress(...)` caching
  - deferred install status visibility

Relevant smoke script:

- [thread_backtrace_hook.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/thread_backtrace_hook.js)

### 7. `Script.bindWeak(...)` / `Script.unbindWeak(...)` now has a working generic baseline

Nook now has a generic weak-binding primitive at the script runtime layer, aimed at Frida-style semantics:

- `Script.bindWeak(value, callback)`
- `Script.unbindWeak(token)`

Current implementation direction:

- uses QuickJS `FinalizationRegistry` instead of the earlier custom hidden-finalizer-cell experiment
- keeps deterministic cleanup on script unload through the existing unload path
- keeps explicit Java wrapper cleanup (`$dispose()`) as a separate mechanism

Validated desktop coverage now includes:

- API presence
- GC-triggered callback delivery
- `unbindWeak(...)` suppressing GC callback delivery
- unload-triggered callback delivery

## Problems Encountered and How They Were Resolved

### Problem 1: Hooking before `.so` load was fragile

Early symptom:

- attach scripts failed when the target library was not yet loaded
- users had to manually time attach after entering the login page

Root cause:

- initial hook path assumed target exports were immediately available

Resolution:

- added deferred module+symbol attach/install flow
- reused module-load observation so install completes when the library actually appears

Current status:

- fixed for the tested attach paths
- repeated user validation confirms deferred install now works in the demo target flow

### Problem 2: Observer hooks caused visible app/UI stalls

Early symptom:

- `thread_backtrace_hook.js` caused obvious freezes for several seconds
- especially visible around `Thread.backtrace(...)` + `DebugSymbol.fromAddress(...)`

Root causes:

1. hook thread synchronously waited for JS callback completion
2. symbolization repeatedly rescanned modules/exports in hot paths

Resolution:

- introduced `blocking: false` observer mode
- kept default `blocking: true` to avoid breaking mutation semantics
- added `DebugSymbol.fromAddress(...)` caches

Current status:

- the user reported the app no longer freezes
- `thread-backtrace-hook-fuzzy` is still somewhat slower than accurate mode, but the stall is now small and script-local instead of app-wide

### Problem 3: `Thread.backtrace` API shape lagged behind Frida usage

Early symptom:

- only context-driven or narrower forms were reliable
- no-arg and mode-only Frida-style calls were incomplete

Root cause:

- JS argument parsing and mode handling were still in staging form

Resolution:

- expanded parser and added explicit mode validation
- split accurate and fuzzy runtime behavior

Current status:

- fixed for the validated smoke flows
- output now clearly shows different frame counts for accurate vs fuzzy in many runs

### Problem 4: `DebugSymbol` cache implementation first failed to compile

Symptom during implementation:

- the first cache version did not compile

Root cause:

- cache fields were added to `RuntimeState` before `NativeModuleRecord` and `NativeModuleExportRecord` were declared

Resolution:

- moved the module/export struct declarations above `RuntimeState`
- kept the cache logic otherwise minimal

Current status:

- fixed
- local runtime tests and Android rebuild passed after this correction

### Problem 5: Some exported/native function smoke scripts were unstable or crashed when hook targets were too risky

Observed examples during iteration:

- hooking broad libc functions such as `strcmp` could crash or behave unpredictably
- some target exports were unavailable until the target module loaded
- some early demo hooks were too invasive for a stable smoke baseline

Resolution:

- changed smoke focus toward safer target-specific functions inside the demo app
- added deferred-capable module+symbol paths
- reduced reliance on globally hot libc symbols for baseline smoke coverage

Current status:

- the current smoke set is much more stable than the initial exploratory scripts

### Problem 6: `MainActivity.incrementIntercept()` is a weak static-Java smoke target

Observed symptom:

- the static Java hook installs successfully for `com.demo.target.MainActivity.incrementIntercept`
- runtime logs show `forced_interpret_ok=1` and shared-stub replacement install success
- but the JS replacement callback still does not fire on device

Evidence gathered:

- app logs confirm that the surrounding caller methods do execute after hook install:
  - `AdWallFragment.loadAd()`
  - `ButtonFragment.validateForm()`
  - `TextFragment.updateUserInfo()`
  - `LoginFragment.attemptLogin()`
- each of those methods contains a direct source-level call to `MainActivity.incrementIntercept()`
- despite that, there is still no runtime hit in:
  - `java-static-app-enter`
  - `DoCallEnterCallback`
  - `StaticNativeHookCallback`

Most likely root cause:

- `incrementIntercept()` is an extremely small static method:
  - increments one static integer
  - returns `void`
- this makes it a strong candidate for AOT/JIT inlining or other direct-call optimization
- in that case, switching pages or triggering the surrounding feature does not guarantee an actual runtime dispatch to the hooked `ArtMethod`

Why this matters:

- failure on this target does not necessarily mean the static-hook installation path is broken
- it is a poor sentinel for validating end-to-end static Java dispatch on real devices

Current recommendation:

- do not use `incrementIntercept()` as the primary real-device proof target for static Java hooking

### Problem 7: initial `Script.bindWeak(...)` GC delivery never reached the script callback

Observed symptom:

- weak-binding registration succeeded
- target objects became unreachable
- cleanup machinery ran
- but the user callback still never observed `"gc"`

Root causes found during desktop debugging:

1. the first generic implementation used a custom finalizer-cell approach, but the runtime was later moved to QuickJS `FinalizationRegistry`, which is closer to the intended generic Frida-style primitive
2. during that migration, `FinalizationRegistry.register(...)` argument duplication leaked a strong JS reference, keeping the target alive
3. after that leak was fixed, the real blocking bug was in weak-callback dispatch:
   - callers marked the record as `fired = true`
   - `DispatchWeakBindingCallbackLocked(...)` refused to dispatch records already marked `fired`
   - result: cleanup callback arrived, but script callback was skipped

Resolution:

- switched the runtime path to QuickJS `FinalizationRegistry`
- released temporary duplicated JS arguments after `register(...)` / `unregister(...)`
- moved `fired` state transition into `DispatchWeakBindingCallbackLocked(...)` so the dispatch gate and caller logic no longer contradict each other
- removed the obsolete custom weak finalizer-cell bootstrap path
- kept `JsRuntimeRunGcForTesting()` as a test helper that repeatedly runs GC and drains pending jobs so weak-callback tests stay deterministic

Current status:

- desktop weak-binding tests now pass end-to-end
- the runtime now has a cleaner generic weak-binding baseline for future Frida-alignment work

### Problem 8: unload-triggered `bindWeak(...)` callback message was lost in `nook-cli --wait`

Observed symptom on device:

- runtime-side script unload succeeded
- `script unload ok: script_id=...` was printed by the CLI
- but the unload-triggered weak callback message was not shown to the user

Root cause:

- the host `--wait` path stopped the main message loop on `Ctrl+C`
- cleanup then called `script.unload()`
- but the CLI exited immediately after the unload response, without draining any script messages emitted during unload cleanup

Resolution:

- updated `host/nook-py/nook/cli.py` so wait-mode cleanup performs a short post-unload message drain window
- added CLI coverage in `host/nook-py/tests/test_cli.py` for unload-time message delivery

Verification:

- `python -m unittest host.nook-py.tests.test_cli`

### Problem 9: Java owned wrappers only released on unload/shutdown instead of wrapper GC

Observed limitation:

- `Java.retain(...)` correctly produced owned wrappers
- native fallback cleanup released those handles on unload / registry clear / shutdown
- but simply letting the JS wrapper die did not release the retained Java handle

Root cause:

- the generic `Script.bindWeak(...)` runtime existed, but Java retained wrappers were not consuming it yet
- `Java.ClassFactory.retain(...)` also re-cast retained wrappers in a way that dropped ownership semantics from the returned wrapper

Resolution:

- updated the Java bootstrap so `Java.retain(...)` attaches weak cleanup to the returned owned wrapper using `Script.bindWeak(...)`
- updated `Java.ClassFactory.retain(...)` to preserve owned-wrapper semantics on the returned loader-aware wrapper
- updated wrapper `$dispose()` to call `Script.unbindWeak(...)` before native release, preventing later double release
- kept native `owned_java_handles` tracking as a deterministic fallback for unload / shutdown

Verification:

- added desktop coverage in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - GC of a retained wrapper triggers `Java.release(...)`
  - `Java.ClassFactory.retain(...)` returns a real owned wrapper and `$dispose()` releases exactly once
- re-ran:
  - `build\\test_js_runtime_native_attach.exe`

### Problem 7: cold-spawn Java field hook still missed the first `AdWallFragment.loadAd()` calls

Observed symptom:

- `java_field_smoke.js` could print:
  - `java-field-bootstrap-installed`
  - `java-field-deopt:...`
  - `java-field-init-installed:...`
  - `java-field-installed:...`
  - `java-field-ready:initial`
- but on a cold spawn there was still no `java-field-instance:...` output until the user switched away from the ad page and came back later

Evidence:

- earlier logcat showed the app's first `AD_TARGET: Loading ad: ...` lines happening before the deferred Java hook was actually installed
- Frida comparison was stable on the same target and could catch the first three `loadAd()` calls during cold start

Root cause:

- `JavaHook::FindClass()` only used `findLoadedClass()` for non-bootstrap app classes
- when `AdWallFragment` had not been loaded yet, Nook returned failure immediately instead of using the app class loader's `loadClass()`
- this forced `loadAd.implementation = ...` into the deferred polling path, which is inherently too late for the first screen's early calls

Resolution:

- updated [JavaHook.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/java_hook/JavaHook.cpp) so `JavaHook::FindClass()` now falls back to `JavaHookLoaderResolver::LoadClassWithLoader(...)` for app classes too, not just bootstrap classes
- added a source regression check in [test_java_hook_runtime_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_java_hook_runtime_regressions.cpp) to prevent reintroducing the old `if (!bootstrapClass) return nullptr;` gate
- rebuilt and repushed:
  - `/data/local/tmp/nook/nook-server`
  - `/data/local/tmp/nook/libnook-agent.so`
  - `/data/local/tmp/nook/libnook.so`

Expected impact:

- `Java.use("com.demo.target.AdWallFragment")` + `implementation = ...` should behave much closer to Frida on cold spawn
- first-screen hooks should no longer depend on deferred retry timing when the class is resolvable through the app class loader

### Problem 8: `spawn` gate existed in code but was never actually enforced in the target process

Observed symptom:

- host CLI ordering was correct on paper:
  - `spawn`
  - `agent ready`
  - `script create/load`
  - `resume`
- but real-device timestamps proved the target app had already executed first-screen code before `script load ok`

Concrete evidence:

- latest device log showed:
  - `17:04:03.303` `AD_TARGET: Loading ad: native at list_item_1/2/3`
  - `17:04:05.509` `Hooked successfully: com.demo.target.AdWallFragment.loadAd`
  - `17:04:05.510` `script load ok script_id=1`

Root cause:

- [NookComm.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookComm.cpp) already had:
  - spawn gate state
  - resume handler
  - `NookCommWaitForResumeIfSpawned()`
- but `NookAgentInitialize()` never called `NookCommWaitForResumeIfSpawned()`
- result: the target process never actually blocked on the spawn gate, so `spawn --resume` only looked suspended from the protocol side

Resolution:

- updated [NookComm.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookComm.cpp) so `NookAgentInitialize()` now:
  - initializes comm
  - initializes the script runtime bridge
  - then immediately calls `NookCommWaitForResumeIfSpawned()`
- this preserves the intended phase-1 order:
  - target connects and registers script callbacks
  - host creates/loads script while the gate is still held
  - only `resume` releases the process
- also added a regression source check in [test_java_hook_runtime_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_java_hook_runtime_regressions.cpp)

### Problem 7: Nested Java hooks corrupted instance-field receiver context on Android

Observed symptom:

- `java_field_smoke.js` installed successfully under `spawn --resume --wait`
- switching to the ad page then crashed the app
- the last script output before the crash was:
  - `java-field-bootstrap-installed`
  - `java-field-deopt:true:true:invalidated:504`
  - `java-field-static:object:I:true:3:13:3`
  - `java-field-installed:(Ljava/lang/String;Ljava/lang/String;)V:false`
  - `java-field-ready:initial`

Device-side crash evidence:

- ART aborted with:
  - `JNI DETECTED ERROR IN APPLICATION: jfieldID int com.demo.target.AdWallFragment.adCount not valid for an object of class java.lang.Class<android.util.Log>`

Root cause:

- the Android Java-JS bridge tracked active Java hook context with a per-thread single slot:
  - one `JNIEnv*`
  - one `thiz`
  - one signature record
- `java_field_smoke.js` uses `android.util.Log.d` as a bootstrap hook, and later installs an instance hook for `AdWallFragment.loadAd`
- when nested or re-entrant Java hook dispatch happened on the same thread, the later hook overwrote the earlier active invocation record
- default Android instance-field access then resolved `this.adCount` against the wrong active `thiz`, so `AdWallFragment.adCount` was applied to the `android.util.Log` receiver/class path and ART aborted

Resolution:

- changed active Java invocation tracking from:
  - `unordered_map<thread_id, ActiveJavaJsInvocation>`
- to:
  - `unordered_map<thread_id, vector<ActiveJavaJsInvocation>>`
- `HandleJavaJsHookInvocation(...)` now:
  - pushes the current invocation before dispatch
  - reads the top-of-stack for field access / `callOriginal`
  - pops after dispatch completes
- added a source-level regression check requiring stack semantics in:
  - [test_java_hook_runtime_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_java_hook_runtime_regressions.cpp)

Files changed for this fix:

- [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp)
- [test_java_hook_runtime_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_java_hook_runtime_regressions.cpp)

Current status:

- local regression build and run passed
- updated Android arm64 artifacts were rebuilt and pushed to device
- real-device revalidation is required with `java_field_smoke.js`

## Problem 7: `--spawn-symbi` depended on foreground `Ctrl+C` to restore zygote state

Observed behavior before this change:

- `Ninjector --spawn-symbi` patched the zygote ART slot and shellcode page
- the injector then stayed resident and waited for manual `Ctrl+C`
- restore only happened on `SIGINT` / `SIGTERM`

Why this was a problem:

- cleanup semantics depended on the foreground terminal staying alive
- a crash or disconnect could leave the zygote patch in place longer than intended
- this was acceptable for mechanism study, but not for a Frida-style spawn transaction

Root cause:

- the symbi stub had no callback path back to the injector
- the injector therefore had no precise completion event and fell back to manual lifetime control

Resolution applied:

- extended the symbi stub config with callback metadata:
  - abstract Unix socket name
  - remote `getpid()` / `getppid()`
  - existing remote socket/write helpers are now used
- the stub now sends one compact callback message after the target-UID `dlopen(...)` attempt
- the injector now:
  - creates a local abstract socket listener
  - patches zygote
  - starts the target app
  - waits for one callback or timeout
  - always restores the original zygote slot and shellcode page before returning

Current status:

- manual `Ctrl+C` is no longer required for the symbi flow
- the rebuilt `Ninjector` binary has been pushed to `/data/local/tmp/Ninjector/Ninjector`
- this is still a Ninjector-side transactional improvement, not yet the final Nook server-managed spawn gate

## Problem 8: the patched child could still hit the symbi stub multiple times before process exit

Observed behavior after auto-restore landed:

- the zygote-side patch was restored correctly
- but the already-forked child process could still log multiple `NSymbiStub` entries
- repeated `dlopen(...)` attempts happened inside the same child

Root cause:

- the symbi stub did not restore the child-local `setArgV0` slot on first entry
- after fork, the child inherits the patched slot value
- restoring the zygote later does not retroactively change the already-running child's copied state

Resolution applied:

- added a child-local one-shot restore in `stub_replacement_set_argv0(...)`
- on first stub entry, the stub now writes `slot_addr <- original_set_argv0`
- this mirrors the same general principle used by Frida's zymbiote payload: restore the hook point immediately after the first hit

Current status:

- future validation should show at most one effective symbi stub hit per child process
- this keeps the child-side behavior aligned with the injector's transactional cleanup model
- prefer a non-trivial static method whose body is less likely to be inlined away, or explicitly add caller-side / broader deoptimization support before treating this case as a framework regression

### Problem 7: Static Java hook access-flag normalization lagged behind the upstream Frida-style path

Observed gap:

- Nook's static Java hook path did enable `forced_interpret_only` and best-effort JIT invalidation
- but its access-flag rewrite logic was still weaker than the upstream Frida-style implementation

Concrete issues found:

- `kAccCriticalNative` was defined with the same value as the nterp fast-path flag, which is incorrect
- `getModifiedFlag()` did not clear `kAccFastInterpreterToInterpreterInvoke`
- `getModifiedFlag()` did not clear `kAccSingleImplementation`
- temporary ArtMethod recovery for `callOriginal` only added `CompileDontBother`, but did not normalize fast dispatch flags

Resolution applied:

- corrected `kAccCriticalNative` to `0x00200000`
- added missing access-flag constants for:
  - `kAccSingleImplementation`
  - `kAccFastInterpreterToInterpreterInvoke`
- added API-aware `CompileDontBother` selection instead of assuming one fixed bit value
- updated hooked-flag generation to clear:
  - fast native / critical native
  - nterp fast-path
  - fast interpreter-to-interpreter dispatch
  - single-implementation optimization
- updated temporary recovery flags used by `callOriginal` to also clear fast dispatch bits

Verification:

- added a regression check in [tests/headers/test_java_hook_runtime_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_java_hook_runtime_regressions.cpp)
- rebuilt and passed:
  - `build/test_java_hook_runtime_regressions.exe`
- rebuilt Android artifacts and pushed:
  - `/data/local/tmp/nook/libnook.so`
  - `/data/local/tmp/nook/libnook-agent.so`

### Problem 8: Java-side deopt and ART-router diagnostics were only available through native logs

Observed gap:

- once static Java hooks started working, further debugging still depended too much on logcat
- there was no script-level way to:
  - trigger JIT cache invalidation on demand
  - temporarily toggle forced-interpret mode
  - inspect ART router miss/debug state

Resolution applied:

- exposed new JS bindings on the `Java` object:
  - `Java.deopt()`
  - `Java._setForcedInterpretOnly(enable)`
  - `Java._artRouterDebug()`
- added corresponding `JavaHook` wrappers so the JS runtime can reuse the existing native implementation safely
- added smoke script:
  - [java_debug_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_debug_smoke.js)

Verification:

- added a regression check in [tests/headers/test_java_hook_runtime_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_java_hook_runtime_regressions.cpp)
- rebuilt and passed:
  - `build/test_java_hook_runtime_regressions.exe`
- rebuilt Android artifacts and pushed updated:
  - `/data/local/tmp/nook/libnook.so`
  - `/data/local/tmp/nook/libnook-agent.so`

## Current Validation Status

The following categories have been exercised during this round:

- C++ native tests for JS runtime and native JS bridge
- Python CLI tests
- Android `ndk-build`
- repeated on-device `spawn`, `attach`, `resume`, `post`, `unload`, `repl`
- on-device validation of:
  - native attach
  - deferred attach
  - interceptor attach
  - unload / reload
  - RPC call path
  - memory APIs
  - process / module APIs
  - replace / revert
  - retval replacement
  - UTF-16 pointer operations
  - thread backtrace and debug symbolization

Latest locally verified commands before pushing the current runtime included:

- `build\\test_js_runtime_native_attach_task66.exe`
- `build\\test_native_js_bridge_task66.exe`
- `python host\\nook-py\\tests\\test_cli.py`
- Android rebuild with `ndk-build`
- `adb push` for `libnook-agent.so`, `libnook.so`, and `nook-server`

## Current Residual Issues

### 1. `thread-backtrace-hook-fuzzy` is still not cheap

The major freeze is fixed, but fuzzy mode still does more work than accurate mode because it combines:

- stack scanning
- more candidate addresses
- repeated `send(...)` payload construction
- partial per-frame symbolization

This is now a performance polish problem, not a correctness blocker.

### 2. Observer-mode semantics are intentionally reduced

`blocking: false` cannot safely support:

- argument rewrite
- return-value replacement
- synchronous callback-dependent control flow

This is by design, but it must stay explicit in docs and examples so users do not treat it as a drop-in replacement for blocking hooks.

### 3. Frida parity is still partial

The project is much closer to Frida-style usage now, but it is not yet feature-complete relative to Frida. Current work is a staged subset that already supports realistic usage, not full API parity.

## Recommended Next Steps

### Near term

1. Reduce hot-path overhead in `thread_backtrace_hook.js`
   - symbolize fewer fuzzy frames
   - shorten preview output
   - avoid unnecessary string work inside `onEnter`

2. Keep expanding smoke coverage around the now-stable APIs
   - especially around mixed runtime usage and deferred hooks

### Medium term

3. Continue closing Frida API gaps in the runtime
   - prioritize APIs that unlock real scripts instead of edge-case completeness first

4. Separate runtime capability work from smoke verbosity
   - keep the runtime powerful
   - keep default smoke scripts cheap enough for interactive testing

## Review Summary

Overall assessment:

- the runtime has moved from a narrow smoke environment to a usable Frida-like scripting substrate
- the biggest practical blocker in this phase was observer-hook-induced stalling
- that blocker is now substantially resolved through `blocking: false` plus `DebugSymbol` caching
- remaining issues are mostly optimization and further API expansion, not architectural dead-ends

The most important review question at this point is no longer "does it work at all", but:

- whether the current observer/runtime split is the right long-term compatibility contract for Frida-like behavior
- and whether the next batch of work should focus on deeper parity or on making the existing surface cheaper and more ergonomic

## 2026-04-25 Step6 Java Dispatch Update

### What was completed

- Closed the `Java.perform()` minimal path from installed Java hook back into the JS runtime.
- Added runtime-level dispatcher registration in `js_runtime.cpp` so Java hook callbacks no longer depend only on the test helper entrypoint.
- Unified the actual Java callback invocation path and the testing path through the same internal helper.
- Fixed Android agent-side link dependencies in `build/android/Android.mk` so `libnook-agent.so` and the smoke agents can link the Java hook bridge successfully.

### Root cause

The new Java bridge code in `nook_java_js_bridge.cpp` already knew how to:

- install deferred Java hooks
- dispatch native Java callback thunks by `hook_id`
- call back into JS
- call `callOriginal(...)`

But `js_runtime.cpp` had not yet registered a runtime dispatcher with the bridge. That left the bridge with only:

- direct test helper invocation via `JsRuntimeInvokeJavaHookCallbackForTesting(...)`
- no real runtime-owned `hook_id -> script_id -> JS callback` dispatch path

Separately, Android build failures came from `NOOK_AGENT_SRC` not linking the Java hook stack now required by `nook_java_js_bridge.cpp`.

### Fix

Runtime side:

- added `DispatchJavaHookInvocationToRuntime(...)`
- added `InvokeJavaHookCallbackLocked(...)`
- registered the dispatcher during `JsRuntime::Initialize()`
- reset the dispatcher during `JsRuntime::Shutdown()`
- routed `JsRuntimeInvokeJavaHookCallbackForTesting(...)` through the same helper

Build side:

- extended `NOOK_AGENT_SRC` to include:
  - `NookJavaHook.cpp`
  - `NookJavaHookPayload.cpp`
  - `JavaHook.cpp`
  - `JVM.cpp`
  - deferred Java hook registry / resolver / observer sources
  - Java hook logging and ART structure detection sources

### Verification

Locally verified:

- `build\\test_java_js_bridge_task70.exe`
- `build\\test_js_runtime_native_attach_task70.exe`

Android verified:

- `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk -j4`
- pushed:
  - `libs/arm64-v8a/libnook.so`
  - `libs/arm64-v8a/libnook-agent.so`
  - `libs/arm64-v8a/nook-server`

### Deployment pitfall found during validation

This repo's `ndk-build` output is currently emitted to the repository-root directories:

- `obj/local/...`
- `libs/arm64-v8a/...`

It is **not** emitted to:

- `build/android/obj/...`
- `build/android/libs/...`

An earlier validation round mistakenly inspected and pushed the stale `build/android/libs/...` artifacts, which explained why device-side runtime behavior did not match the latest source or local unit-test results. The correct deployment source for current Android artifacts is `libs/arm64-v8a/`.

### Remaining gap

The current device smoke for `java_perform_smoke.js` still mainly proves:

- `Java.perform`
- `Java.use`
- implementation installation path

The next important validation is a real device-side Java callback entering JS and preserving app behavior, so the smoke should be tightened around a concrete target method instead of API shape only.

## 2026-04-25 Java deferred hook crash on login-page load

### Symptom

- `nook-cli attach ... -l host/nook-py/java_perform_smoke.js --wait --usb`
- script load succeeded
- entering the login page crashed the target process before the hooked method was invoked

Observed device behavior before the fix:

- `java-bindings:object:function:function`
- `java-wrapper:object:function:function`
- `java-implementation-installed`
- crash happened when the page was inflating / resolving classes, not when pressing the login button

### Root cause

The deferred Java hook path installed an observer on `ClassLoader.loadClass(...)` and then immediately scheduled real `HookMethod(...)` work from a background thread as soon as the target class name was seen.

That means the hook install could race with ART class loading itself:

- class name observed before the target class was fully ready
- background retry thread entered `FindClass` / ART hook install while class loading was still active
- the result was an unsafe JNI / ClassLinker interaction and a process crash during page inflation

This was an architecture problem in the deferred install trigger, not in the JS bridge added for `Java.perform(...)`.

### Fix

The deferred installer was simplified to a safer model:

- removed the `ClassLoader.loadClass(...)` observer hook
- removed the one-shot detached install thread pattern
- added a single retry worker thread in `java_hook_class_observer.cpp`
- the worker now polls pending deferred Java hooks and installs them only after normal class resolution succeeds
- `Reset()` now stops and joins the retry worker cleanly

Supporting changes:

- added `PendingJavaHookRegistry::HasAnyPending()`
- added a regression check in `tests/headers/test_java_hook_runtime_regressions.cpp` to prevent reintroducing the old `loadClass` observer path

### Verification

Verified locally after the fix:

- `build/test_java_hook_runtime_regressions.exe`
- `build/test_js_runtime_native_attach_task70.exe`
- Android rebuild via `ndk-build`

Deployment after rebuild:

- pushed `libs/arm64-v8a/libnook.so`
- pushed `libs/arm64-v8a/libnook-agent.so`
- pushed `libs/arm64-v8a/nook-server`

### Expected device result after redeploy

For `java_perform_smoke.js`:

- attach and script load should still succeed
- entering the login page should no longer crash the app
- after triggering the target method, the smoke should print:
  - `java-hook-enter:<password>`
  - `java-hook-leave:<result>`

## 2026-04-25 Java.perform install-success-but-no-callback follow-up

### Symptom

After the crash fix, `java_perform_smoke.js` loaded successfully and no longer crashed the app, but pressing the login button still produced no:

- `java-hook-enter:...`
- `java-hook-leave:...`

### Root cause

`JsJavaInstallImplementation()` currently installs Java hooks with:

- `request.signature = "*"`

But the lower `JavaHook::FindMethod()` path originally only supported exact JNI signatures and used `GetMethodID()` / `GetStaticMethodID()` directly.

So the runtime behavior became:

- JS side thought the implementation was installed
- deferred Java hook request was registered successfully
- actual hook install kept failing because `*` is not a valid JNI method signature

This explained the "load ok but never enters callback" symptom exactly.

### Fix

Added wildcard Java method resolution in `JavaHook.cpp`:

- if `methodSignature == "*"`
- enumerate `getDeclaredMethods()`
- match by method name + static/instance shape
- build the real JNI signature from reflected parameter/return types
- obtain the real `jmethodID` through `FromReflectedMethod()`
- reject ambiguous matches

This is enough for the current minimal `Java.perform()` path as long as the target method is not overloaded.

### Verification

Verified locally:

- `build/test_java_hook_runtime_regressions.exe`
- `build/test_js_runtime_native_attach_task70.exe`

Android artifacts were rebuilt and redeployed again after this fix.

## 2026-04-25 Java.perform callOriginal hang after enter

### Symptom

Latest device behavior moved one step further:

- `java-bindings:object:function:function`
- `java-wrapper:object:function:function`
- `java-implementation-installed`
- `java-hook-enter:222222`

But then:

- no `java-hook-leave:...`
- the app UI stalled during the button click

So the remaining problem was no longer:

- Java hook install
- deferred attach timing
- JS callback entry into QuickJS

It was specifically the original-method invocation path behind:

- `this.verifyPasswordNative.callOriginal(password)`

### Root cause

The most plausible deadlock path in the current implementation was:

1. Java hook entered `hook_handler(...)`
2. callback crossed into QuickJS and printed `java-hook-enter:...`
3. JS called `callOriginal(...)`
4. `JavaHook::InvokeOriginalMethod(...)` invoked the persistent `backupArtMethod`
5. for native Java methods, that invocation path could still re-enter the installed hook instead of cleanly running the original implementation

Once that happened, there were two bad outcomes available:

- re-enter the same per-hook mutex already held by the outer callback path
- or re-enter the runtime callback path while the outer JS dispatch was still active

Either way, the externally visible symptom was exactly what the device showed:

- first `enter` log appears
- no `leave`
- app appears hung

### Fix

Changed Java original invocation to stop directly reusing the long-lived `backupArtMethod` pointer during the actual invoke call.

Instead, the runtime now:

- refreshes original entry metadata through `sync_backup_artmethod(...)`
- clones a fresh ArtMethod snapshot from the current live method
- calls `recover_artmethod(...)` on that temporary clone
- invokes the temporary recovered ArtMethod
- frees the temporary clone after the call returns

This reduces the chance of stale or re-hooked state being reused across `callOriginal(...)` invocations, and matches the safer "fresh invoke copy" pattern that is typically needed around ART-native method dispatch.

### Verification

Verified locally after the change:

- rebuilt `build/test_java_hook_runtime_regressions.exe`
- `build/test_java_hook_runtime_regressions.exe`
- `build/test_js_runtime_native_attach_task70.exe`
- Android rebuild via `ndk-build`

Redeployed:

- `libs/arm64-v8a/libnook.so`
- `libs/arm64-v8a/libnook-agent.so`
- `libs/arm64-v8a/nook-server`

### Pending device validation

Retest command:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_perform_smoke.js --wait --usb
```

Expected result now:

- `java-hook-enter:<password>`
- `java-hook-leave:true` or `java-hook-leave:false`
- no UI hang during button click

## 2026-04-25 Minimal Java.overload support

### Summary

Added the first narrow `Java.use(...).method.overload(...)` path so Java hooks can bind an exact method signature instead of always relying on the wildcard `*` method lookup.

Current scope is intentionally limited:

- primitive type names such as `boolean`, `int`, `long`, `float`, `double`
- normal Java class names such as `java.lang.String` or `com.demo.target.SomeClass`
- exact-signature preservation for both `implementation = fn` install and `callOriginal(...)`

Still not included:

- array type names
- constructors / fields / `Java.choose(...)`
- broader Frida Java overload surface beyond the minimal hook path

### Why this was needed

The earlier minimal Java path was good enough for non-overloaded methods because install used wildcard `*` resolution and the lower Java bridge could pick the single matching method.

That falls apart as soon as the target class has overloads:

- JS can name the method
- but cannot state which parameter list it wants
- so install and `callOriginal(...)` are ambiguous

For Frida-style usage, the first missing piece was not more Java APIs in general, but an exact-signature selection path that can flow through the existing bridge.

### Implementation

Implemented in two layers.

JS runtime:

- `Java.use(...)` method wrappers now expose `overload(...typeNames)`
- overload selection resolves a JNI-style signature through `__nookJavaResolveOverloadSignature(...)`
- the selected wrapper stores `$signature`
- `JsJavaInstallImplementation(...)` now prefers wrapper `$signature` over wildcard `*`
- Java callback receivers also preserve the installed record signature so `callOriginal(...)` keeps the exact method binding

Java bridge:

- added `ResolveJavaMethodSignature(...)` to `nook_java_js_bridge.*`
- Android runtime can resolve type-name lists through reflection when no test dependency is injected
- host tests can inject a fake signature resolver through `JavaJsHookInstallerDependencies.resolve_signature`

### Problems encountered

The first new overload tests failed during `registry.LoadScript(...)`.

Root cause:

- the non-Android unit tests execute overload selection inside the JS runtime
- but two tests only injected `install_hook` / `call_original_hook`
- they forgot to inject the new `resolve_signature` dependency
- so the overload resolver failed before hook install even ran

This was a test-harness issue, not a runtime design problem.

Fix:

- added `FakeResolveJavaMethodSignature` injection to the exact-signature install test
- added the same injection to the exact-signature `callOriginal(...)` test

### Verification

New coverage added in `tests/communication/test_js_runtime_native_attach.cpp`:

- overload wrapper selection returns a signature-bound wrapper
- overload install uses exact signature `(Ljava/lang/String;)Z`
- overload callback `callOriginal(...)` preserves the exact signature record

Verified locally:

- rebuilt `build/test_js_runtime_native_attach_task70.exe`
- `build/test_js_runtime_native_attach_task70.exe` passes cleanly

### Device smoke path

Added a dedicated host smoke script:

- [java_overload_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_overload_smoke.js)

Recommended device validation command:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_overload_smoke.js --wait --usb
```

Expected initial output:

- `java-overload-wrapper:object:(Ljava/lang/String;)Z:function`
- `java-overload-installed:(Ljava/lang/String;)Z`

Expected output after pressing the login button:

- `java-overload-enter:<password>`
- `java-overload-leave-original:true` or `java-overload-leave-original:false`

This smoke uses a non-overloaded target method on purpose. The goal here is not to prove method disambiguation against multiple same-name methods yet, but to prove that the device runtime now accepts the `overload("java.lang.String")` API shape and carries the resolved exact signature through the real Java install and original-call path.

## 2026-04-25 Real overloaded-target validation path

### Summary

After the first device smoke passed on `LoginFragment.verifyPasswordNative(String)`, the remaining gap was obvious: that smoke only proved the `overload(...)` API shape on a non-overloaded target.

To close that gap, a dedicated real overloaded-target validation path was added.

### Target choice

Used `TargetDemoApp` `TextFragment` as the validation surface and changed it to expose:

- `formatBalance(double)`
- `formatBalance(java.lang.String)`

The `double` overload now calls into the `String` overload so switching to the `TextFragment` page triggers both paths in one user action.

### Why this approach

Compared with using framework methods like `TextView.setText(...)`, the demo-app target is:

- easier to reason about
- easier to document
- less noisy
- stable enough for repeated regression testing

### Host smoke

Added:

- [java_overload_textfragment_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_overload_textfragment_smoke.js)

It installs both:

- `TextFragment.formatBalance.overload("double")`
- `TextFragment.formatBalance.overload("java.lang.String")`

and logs wrapper signatures plus per-overload enter/leave messages.

### Supporting test coverage

Extended `tests/communication/test_js_runtime_native_attach.cpp` with:

- primitive double overload wrapper selection
- distinct `double` and `java.lang.String` overload install on the same method name

This keeps the host-side runtime coverage aligned with the new real-device smoke path.

### Validation command

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_overload_textfragment_smoke.js --wait --usb
```

User action:

- switch to the `TextFragment` tab/page

Expected output:

- `text-overload-wrapper-double:(D)Ljava/lang/String;`
- `text-overload-wrapper-string:(Ljava/lang/String;)Ljava/lang/String;`
- `text-overload-installed`
- `text-overload-double-enter:10` or `10.0`
- `text-overload-string-enter:10.00`
- `text-overload-string-leave:BAL 10.00`
- `text-overload-double-leave:BAL 10.00`

## 2026-04-25 Java bridge scalar widening and minimal static-method path

### Summary

Extended the current minimal Java bridge in the next practical direction instead of widening API surface blindly:

- added bridge coverage for Java `long` and `float`
- kept `void`, `boolean`, `int`, `double`, and `java.lang.String`
- added the first minimal static-method hook path through the existing `Java.use(...).method.overload(...).implementation = fn` flow

This keeps the project aligned with the agreed priority:

- first make the current Frida-like path more correct and less brittle
- then expand outward

### Why this was needed

After the real overloaded-target validation passed, the next obvious gap was not another new Java API surface, but a type and metadata gap:

- overload selection already accepted primitive names like `long` and `float`
- but the bridge/runtime only had a partial scalar set wired through
- and overload resolution always assumed instance methods

That meant the current path was still weaker than it looked:

- some primitive signatures were not fully represented in the bridge
- `static` method hooks could not follow the same exact-signature wrapper path

### Implementation

Runtime changes:

- `JavaJsValueKind` now includes `kInt64` and `kFloat`
- `MakeJavaJsValue(...)` now emits JS numbers for `long` / `float`
- overload resolution now tries instance first and then static when resolving an exact signature
- overload-selected wrappers now preserve `$isStatic`
- `JsJavaInstallImplementation(...)` now forwards wrapper static metadata into `JavaJsHookRequest.is_static`
- Java callback receivers also preserve `$isStatic` so callback-side `callOriginal(...)` stays aligned with the installed hook record

Java bridge changes:

- JNI-style descriptor parsing now accepts `J` and `F`
- Android-side Java-to-JS conversion now maps:
  - `J` -> `kInt64`
  - `F` -> `kFloat`
- JS-to-Java conversion now accepts the practical numeric path for:
  - `long`
  - `float`
  - existing `double` / `int`

### Problems encountered

The first new red test was the static overload-wrapper case, failing during `registry.LoadScript(...)`.

Root cause:

- the overload resolver still hardcoded `is_static = false`
- so exact-signature lookup for a static target failed before install even started

Once that was fixed, the remaining design choice was how to expose static behavior without inventing more temporary API surface.

Chosen resolution:

- keep the user-facing shape unchanged
- make overload resolution automatically fall back from instance to static
- attach the resolved static/instance metadata to the selected wrapper

This is closer to Frida usage than adding a Nook-only temporary static selector.

### Verification

Extended `tests/communication/test_js_runtime_native_attach.cpp` with new host coverage for:

- `TextFragment.formatScaled.overload("long")`
- `TextFragment.formatScaled.overload("float")`
- `MainActivity.incrementIntercept.overload("int")` static exact-signature selection
- `long` overload callback `callOriginal(...)`
- `float` overload callback `callOriginal(...)`
- static overload install exact-signature metadata
- static overload callback `callOriginal(...)`

Locally verified:

- rebuilt `build/test_js_runtime_native_attach_task70.exe`
- `build/test_js_runtime_native_attach_task70.exe` passes cleanly after the implementation

### Current boundary

The current `long` path is intentionally practical rather than complete:

- it uses the existing JS number bridge
- it is suitable for common real-world values in current smoke usage
- it is not yet a full BigInt / lossless Int64 compatibility promise

The current static support is also intentionally narrow:

- exact-signature `overload(...)` install works
- callback-side `callOriginal(...)` works
- wildcard non-overload static install and broader Frida Java parity are still pending

### Device smoke follow-up

Added dedicated real-device static smoke scripts:

- [java_static_log_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_static_log_smoke.js)
- [java_static_app_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_static_app_smoke.js)

Stable baseline target:

- `android.util.Log.d(String, String)`

Demo-app-specific target:

- `MainActivity.incrementIntercept()`

The framework static target is now the preferred baseline because it proved on device that:

- exact-signature static overload resolution works
- static install works
- static `callOriginal(...)` wrapper shape resolves correctly

The demo-app private static target is still useful, but it is now treated as an app-alignment check instead of the baseline runtime proof.

Recommended validation command:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_static_log_smoke.js --wait --usb
```

Expected initial output:

- `java-static-log-wrapper:object:(Ljava/lang/String;Ljava/lang/String;)I:true:function`
- `java-static-log-installed:(Ljava/lang/String;Ljava/lang/String;)I:true`

Expected output after app interaction:

- `java-static-log-enter:<tag>:<message>`
- `java-static-log-leave-original:<int>`

Supporting host coverage was also extended for the real smoke shape:

- zero-argument static overload selection resolves `()V`
- zero-argument static callback `callOriginal()` returns cleanly through the JS bridge

### Real-device conclusion from this step

The earlier `ResolveJavaMethodSignature no method match` result was not enough to conclude that static support was broken.

After pushing a diagnostic build and testing the framework static target on device, the current evidence is more specific:

- `android.util.Log.d(String, String)` resolves successfully through the static exact-signature path
- the static wrapper installs successfully
- JavaHook logs confirm the target `ArtMethod` was patched successfully
- later real `Log.d(...)` executions still happen in the app process
- but there is still no `hook_handler` / callback-entry evidence for those static invocations

So the current boundary is:

- static exact-signature resolution works
- static install metadata flow works
- static callback dispatch on real device `invoke-static` callsites is still not solved

This is no longer an overload resolver problem. The stronger hypothesis now is that the current JavaHook strategy patches the callee `ArtMethod`, while compiled `invoke-static` callsites can continue using already-resolved direct entrypoints and therefore bypass the patched dispatch path.

In other words, the remaining gap is likely in lower ART/direct-call handling, not in the JS bridge.

## 2026-04-26 Java.deopt diagnostics and wider JIT scan

### Symptom

The latest device debug smoke already showed:

- `java-force-on:true:true:true`
- `java-force-off:true:false:true`

but still reported:

- `java-deopt:false:false`

So the blocker was no longer JavaHook initialization or the forced-interpret path. The remaining failure was specifically inside the best-effort JIT cache invalidation path.

### Root cause

`TryInvalidateJitCodeCache(...)` still used a very narrow `Runtime` scan window and only returned a boolean result. That left two practical problems:

- the scan could miss device-specific `Runtime -> Jit` placement
- when it failed, the JS side could not tell whether the miss came from:
  - missing `libart` symbols
  - missing `Runtime`
  - an empty runtime scan
  - candidate pointers being unreadable
  - no valid `JitCodeCache` being recovered

This made each device retest depend on native logcat instead of script-visible diagnostics.

### Fix

Kept `Java.deopt()` as best-effort, but made it much more observable:

- added `DeoptDiagnostics` in `JavaHook.h`
- cached the latest deopt attempt in native code
- widened the runtime scan window from the old small magic range to `0x100..0x1000`
- recorded:
  - `symbolsAvailable`
  - `runtimeAvailable`
  - `scanStart`
  - `scanEnd`
  - `candidatesSeen`
  - `readableCandidates`
  - `runtimeOffset`
  - `runtimeAddress`
  - `codeCacheAddress`
  - `reason`
- exposed these fields through `Java.deopt()` in `js_runtime.cpp`
- updated `host/nook-py/java_debug_smoke.js` to print the new compact diagnostics line

The old `ok` / `invalidated` fields were preserved so the previous smoke interpretation still works.

### Verification

Verified locally:

- `g++ -std=c++17 tests/headers/test_java_hook_runtime_regressions.cpp -o build/test_java_hook_runtime_regressions.exe`
- `build\\test_java_hook_runtime_regressions.exe`
- Android rebuild via `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd -B NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_MODULES="nook nook_agent" -j4`

### Current boundary

This change improves diagnosis and increases the odds of locating `JitCodeCache`, but it is still not full Frida-level ART deopt parity.

Current status:

- `Java._setForcedInterpretOnly(...)` is usable
- `Java.deopt()` now reports why it failed much more clearly
- if a device still reports a scan miss, the next step should be a more ART-structure-aware `Runtime/Jit` discovery path instead of more blind retries

## 2026-04-26 Java.deopt fully invalidates JIT on the test device

### Device evidence

After replacing the earlier unsafe `GetCodeCache()` guess path with an ART-structure-aware path, the device debug smoke now reports:

- `java-deopt:true:true:invalidated:true:true:256-4096:32:23:504`

This confirms that on the current test device:

- `libart` symbols are sufficient
- `Runtime` is available
- `TryInvalidateJitCodeCache(...)` now finds the real `jit_` object
- `JitCodeCache::InvalidateAllCompiledCode()` executes successfully

### Root cause of the earlier failures

The earlier implementation assumed:

- `Jit::GetCodeCache()` existed as a usable exported symbol
- any readable `Runtime` candidate that looked vaguely object-like could be treated as `Jit*`

Both assumptions were too weak for the real device.

What the device `libart.so` actually showed:

- `Jit::GetCodeCache()` was not available as a usable symbol
- the actual invalidation entrypoint was:
  - `_ZN3art3jit12JitCodeCache25InvalidateAllCompiledCodeEv`
- the `art::jit::Jit` vtable symbol was available:
  - `_ZTVN3art3jit3JitE`

### Fix

The current implementation now:

- resolves the `art::jit::Jit` vtable symbol from `libart.so`
- scans `Runtime` for a candidate whose first word matches the JIT vtable address-point
- treats `jit_ + 8` as the `JitCodeCache*` field on this device
- invokes `JitCodeCache::InvalidateAllCompiledCode()` directly

This stopped the previous crash path and also moved `Java.deopt()` from:

- safe diagnostics only

to:

- real successful JIT invalidation on the current hardware / ART build

### Implication for static Java hooks

`HookMethod(...)` already calls:

- `TrySetForcedInterpretOnly(...)`
- `TryInvalidateJitCodeCache(...)`

before installing the Java hook.

That means the static Java hook path is now naturally exercising the same deopt logic during install. The remaining static-hook validation question is no longer:

- whether `deopt()` itself works

but:

- whether the hook routing path actually starts receiving the expected static callsites once JIT invalidation is now real instead of best-effort-only

### Smoke-script support

To make the next real-device static validation tighter, both scripts now print the deopt status before installing the hook:

- `host/nook-py/java_static_log_smoke.js`
- `host/nook-py/java_static_app_smoke.js`

## 2026-04-26 Static Java hook now works on the real device

### Device result

Latest real-device validation now shows both the framework static target and the demo-app static target firing correctly after the JIT deopt path was fixed.

Framework static target:

- `java-static-log-deopt:true:true:invalidated:504`
- `java-static-log-installed:(Ljava/lang/String;Ljava/lang/String;)I:true`
- repeated callback hits such as:
  - `java-static-log-enter:TargetDemo:LoginFragment setup complete`
  - `java-static-log-leave-original:1`
  - `java-static-log-enter:LOGIN_TARGET:Attempting native AES password verification`
  - `java-static-log-leave-original:1`

Demo-app static target:

- `java-static-app-deopt:true:true:invalidated:504`
- `java-static-app-installed:()V:true`
- callback hit:
  - `java-static-app-enter`
  - `java-static-app-leave-original:undefined`

### Conclusion

This changes the earlier real-device conclusion materially.

The previous static-hook limitation was not just:

- exact-signature resolution
- wrapper metadata flow
- JS bridge dispatch

The real blocker was that direct/static callsites were still running against compiled/JIT-cached paths that bypassed the patched hook routing.

Now that:

- `TrySetForcedInterpretOnly(...)` works
- `TryInvalidateJitCodeCache(...)` performs real invalidation on this device

the static Java hook path is receiving the expected callsites on device.

### Updated boundary

Current real-device status is now:

- `Java.perform(...)` works
- instance Java hook dispatch works
- static Java hook dispatch works
- exact-signature overload selection works
- `callOriginal(...)` works for the validated static and instance smoke paths

So the Java hook work has moved past the earlier “installed but not firing” stage and into the next phase:

- expanding API coverage
- hardening ART compatibility
- reducing the amount of device-specific reverse engineering still embedded in the current deopt path

## 2026-04-26 Minimal Java field access added

### Scope

Added the first minimal Frida-style Java field surface:

- `Java.use("...").fieldName.value` for static fields
- `this.fieldName.value` inside an active Java hook callback receiver for instance fields

Current supported field types:

- `boolean`
- `int`
- `long`
- `float`
- `double`
- `java.lang.String`

### Host-side implementation

Bridge surface added in:

- `src/agent_runtime/nook_java_js_bridge.h`
- `src/agent_runtime/nook_java_js_bridge.cpp`

Runtime wrapper changes added in:

- `src/agent_runtime/js_runtime.cpp`

New host coverage added in:

- `tests/communication/test_js_runtime_native_attach.cpp`

New smoke script added in:

- `host/nook-py/java_field_smoke.js`

### Main implementation points

- introduced `JavaJsFieldRecord`
- extended Java bridge dependencies with:
  - `resolve_field`
  - `read_field`
  - `write_field`
- added bridge entrypoints:
  - `ResolveJavaField(...)`
  - `ReadJavaField(...)`
  - `WriteJavaField(...)`
- added JS runtime helpers:
  - `__nookJavaResolveField`
  - `__nookJavaReadField`
  - `__nookJavaWriteField`
- extended `Java.use(...)` proxy so unknown properties now:
  - first try lazy field resolution
  - fall back to the existing lazy method wrapper path if no field resolves
- changed Java callback receiver creation so `this` is now a class proxy with a bound receiver handle, which enables:
  - `this.methodName.callOriginal(...)`
  - `this.someField.value`

### Problem encountered

The first green compile still failed one runtime assertion in host tests:

- writing `MainActivity.interceptCount.value = 42` reached the fake field backend as `kDouble`
- the test expected the bridge to normalize by field signature and store `kInt32`

### Fix

Added field-signature-based normalization in `WriteJavaField(...)` before delegating to either:

- fake test dependencies
- or the default Android bridge

This keeps the contract stable regardless of whether the backend is:

- the host fake store
- or the real JNI field writer

### Verification

Host verification completed:

- rebuilt `build/test_js_runtime_native_attach_task_fields.exe`
- `build/test_js_runtime_native_attach_task_fields.exe` passes cleanly
- rebuilt `build/test_java_js_bridge_task_fields.exe`
- `build/test_java_js_bridge_task_fields.exe` passes cleanly

### Current boundary

The current field API is intentionally narrow:

- static field access is general within the supported scalar/String subset
- instance field access is currently validated only inside the active Java hook callback receiver
- broader Frida-style object wrappers, constructor support, and external instance-handle APIs are still pending

## 2026-04-26 Spawn timeout after reboot: root cause was agent `dlopen` failure

### Symptom

After reboot, `nook-cli spawn ...` still timed out and server logs showed:

- `spawn failed pkg=com.demo.target agent=/data/local/tmp/nook/libnook-agent.so error=spawn callback timeout or failed`

At first glance this looked like a callback-file wait problem in:

- `server/ninjector_spawn_injector.cpp`
- `server/ninjector_compat.cpp`

### Root cause evidence

Direct device logs showed the real failure happened earlier in the zygote child:

- `ncore: target matched, loading payload package=com.demo.target so=/data/local/tmp/nook/libnook-agent.so`
- `ncore: dlopen failed for /data/local/tmp/nook/libnook-agent.so: dlopen failed: cannot locate symbol "__emutls_get_address"`

That means:

- `spawn_result.json` was never written
- the server timeout was only a downstream symptom
- fixing wait logic alone would not solve this class of failure

### Actual cause

Two runtime files were still using `thread_local` state that forced compiler-emulated TLS into the shared objects:

- `src/java_hook/JavaHook.cpp`
- `src/agent_runtime/nook_java_js_bridge.cpp`

That produced an `__emutls_get_address` dependency in:

- `libs/arm64-v8a/libnook-agent.so`
- `libs/arm64-v8a/libnook.so`

Attach could still work in later process state, but early spawn-time payload loading through `ncore` failed when this symbol could not be resolved.

### Fix

Replaced both `thread_local` states with explicit per-thread maps keyed by `gettid()` and protected by `std::mutex`:

- Java `callOriginal(...)` bypass depth stays thread-scoped
- active Java JS callback invocation state also stays thread-scoped
- but neither path now emits emulated TLS dependencies into the shared objects

### Verification

TDD-style regression flow:

- first changed `tests/headers/test_java_hook_runtime_regressions.cpp` to require the new non-`thread_local` shape
- ran it and confirmed failure
- implemented the runtime change
- reran the test and confirmed pass

Android verification:

- rebuilt with:
  `ndk-build -B NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_MODULES="nook nook_agent" -j4`
- checked both rebuilt shared objects with `llvm-readelf`
- confirmed neither `libnook-agent.so` nor `libnook.so` now contains:
  - `__emutls_get_address`
  - `__emutls_v.*`

Deployment completed:

- pushed `/data/local/tmp/nook/libnook.so`
- pushed `/data/local/tmp/nook/libnook-agent.so`

### Expected outcome

The previous spawn failure mode should now be gone:

- payload `dlopen` in the zygote child should no longer fail on `__emutls_get_address`
- if spawn still fails, it is now a different stage and should be debugged from fresh logs instead of the old timeout symptom

## 2026-04-26 REPL spawn `%resume` ordering: pre-resume script load is incompatible with current SIGSTOP model

An attempted CLI change reordered `repl spawn ... -l hook.js` so `%resume` would:

1. create/load deferred script
2. then send `ResumeRequest`

This was intended to catch startup-early Java callbacks before app code resumed, but real-device testing showed it times out consistently.

### Root cause

Current spawn suspension is server-side `SIGSTOP` on the whole target process.

That means while the process is suspended:

- the agent session is connected
- but the agent cannot run its recv loop
- so it cannot answer `SCRIPT_CREATE` / `SCRIPT_LOAD`

Device logs confirmed the exact failure shape:

- server forwarded `SCRIPT_CREATE`
- no matching `SCRIPT_CREATE_RESP` came back until the process was resumed
- host eventually timed out with `error: operation timed out`

So with the current architecture, "load script before resume" is not just buggy, it is structurally incompatible with whole-process `SIGSTOP`.

### Resolution

The CLI change was reverted.

Current valid behavior remains:

- `repl spawn ... -l hook.js`
- `%resume`
- server sends `ResumeRequest`
- target process continues
- deferred script is then created and loaded

### Architectural implication

If Nook eventually wants Frida-like "script is active before the app proceeds" semantics, the spawn model must evolve away from whole-process `SIGSTOP`, for example toward:

- cooperative agent-side gating
- or selective thread suspension / interception

That is a future architecture task, not a CLI ordering bug.

## 2026-04-26 Spawn gate phase-1 implementation progress

This pass moved Nook one step closer to the intended Frida-like spawn flow while keeping the phase-1 host protocol shape unchanged.

### Completed in this pass

- injector-side spawn result semantics were changed from "callback pid" wording to "gate-held child pid"
- server-side spawn handling no longer applies an extra coarse `SIGSTOP` after spawn success
- `resume` semantics in server handlers now mean "release spawn gate"
- injector spawn now creates a per-process spawn marker before app start
- spawn marker is kept on successful gated spawn and removed on gate callback failure
- Android `NookComm` now consumes the spawn marker, sends `AgentReady`, installs a `ResumeRequest` handler, and blocks in `NookCommWaitForResumeIfSpawned()` until release
- Android server main path now releases the target by sending `ResumeRequest` to the agent session instead of `SIGCONT`

### Why these changes were needed

The previous architecture still depended on whole-process suspension:

- host could receive `SpawnResponse`
- agent could later send `AgentReady`
- but any attempt to make pre-resume script loading real would deadlock or time out under coarse stop/resume semantics

This pass introduces the minimum cross-layer plumbing needed for a real cooperative gate:

- injector leaves a spawn marker
- target process detects it and arms a wait
- server later releases that wait over the existing control channel

### Local verification

Passed:

- `build\test_ninjector_spawn_injector.exe`
- `build\test_server_handlers.exe`
- `g++ -std=c++17 -I . -I include -I src tests/headers/test_public_headers.cpp -c -o build/test_public_headers.o`
- `g++ -std=c++17 -I . -I include -I src -c src/framework/NookComm.cpp -o build/NookComm.o`
- `g++ -std=c++17 -I . -I include -I src -c server/server_main.cpp -o build/server_main.o`

Android arm64 rebuild passed with:

- `E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk APP_ABI=arm64-v8a -j4`

Produced:

- `build\android\libs\arm64-v8a\nook-server`
- `build\android\libs\arm64-v8a\libnook-agent.so`

### Remaining gaps

- device smoke is still needed to confirm the new gate actually enables "load before release" flow end-to-end
- broader doc cleanup outside the Python README is still pending

## 2026-04-26 Host and CLI spawn ordering updated for gate-held children

Follow-up work completed after the lower spawn gate landed:

- `HostSpawnClient` comments now explicitly document that `SpawnResponse.pid` is a gate-held child and `Resume()` means gate release
- Python CLI ordering was updated so `spawn -l --resume` now loads the script before calling `resume`
- `repl spawn -l hook.js` now loads the startup script immediately while the gate is still held
- manual `%load` / `%reload` are now allowed in suspended `repl spawn` sessions
- helper flows like `call --spawn --resume`, `post`, and `unload` were aligned to the same `spawn -> load -> resume` ordering

### Verification

Passed:

- `build\test_host_spawn_client.exe`
- `python -m unittest host.nook-py.tests.test_cli`

README updates were also applied to match the new ordering.

## 2026-04-26 Spawn bound to zygote64 root-cause and mitigation

Observed device logs showed the failure mode clearly:

- `spawn success` returned a pid whose later `AgentReady` still reported `name=zygote64`
- `script.create` and `script.load` were then routed into the zygote-side agent session instead of the real app child

Root cause identified in this pass:

- `libnook-agent.so` was already being loaded into zygote during the spawn preparation path
- the agent constructors eagerly initialized communication inside that early process
- this caused an early `AgentReady(pid=zygote_pid, process_name=zygote64)` to be cached and replayed to the host
- when the same library image was inherited by the real child, there was no explicit child-side re-initialization point
- attach also had a latent issue after any zygote preload, because a later `dlopen()` on an already loaded agent image does not rerun constructors

Mitigation implemented:

- added a small init policy layer so early process names such as `zygote64` and `usap64` no longer eagerly initialize the agent
- added exported `NookAgentInitialize()` as an explicit init entry that performs the real comm + bridge initialization
- changed `NookComm` constructor behavior to register a post-fork child handler and defer spawned-child activation until the specialized process name matches a pending spawn marker
- removed bridge-side eager constructor init so zygote preload no longer starts the script runtime bridge too early
- changed `InjectSoByPid(...)` to call remote `NookAgentInitialize()` after `dlopen()`, so attach still works even if the library image was preloaded earlier without running full agent init

Local verification added in this pass:

- new policy test: `build\test_nook_agent_init_policy.exe`
- compile checks still pass for:
  - `g++ -std=c++17 -I . -I include -I src tests/headers/test_public_headers.cpp -c -o build/test_public_headers.o`
  - `g++ -std=c++17 -I . -I include -I src -c src/framework/NookComm.cpp -o build/NookComm.o`
  - `g++ -std=c++17 -I . -I include -I src -c server/ninjector_compat.cpp -o build/ninjector_compat.o`

Remaining device verification needed:

- rebuild arm64 artifacts
- push `nook-server`, `libnook-agent.so`, and `libnook.so`
- rerun `spawn` and `repl spawn` smoke to confirm the host now binds to the real app child instead of `zygote64`

## 2026-04-26 Spawn gate callback timeout root-cause and fix

Observed regression after adding target-side wait:

- `nook-cli spawn ... --resume --wait --usb` timed out before returning `spawn response ok`
- device log showed `NookCommApi: waiting for spawn gate release pid=...`
- server log showed `spawn failed ... error=spawn gate callback timeout or failed`
- the same run also showed `agent ready without bound host`, proving the agent had connected but the server still lacked the authoritative spawn callback pid

Root cause:

- `NookAgentInitialize()` was changed to block in `NookCommWaitForResumeIfSpawned()`
- however, the existing Ninjector spawn path still treats `/data/local/tmp/Ninjector/spawn_result.json` as the signal that spawn setup completed successfully
- that callback file is only considered after the target-side initialization path returns
- so waiting too early deadlocked the control flow: the target held the gate correctly, but the server never got the pid needed to finish `Spawn()` and bind the host session

Fix implemented in this pass:

- kept the real cooperative wait in `NookAgentInitialize()`
- added an explicit target-side spawn callback report step after `NookScriptRuntimeBridgeInitialize()` and before `NookCommWaitForResumeIfSpawned()`
- the agent now writes `{"pid":<child_pid>}` to `/data/local/tmp/Ninjector/spawn_result.json` exactly once for armed spawn-gate children
- this preserves the important ordering:
  - server gets the real child pid and returns `SpawnResponse`
  - host binds the session and sends `script create/load`
  - target already has bridge handlers installed
  - only then does `resume` release the gate

Why this placement matters:

- writing the callback earlier inside `NookCommInitialize()` would reopen a race where the host may send `script create/load` before the bridge handlers exist
- writing it after bridge init but before wait gives the server enough information without dropping control-channel messages

Verification completed locally:

- source-order check confirms `bridge init -> spawn callback report -> wait`
- Android arm64 rebuild passed with:
  - `E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk APP_ABI=arm64-v8a -j4`

Device verification still needed:

- push updated `nook-server`, `libnook-agent.so`, and `libnook.so`
- rerun:
  - `nook-cli spawn com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_field_smoke.js --resume --wait --usb`
- expected change:
  - no `operation timed out`
  - `spawn response ok` should return first
  - first cold spawn should already be able to hit `java-field-instance:*` logs without needing an extra page switch

### Device verification result

Verified on device after moving the effective gate to the bootstrap Java hook:

- cold `spawn --resume --wait` no longer times out
- `java_field_smoke.js` now loads during the first spawn session
- first launch already produces:
  - `java-field-bootstrap-installed`
  - `java-field-deopt:true:true:invalidated:504`
  - `java-field-static:object:I:true:0:10:0`
  - `java-field-init-installed:(Landroid/view/View;)V:false`
  - `java-field-installed:(Ljava/lang/String;Ljava/lang/String;)V:false`
  - `java-field-ready:initial`
  - `java-field-instance:I:false:0:1:native:list_item_1`
  - `java-field-instance-after-original:2`
  - `java-field-instance:I:false:2:3:native:list_item_2`
  - `java-field-instance-after-original:4`
  - `java-field-instance:I:false:4:5:native:list_item_3`
  - `java-field-instance-after-original:6`

Conclusion:

- the previous main-thread spawn wait was too early and violated Android app startup timing
- reporting the spawn callback before release is still required
- the actual blocking point must live later in app bootstrap, where the process is already attached but application code has not yet run past the chosen lifecycle hook

## 2026-04-26 Java.ready object bridge and ClassLoader gate

Goal in this pass:

- move Nook one step closer to Frida-style Android usage
- specifically support `Java.ready(function () { ... })` during spawn-time script load
- keep the change narrow enough to avoid destabilizing existing Java hook flows

Root cause:

- Nook's Java JS bridge only converted Java callback values into JS scalars plus `String`
- Frida-style `Java.ready()` on Android relies on hooking `android.app.Instrumentation.newApplication(ClassLoader, String, Context)`
- that hook needs the first argument `ClassLoader jobject` to cross into JS so the runtime can cache it and make later `Java.use(app class)` resolution stable
- without a generic object bridge, Nook could not port this flow cleanly

Fix implemented:

- added `JavaJsValueKind::kObject`
- Java callback arguments and return values now support non-`String` object wrappers:
  - native -> JS stores raw `jobject` handle plus runtime class name
  - JS -> native accepts wrapper objects back for `callOriginal(...)` and hook return values
- `Java.use(...)` wrappers now also expose `__jptr` for closer Frida-style ergonomics
- added explicit app `ClassLoader` cache helpers in `java_hook_loader_resolver.*`:
  - `UpdateApplicationClassLoader(JNIEnv*, jobject)`
  - `IsApplicationClassLoaderReady(JNIEnv*)`
  - `GetApplicationClassLoader()` now prefers the cached global ref and backfills it from `currentApplication()` when available
- exported two internal JS helpers on `Java`:
  - `Java._updateClassLoader(...)`
  - `Java._isClassLoaderReady()`
- installed a bootstrap JS shim in `js_runtime.cpp`:
  - defines `Java.ready = function (fn) { ... }`
  - if the app `ClassLoader` is already ready, callback runs immediately
  - otherwise Nook hooks `Instrumentation.newApplication(...)`
  - the hook updates the cached `ClassLoader`, drains queued callbacks, then calls the original method

Important boundary:

- this object bridge is intentionally minimal
- it is sufficient for synchronous hook callbacks and for immediately promoting callback objects into native-side global refs
- it does not yet claim that arbitrary Java object wrappers can be stored in JS forever and safely reused later without an explicit native retention policy

Regression coverage:

- added `tests/headers/test_java_ready_object_bridge.cpp`
- verified red -> green locally:
  - `g++ -std=c++17 tests\headers\test_java_ready_object_bridge.cpp -o build\test_java_ready_object_bridge.exe`
  - `build\test_java_ready_object_bridge.exe`
  - exit code `0`

Build verification:

- Android arm64 rebuild passed with:
  - `E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk APP_ABI=arm64-v8a -j4`

Added smoke script:

- `host/nook-py/java_ready_smoke.js`
- expected attach/spawn signal shape:
  - `java-ready-bindings:object:function:function:function`
  - `java-ready-fired:true:com.demo.target.LoginFragment:function`

## 2026-04-26 Java callable method wrappers

Goal in this pass:

- move Java method wrappers from "metadata objects" closer to Frida's callable method wrappers
- make direct wrapper invocation work before broadening the Java API further

Root cause:

- after `Java.ready()` was working, `typeof LoginFragment.verifyPasswordNative` still returned `object`
- this meant Nook could install hooks and use `callOriginal(...)`, but could not yet support Frida-style direct invocation like:
  - `this.verifyPasswordNative(password)`
  - `SomeClass.someStaticMethod(...)`

Fix implemented:

- method wrappers produced by `CreateJavaUseWrapper(...)` are now function objects
- wrapper metadata remains attached to the function object:
  - `$className`
  - `$methodName`
  - `$signature`
  - `$isStatic`
  - `__nookJavaReceiverHandle`
- existing helper APIs stay available:
  - `.overload(...)`
  - `.implementation`
  - `.callOriginal(...)`
- added a new minimal native bridge:
  - `InvokeJavaMethod(...)`
  - test dependency injection through `JavaJsHookInstallerDependencies::invoke_method`
  - default Android implementation resolves method id from exact signature and dispatches through JNI
- direct JS method invocation now routes through `__nookJavaInvoke(...)`

Important boundary:

- this pass is intentionally a minimal invoke bridge, not a full Frida Java object model
- the direct invoke path is now good enough for:
  - static methods
  - instance methods where a valid receiver handle already exists
  - overload-selected wrappers with exact signatures
- broader Frida parity such as constructors, `$new`, and richer object lifetime control remains separate work

Regression verification completed locally:

- rebuilt and passed:
  - `build\test_js_runtime_native_attach.exe`
- retained previous Java.ready regression coverage:
  - `build\test_java_ready_object_bridge.exe`

New smoke script:

- `host/nook-py/java_callable_smoke.js`
- expected signal shape on device:
  - `java-callable-wrapper:function:(Ljava/lang/String;Ljava/lang/String;)I:true:function`
  - `java-callable-result:<int>`

## 2026-04-26 Java numeric overload inference follow-up

Goal in this pass:

- make direct Java method invocation behave more like Frida when the JS argument is a plain `number`
- specifically fix the bad case where `10.0` was being forced into `int` inference before overload resolution

Root cause:

- `JsJavaInvoke(...)` previously collapsed an integral-looking JS `number` into a single inferred Java type name:
  - `int` when `floor(number) === number` and it fit in int32
  - otherwise `double`
- that made `this.formatBalance(10.0)` fail or resolve incorrectly even though the real Java overload set only exposed `double`
- class-wrapper direct calls without explicit `.overload(...)` also defaulted to instance-method resolution, so static direct invoke could not fall back to static lookup

Fix implemented:

- changed direct invoke overload inference from a single guessed type to an ordered candidate list
- current numeric fallback policy:
  - JS `double` / plain `number`: try `double` first
  - integral numbers then fall back to `int`, `long`, and finally `float`
  - `int32` values fall back through `int -> long -> float -> double`
  - `int64` values fall back through `long -> double -> float`
  - `float` values fall back through `float -> double`
- added a second resolve pass for class-wrapper direct invoke:
  - if receiver handle is `0` and instance lookup fails, retry as static

Why this boundary was chosen:

- changing all JS numbers to always mean `double` would fix `10.0`, but it would break int-only direct calls like `MainActivity.incrementIntercept(41)`
- adding candidate-based resolution keeps the fix narrow and preserves existing direct-call behavior
- this is still a minimal heuristic, not full Frida-style score-based overload selection across the whole reflected overload set

Regression coverage added:

- `tests/communication/test_js_runtime_native_attach.cpp`
  - `TestJavaStaticMethodWrapperCanResolveIntOverloadFromPlainNumberDirectly()`
  - `TestJavaInstanceMethodWrapperCanResolveDoubleOverloadFromIntegralNumberDirectlyInsideCallbackReceiver()`

Verification completed locally:

- rebuilt:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- passed:
  - `build\test_js_runtime_native_attach.exe`

New smoke script:

- `host/nook-py/java_numeric_overload_smoke.js`
- expected signal shape on device:
  - `java-numeric-overload-wrapper:function:(Landroid/view/View;)V:false`
  - `java-numeric-overload-installed`
  - `java-numeric-overload-result:BAL 10.00:BAL 10.50`

Additional regression coverage added after device validation:

- `TestJavaInstanceMethodWrapperCanResolveLongOverloadFromPlainIntDirectlyInsideCallbackReceiver()`
- `TestJavaInstanceMethodWrapperCanResolveFloatOverloadFromPlainDoubleDirectlyInsideCallbackReceiver()`

What this confirms:

- plain JS integer arguments can fall through to `long` direct invoke when no `int` overload exists
- plain JS fractional `number` arguments can fall through to `float` direct invoke when no `double` overload exists
- no further runtime change was required after the candidate-based resolver landed; the extra work here was test coverage and real-device smoke expansion

Current validation boundary:

- the current Android demo app does not expose a real `long` / `float` overloaded Java method for on-device smoke validation
- these two paths are therefore covered by desktop runtime regression tests only for now
- if we want true device-side validation later, we should first add a minimal overloaded Java method to the demo target app

## 2026-04-26 Java.cast minimal support

Goal in this pass:

- add a minimal Frida-style `Java.cast(objectWrapper, classWrapper)` API
- keep it small by reusing the existing Java wrapper factory instead of introducing a larger object model

Scope implemented:

- first arg must be a Java object wrapper with a non-zero receiver handle
- second arg must be a `Java.use(...)` class wrapper
- success returns a new wrapper that:
  - preserves the original Java object handle
  - switches `$className` to the target class wrapper's class
  - resolves subsequent methods and fields through the target class view

Explicitly not implemented in this pass:

- JNI `instanceof` or subclass checks
- object lifetime changes such as `retain()`
- `choose()`
- implicit `null` pass-through cast behavior

Desktop regression coverage added:

- `TestJavaCastBindingExists()`
- `TestJavaCastReturnsRewrappedObjectWithNewClassName()`
- `TestJavaCastWrapperCanInvokeTargetClassMethodDirectly()`
- `TestJavaCastRejectsNonJavaObject()`
- `TestJavaCastRejectsNonClassWrapper()`

Local verification completed:

- rebuilt:
  - `build\test_js_runtime_native_attach.exe`
- passed:
  - `build\test_js_runtime_native_attach.exe`

Device smoke script:

- `host/nook-py/java_cast_smoke.js`
- expected key output:
  - `java-cast-bindings:function:2026-04-26-numcand-v2`
  - `java-cast-installed`
  - `java-cast-result:com.demo.target.TextFragment:true:BAL 10.00`

Current boundary:

- this smoke validates wrapper re-casting and direct method invocation on device
- it does not validate hierarchy safety, because this pass deliberately skips JNI type checking

## 2026-04-26 Java.retain minimal support

Goal in this pass:

- add a minimal Frida-style `Java.retain(objectWrapper)` API
- make callback-scoped Java object wrappers promotable to Android global references for later reuse

Scope implemented:

- `Java.retain(obj)` requires a Java object wrapper with a non-zero receiver handle
- success creates an Android-side `NewGlobalRef(...)`
- success returns a new wrapper that:
  - preserves the original `$className`
  - uses the retained global-ref handle
  - can be passed into `Java.cast(...)` and reused for later method calls

Explicitly not implemented in this pass:

- `Java.release(...)`
- automatic retained-object cleanup
- retained object registry / GC policy
- non-Android runtime support beyond a clear failure

Desktop regression coverage added:

- `TestJavaRetainBindingExists()`
- `TestJavaRetainReturnsRewrappedObjectWithRetainedHandle()`
- `TestJavaRetainRejectsNonJavaObject()`
- `TestJavaRetainRejectsNullHandleObject()`

Local verification completed:

- rebuilt:
  - `build\test_js_runtime_native_attach.exe`
- passed:
  - `build\test_js_runtime_native_attach.exe`

Device smoke script:

- `host/nook-py/java_retain_smoke.js`
- expected key output:
  - `java-retain-bindings:function:2026-04-26-numcand-v2`
  - `java-retain-installed`
  - `java-retain-result:com.demo.target.TextFragment:true:BAL 10.00`

Current boundary:

- this pass proves that a callback receiver can be retained, recast, and invoked again on device
- it does not yet provide any way to release retained global references, so leak control remains a follow-up task

## 2026-04-26 Android artifact path mismatch root cause

Observed contradiction:

- local desktop verification already passed against the latest `src/agent_runtime/js_runtime.cpp`
- device-side Java numeric overload smoke still reported the old direct-invoke behavior:
  - `args=[int]`
  - no `_invokeResolverVersion`

Root cause confirmed:

- the active Android NDK build command was installing fresh outputs into:
  - `libs/arm64-v8a/...`
- an older stale artifact set was still present under:
  - `build/android/libs/arm64-v8a/...`
- the earlier device push accidentally used the stale `build/android/libs/...` binaries instead of the real fresh `libs/...` outputs

Evidence:

- fresh root artifact contains the new runtime markers:
  - `_invokeResolverVersion`
  - `2026-04-26-numcand-v2`
  - `Java invoke overload resolution failed`
- stale `build/android/libs/arm64-v8a/libnook-agent.so` does not contain those markers
- `build/ndk_verbose.log` shows the actual install target:
  - `Install : libnook-agent.so => libs/arm64-v8a/libnook-agent.so`
  - `Install : libnook.so => libs/arm64-v8a/libnook.so`
  - `Install : nook-server => libs/arm64-v8a/nook-server`

Correct deployment source:

- always push from `libs/arm64-v8a/`, not `build/android/libs/arm64-v8a/`

Validated hashes for the fresh deploy set:

- local `libs/arm64-v8a/libnook-agent.so`
  - `b93268b87adbb3e77c42e2f6e624e893`
- local `libs/arm64-v8a/libnook.so`
  - `e3a6396cb9141277a5ad413c3c8b261a`
- local `libs/arm64-v8a/nook-server`
  - `11dcfd5946a283bfaa16dfa556ca94f5`

Device push validation after correction:

- `/data/local/tmp/nook/libnook-agent.so`
  - `b93268b87adbb3e77c42e2f6e624e893`
- `/data/local/tmp/nook/libnook.so`
  - `e3a6396cb9141277a5ad413c3c8b261a`
- `/data/local/tmp/nook/nook-server`
  - `11dcfd5946a283bfaa16dfa556ca94f5`

Operational rule going forward:

- after any Android rebuild, validate against `libs/arm64-v8a/...`
- if runtime behavior looks stale, first compare strings or hashes between:
  - `libs/arm64-v8a/...`
  - `/data/local/tmp/nook/...`

## 2026-04-26 Java.choose minimal Android backend

Goal in this pass:

- add a minimal Frida-style `Java.choose(className, callbacks)` path that actually works on Android
- keep the JS API shape stable while using the narrowest viable backend for the first live-object enumeration pass

Root cause:

- the JS runtime binding and desktop fake dependency path for `Java.choose(...)` were already present and green
- Android still failed because `EnumerateJavaObjects(...)` was only a stub returning:
  - `java object enumerator is not configured`

Implementation:

- added `DefaultEnumerateJavaObjects(...)` in `src/agent_runtime/nook_java_js_bridge.cpp`
- the Android default path now:
  - resolves the target class through `JavaHook::FindClass(...)`
  - resolves `dalvik.system.VMDebug`
  - calls hidden VM API `getInstancesOfClasses([Class], true)`
  - wraps each returned instance as a `JavaJsValueKind::kObject`
  - preserves the actual runtime class name via `DescribeJavaObject(...)`
- `EnumerateJavaObjects(...)` now uses that Android default when no injected dependency override is provided

Lifetime handling:

- `Java.choose(...)` now promotes each enumerated instance to a temporary global ref before handing it to JS
- `JsJavaChoose(...)` releases those temporary global refs immediately after dispatching `onMatch` / `onComplete`
- this keeps repeated choose calls from leaking permanent global references during smoke testing

Regression verification:

- rebuilt:
  - `build\test_js_runtime_native_attach.exe`
- passed:
  - `build\test_js_runtime_native_attach.exe`

Device smoke script:

- `host/nook-py/java_choose_smoke.js`
- expected key output:
  - `java-choose-bindings:function:2026-04-26-numcand-v2`
  - `java-choose-installed`
  - one or more:
    - `java-choose-match:com.demo.target.TextFragment:BAL 10.00`
  - `java-choose-complete`

Current boundary:

- the JS API is now Frida-like, but the Android backend is intentionally not Frida's ART heap walker yet
- this pass uses the hidden VM helper path first because it is much smaller and safer to land than a full ART object visitor
- enumerated wrappers are only guaranteed to be valid inside `onMatch`
- if a script wants to keep an object for later use, it should still call `Java.retain(instance)` inside `onMatch`

## 2026-04-26 Java.enumerateLoadedClasses ART backend

Goal in this pass:

- add a Frida-style `Java.enumerateLoadedClasses({ onMatch, onComplete })`
- keep the JS API thin and consistent with the rest of the Java bridge
- make the Android backend use an ART-side loaded-class route instead of the earlier app-loader approximation

JS/runtime side completed:

- `Java.enumerateLoadedClasses` is now exported from the QuickJS runtime
- argument validation is in place for:
  - missing callbacks
  - non-object callbacks
  - missing `onMatch`
  - missing `onComplete`
- native results are de-duplicated before JS callback dispatch
- desktop regression tests were added for binding shape, validation, and callback order

First Android attempt and failure:

- the first Android backend used `art::gc::Heap::VisitObjects(...)`
- during heap traversal, the bridge tried to:
  - walk all heap objects
  - create JNI local refs from raw `mirror::Object*`
  - keep only objects whose runtime type was `java.lang.Class`
- this crashed on device with `SIGSEGV` inside the ART path

Root cause:

- `Heap::VisitObjects(...)` was the wrong boundary for this feature
- even if the symbol exists, this path is too fragile for a Frida-style class enumeration backend because:
  - it traverses all heap objects instead of the class registry
  - raw heap object access is sensitive to ART thread state and GC movement
  - the callback/signature contract is more device-version fragile than the class-linker route

Resolution applied:

- replaced the Android backend in [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp) so it now prefers:
  - `art::ClassLinker::VisitClasses(...)`
- wrapped enumeration in an ART suspend-all section before touching class records
- changed the visitor to only convert `mirror::Class*` into JNI refs, then derive the dot-style class name through the existing `DescribeJavaClassObject(...)` helper
- if class-linker traversal helpers are unavailable, the backend now fails with a controlled error instead of continuing into an unsafe heap walk

Why this is closer to Frida:

- Frida's Android loaded-class enumeration is class-registry oriented, not a generic heap-object sweep
- switching to the class-linker route makes the Nook backend match that architectural direction much more closely
- this also narrows the surface area where ART version differences can crash the process

Device verification result:

- real-device smoke with:
  - `host/nook-py/java_enumerate_loaded_classes_smoke.js`
- now completes successfully and emits app classes such as:
  - `com.demo.target.ButtonFragment`
  - `com.demo.target.TextFragment`
  - `com.demo.target.LoginFragment`
  - `com.demo.target.AdWallFragment`
- `java-enum-classes-complete` is emitted at the end
- the previous app crash no longer reproduces on this smoke path

Current boundary:

- this pass validates Frida-style loaded-class enumeration at the JS API level and through an ART-side class-linker backend
- it does not yet add:
  - `Java.enumerateLoadedClassesSync()`
  - `Java.enumerateClassLoaders()`
  - class-loader metadata on each class result
- if we continue the Frida-compatibility track, the next most natural follow-up is `Java.enumerateClassLoaders()`

## 2026-04-26 Java.enumerateClassLoaders minimal support

Goal in this pass:

- add a minimal Frida-style `Java.enumerateClassLoaders({ onMatch, onComplete })`
- make the result usable as real Java object wrappers instead of ad-hoc metadata
- keep the first Android backend on the stable JNI side instead of introducing another fresh ART raw walker

Root cause for the chosen design:

- after `Java.enumerateLoadedClasses(...)`, the next missing Frida workflow piece was class-loader discovery
- the tempting alternative was to immediately build an ART-side loader walker
- that would have repeated the same risk class we had just seen with the first crashing heap-object enumeration attempt

Resolution applied:

- added `EnumerateJavaClassLoaders(...)` to the native bridge dependency surface
- added `Java.enumerateClassLoaders(...)` to the QuickJS runtime
- added host regression coverage for:
  - binding existence
  - callback validation
  - de-duplication of repeated native loader results
  - `onComplete()` dispatch
- implemented the Android default backend in [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp) using a JNI-side stable loader set:
  - cached application class loader
  - `Thread.currentThread().getContextClassLoader()`
  - `ClassLoader.getSystemClassLoader()`
  - each discovered loader's `getParent()` chain

How loader results are represented:

- each discovered loader is promoted to an Android global ref
- each result is returned as:
  - `JavaJsValueKind::kObject`
  - `object_handle = <global ref>`
  - `object_class_name = <runtime loader class>`
- JS receives normal Java wrappers, so later work can reuse these objects directly for loader-scoped APIs

Why this route was chosen over a new ART-side loader walker:

- it is much less crash-prone on first landing
- it is enough for the next practical Frida-like step:
  - `Java.ClassFactory.get(loader)`
- it keeps the class-loader feature moving forward without reopening raw ART object traversal risk immediately

Validation completed:

- desktop regression rebuilt and passed:
  - `build\test_js_runtime_native_attach.exe`
- Android arm64 rebuilt and pushed from:
  - `libs/arm64-v8a/libnook-agent.so`
  - `libs/arm64-v8a/libnook.so`
  - `libs/arm64-v8a/nook-server`
- new smoke script added:
  - `host/nook-py/java_enumerate_class_loaders_smoke.js`

Current boundary:

- this is intentionally a minimal reachable-loader enumeration, not a full loader census of every possible custom loader in the process
- returned loader wrappers currently hold global refs, and this pass does not yet add a release API for them
- the natural next feature is:
  - `Java.ClassFactory.get(loader)`

## 2026-04-26 Java.ClassFactory.get(loader) minimal support

Goal in this pass:

- add Frida-style `Java.ClassFactory.get(loader)`
- make `factory.use(className)` return loader-scoped wrappers instead of mutating global loader state
- keep existing `Java.use(className)` behavior unchanged

Root cause:

- after `Java.enumerateClassLoaders(...)` returned real loader wrappers, the missing piece was a way to consume them without introducing a Nook-specific global loader switch
- the bridge was still keyed only by class name, so even a JS-side factory object would have silently fallen back to default-loader resolution

Resolution applied:

- added loader-handle plumbing through the Java bridge records:
  - `JavaJsHookRequest`
  - `JavaJsHookRecord`
  - `JavaJsFieldRecord`
  - `JavaJsMethodRecord`
- updated JS wrapper generation in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp) so loader-scoped wrappers now carry:
  - `__nookJavaLoaderHandle`
- forwarded that handle into:
  - overload signature resolution
  - field resolution
  - direct invocation
  - `.implementation = fn` hook installation requests
- added a minimal Frida-style public surface:
  - `Java.ClassFactory.get(loader)`
  - `factory.use(className)`
- added a private helper bridge only for wrapper construction:
  - `__nookJavaUseWithLoader(className, loaderHandle)`

Why this is closer to Frida:

- it follows the `enumerateClassLoaders() -> ClassFactory.get(loader) -> factory.use()` flow instead of adding a Nook-only `Java.use(className, loader)` extension
- it avoids hidden global state and keeps default `Java.use(...)` semantics stable

Validation completed:

- host regression rebuilt and passed:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `build\\test_js_runtime_native_attach.exe`
- new host tests cover:
  - binding existence
  - rejecting non-loader objects
  - returned factory shape
  - forwarding loader handle to overload resolution and invocation
  - forwarding loader handle into implementation-install requests
- added device smoke script:
  - [java_class_factory_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_class_factory_smoke.js)

Current boundary:

- this pass adds only:
  - `Java.ClassFactory.get(loader)`
  - `factory.use(className)`
- it does not yet add:
  - `factory.$new(...)`
  - `factory.choose(...)`
  - `factory.cast(...)`
  - `factory.retain(...)`
  - `Java.classFactory.loader`
  - `Java.setClassLoader(loader)`

## 2026-04-26 Loader-aware public Java hook API

Goal in this pass:

- make loader identity survive all the way through native Java hook installation
- expose reusable public C APIs instead of keeping loader-awareness trapped inside the JS bridge
- remove the remaining semantic gap where `Java.ClassFactory.get(loader).use(...).implementation` still installed through the default loader path

Root cause:

- `JavaJsHookRequest.loader_handle` was already present, but the native install path dropped it before deferred installation:
  - `DefaultInstallJavaJsHook(...)`
  - `NookJavaHookHookDeferred(...)`
  - `PendingJavaHookRegistry`
  - `InstallNow(...)`
  - `JavaHook::HookMethod(...)`
  - `JavaHook::FindClass(...)`
- this meant loader-scoped `.implementation` setup only looked correct at the JS layer

Resolution applied:

- added public loader-aware C API variants in [NookJavaHook.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/include/nook/NookJavaHook.h):
  - `NookJavaHookHookWithLoader(...)`
  - `NookJavaHookHookDeferredWithLoader(...)`
  - `NookJavaHookFindClassWithLoader(...)`
- kept old APIs as null-loader shims
- extended framework/internal plumbing in [NookJavaHook.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookJavaHook.cpp) and [NookJavaHookInternal.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookJavaHookInternal.h):
  - `InstallNow(..., jobject loader, ...)`
- persisted loader identity in deferred requests inside [pending_java_hook_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/java_hook/deferred/pending_java_hook_registry.h) and [pending_java_hook_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/java_hook/deferred/pending_java_hook_registry.cpp)
- extended `JavaHook` in [JavaHook.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/java_hook/JavaHook.h) and [JavaHook.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/java_hook/JavaHook.cpp):
  - `JavaHook::FindClassWithLoader(...)`
  - `JavaHook::HookMethodWithLoader(...)`
- updated the agent runtime default install path in [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp) so non-zero `loader_handle` now calls `NookJavaHookHookDeferredWithLoader(...)`

Why this is closer to Frida:

- loader-scoped wrappers are no longer only a JS/UI-level illusion
- `Java.ClassFactory.get(loader)` now has a matching loader-aware native install primitive underneath
- old default-loader behavior stays intact for `Java.use(...)`

Validation completed:

- public header compile check:
  - `g++ -std=c++17 -I . -I include -c tests/headers/test_public_headers.cpp -o tests/headers/test_public_headers_loader_api.o`
- main host regression rebuilt and passed:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `build\\test_js_runtime_native_attach.exe`
- added device smoke script:
  - [java_class_factory_impl_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_class_factory_impl_smoke.js)

Current boundary:

- this pass fixes loader-aware hook installation and deferred retry persistence
- it still does not add:
  - `factory.$new(...)`
  - `factory.cast(...)`
  - `factory.retain(...)`
  - `factory.choose(...)`
  - `Java.setClassLoader(loader)`

## 2026-04-26 Loader-aware hook API收口

Final adjustments in this pass:

- updated the source-regression checks in [test_java_hook_runtime_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_java_hook_runtime_regressions.cpp) to match the new split design:
  - `JavaHook::FindClass()` is now only the null-loader shim
  - `JavaHook::FindClassWithLoader()` owns the real loader resolution path
  - `JsJavaInstallImplementation(...)` readiness check must target the function definition body instead of the earlier forward declaration
- relaxed the loader-aware API assertion so it verifies stable implementation signals instead of one brittle single-line callsite string:
  - public `*WithLoader` APIs exist
  - deferred registry persists `loader_handle`
  - pending install restores `loader_handle`
  - JS bridge now dispatches through `NookJavaHookHookDeferredWithLoader(...)`

Verification completed after the final cleanup:

- source regression exe passed:
  - `g++ -std=c++17 tests/headers/test_java_hook_runtime_regressions.cpp -o tests/headers/test_java_hook_runtime_regressions_loader_api.exe`
  - `tests\\headers\\test_java_hook_runtime_regressions_loader_api.exe`
- public header compile check passed:
  - `g++ -std=c++17 -I . -I include -c tests/headers/test_public_headers.cpp -o tests/headers/test_public_headers_loader_api.o`
- host runtime regression rebuilt and passed:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `build\\test_js_runtime_native_attach.exe`
- Android artifacts rebuilt and pushed:
  - `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
  - `adb push libs/arm64-v8a/. /data/local/tmp/nook/`

Ready for device validation:

- smoke script:
  - [java_class_factory_impl_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_class_factory_impl_smoke.js)
- expectation:
  - `Java.ClassFactory.get(loader).use("com.demo.target.LoginFragment").verifyPasswordNative.overload("java.lang.String").implementation = ...`
  - should install through the selected loader
  - entering a wrong password should still hit `java-class-factory-impl-enter:<password>` and return `true`

## 2026-04-27 Loader-aware ClassFactory.choose

Goal in this pass:

- make `Java.ClassFactory.get(loader).choose(className, callbacks)` behave like the loader-scoped companion to `factory.use(className)`
- ensure objects returned to `onMatch(instance)` keep the same loader identity for later instance method resolution

Root cause:

- `Java.choose(...)` only enumerated with the default loader path
- even if enumeration became loader-aware, `onMatch(instance)` wrappers would still lose loader identity unless the wrapper itself carried `__nookJavaLoaderHandle`

Resolution applied:

- extended internal object enumeration plumbing so loader-aware choose can pass `loader_handle` through:
  - [nook_java_js_bridge.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.h)
  - [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp)
- `DefaultEnumerateJavaObjects(...)` now resolves the target class with:
  - `JavaHook::FindClassWithLoader(env, loader, class_name)`
- extended [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - `Java.choose(className, callbacks, loaderHandle)` now accepts an internal optional third argument
  - `Java.ClassFactory.get(loader).choose(className, callbacks)` now forwards to that internal path
  - `onMatch(instance)` wrappers created by loader-aware choose now preserve `loaderHandle`, so later `instance.method(...)` calls stay loader-scoped

Why this is closer to Frida:

- loader scoping now applies to both:
  - `factory.use(...)`
  - `factory.choose(...)`
- `choose()` results are no longer partially scoped objects that silently fall back to the default loader on later method access

Validation completed:

- host runtime regression rebuilt and passed:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `build\\test_js_runtime_native_attach.exe`
- source regression still passed:
  - `g++ -std=c++17 tests/headers/test_java_hook_runtime_regressions.cpp -o tests/headers/test_java_hook_runtime_regressions_loader_api.exe`
  - `tests\\headers\\test_java_hook_runtime_regressions_loader_api.exe`
- added device smoke script:
  - [java_class_factory_choose_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_class_factory_choose_smoke.js)

Current boundary:

- this pass adds only loader-aware `factory.choose(...)`
- it still does not add:
  - `factory.$new(...)`
  - `factory.cast(...)`
  - `factory.retain(...)`

## 2026-04-27 Loader-aware ClassFactory.$new

Goal in this pass:

- add the Frida-like constructor entrypoint on loader-scoped wrappers only:
  - `Java.ClassFactory.get(loader).use(className).$new(...)`
- keep the public API minimal:
  - no `factory.$new(className, ...)`

Root cause / gap before this pass:

- loader-aware `factory.use(className)` could resolve methods and hooks, but it could not construct new Java objects
- even if constructor invocation returned a Java object, the wrapper path would lose `loader_handle`, so later instance method calls would silently fall back to the default loader

Resolution applied:

- extended [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - `CreateJavaUseWrapper(...)` now exposes `$new(...)` on loader-aware wrappers
  - `$new(...)` forwards into `__nookJavaInvoke(...)` using synthetic constructor metadata:
    - `$methodName = "<init>"`
    - `$isStatic = false`
    - `__nookJavaReceiverHandle = '0x0'`
    - wrapper `loaderHandle` is preserved
  - if the first `$new(...)` argument is a raw JNI signature like `()V`, it is consumed as an exact constructor signature override
- extended [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp):
  - added constructor detection for `<init>` / `$init`
  - added constructor overload resolution through reflection over `Class.getDeclaredConstructors()`
  - added default constructor invocation path using `JNIEnv::NewObjectA(...)`
- adjusted JS result wrapping so loader-aware constructor or method returns preserve the same loader-scoped wrapper identity instead of dropping back to a default-loader object wrapper

Why this is closer to Frida:

- `factory.use(className)` is no longer just a loader-aware method/hook wrapper; it can now also construct instances through the same selected loader
- constructor results remain loader-scoped, so follow-up instance calls behave consistently

Regression note:

- there was one earlier host-side failure report around `TestMemoryAllocUtf16String()`
- I could not reproduce it from current source
- fresh rebuilds of both host verification binaries passed, so this looked like a stale or transient local binary state rather than a current-source regression

Validation completed:

- added host regression coverage in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - `TestJavaClassFactoryUseReturnsWrapperWithNew()`
  - `TestJavaClassFactoryUseNewForwardsLoaderHandleToConstructorResolveAndInvoke()`
- fresh host rebuild and pass:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `build\\test_js_runtime_native_attach.exe`
- source regression rebuild and pass:
  - `g++ -std=c++17 tests/headers/test_java_hook_runtime_regressions.cpp -o tests/headers/test_java_hook_runtime_regressions_loader_api.exe`
  - `tests\\headers\\test_java_hook_runtime_regressions_loader_api.exe`
- Android artifacts rebuilt:
  - `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
- updated agent pushed directly to device:
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`
- added device smoke script:
  - [java_class_factory_new_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_class_factory_new_smoke.js)

Device crash found and fixed after first push:

- first device run crashed with:
  - `JNI DETECTED ERROR IN APPLICATION: use of deleted local reference`
- root cause:
  - Java object results coming back through `JsJavaInvoke(...)` were exposed to JS wrappers without first being retained as stable JNI references
  - constructor-created objects from `$new()` hit this immediately, because the next instance call reused a dead local reference
- fix:
  - [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp) now retains every non-null Java object result returned by `InvokeJavaMethod(...)` before wrapping it for JS
  - this covers both:
    - `$new(...)` constructor results
    - ordinary Java method calls that return objects
- added/strengthened host regression:
  - `TestJavaClassFactoryUseNewForwardsLoaderHandleToConstructorResolveAndInvoke()` now asserts the constructor result is retained before the follow-up instance call
- verification after the fix:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `build\\test_js_runtime_native_attach.exe`
  - `g++ -std=c++17 tests/headers/test_java_hook_runtime_regressions.cpp -o tests/headers/test_java_hook_runtime_regressions_loader_api.exe`
  - `tests\\headers\\test_java_hook_runtime_regressions_loader_api.exe`
  - rebuilt Android and pushed direct:
    - `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
    - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`

Second device crash and final ownership fix:

- the first repair was incomplete:
  - moving retain into `JsJavaInvoke(...)` still crashed on device
  - latest tombstone showed the abort moved into `RetainJavaObject(...)` itself:
    - `JNI DETECTED ERROR IN APPLICATION: use of deleted local reference`
    - stack reached `RetainJavaObject(...) -> NewGlobalRef(source)`
- final root cause:
  - constructor/object-return results were still being surfaced from the native bridge as raw local refs
  - by the time the higher JS bridge layer tried to retain them, ART already considered the source ref invalid on the device path
- final fix:
  - [nook_java_js_bridge.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.h)
    - extended `JavaJsValue` with `object_handle_is_global`
  - [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp)
    - constructor results now become global refs inside `DefaultInvokeJavaMethod(...)` before being returned to JS
    - generic Java object returns inside `ConvertNookJavaHookValueToJavaJsValue(...)` now also become global refs at conversion time
    - loader/class-loader enumeration paths mark returned object handles as global
  - [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp)
    - `JsJavaInvoke(...)` now only calls `RetainJavaObject(...)` when `object_handle_is_global == false`
- why this version is correct:
  - object ownership is now established at the layer where the JNI local ref is created
  - the JS layer no longer has to guess whether a returned handle is still safe to retain

Final verification:

- fresh host rebuild and pass after the ownership fix:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `build\\test_js_runtime_native_attach.exe`
- fresh source regression rebuild and pass:
  - `g++ -std=c++17 tests/headers/test_java_hook_runtime_regressions.cpp -o tests/headers/test_java_hook_runtime_regressions_loader_api.exe`
  - `tests\\headers\\test_java_hook_runtime_regressions_loader_api.exe`
- Android rebuilt and pushed again:
  - `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`
- final device smoke output:
  - `script load ok: name=java_class_factory_new_smoke.js`
  - `script message: script_id=0 json="java-ready-debug:immediate" data_len=0`
  - `script message: script_id=0 json="java-class-factory-new-loader:dalvik.system.PathClassLoader" data_len=0`
  - `script message: script_id=0 json="java-class-factory-new-wrapper:function:com.demo.target.TextFragment" data_len=0`
  - `script message: script_id=0 json="java-class-factory-new-result:com.demo.target.TextFragment:BAL 10.00" data_len=0`
  - `script message: script_id=0 json="java-class-factory-new-result-exact:com.demo.target.TextFragment:BAL 10.50" data_len=0`

Current boundary:

- this pass adds only wrapper-level loader-aware `$new(...)`
- it still does not add:
  - `factory.$new(className, ...)`
  - loader-aware `factory.cast(...)`
  - loader-aware `factory.retain(...)`
  - a public `Java.setClassLoader(loader)`-style global switch

## 2026-04-27 Loader-aware ClassFactory.cast

Goal in this pass:

- add the loader-scoped companion to `Java.cast(...)`:
  - `Java.ClassFactory.get(loader).cast(objectWrapper, classWrapper)`

Root cause / gap before this pass:

- global `Java.cast(obj, Klass)` already worked
- but `Java.ClassFactory.get(loader)` still could not recast an existing object while forcing later method resolution to stay on the selected loader
- even after adding `cf.cast(...)`, the first draft still dropped the loader handle because `JsJavaCast(...)` ignored the target class wrapper's `__nookJavaLoaderHandle`

Resolution applied:

- extended [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - `Java.ClassFactory.get(loader)` now exposes `cast(objectWrapper, classWrapper)`
  - `cf.cast(...)` rebuilds the target class wrapper through `__nookJavaUseWithLoader(className, loaderHandle)` and then reuses the existing `Java.cast(...)` path
  - `JsJavaCast(...)` now reads `__nookJavaLoaderHandle` from the target class wrapper and passes it into `CreateJavaUseWrapper(...)`

Why this is closer to Frida:

- loader-scoped factory objects can now cover another common Frida flow:
  - enumerate or retain an object
  - cast it into a class view
  - keep later method resolution bound to the intended class loader

Validation completed:

- added host regression coverage in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - `TestJavaClassFactoryGetReturnsFactoryWithCast()`
  - `TestJavaClassFactoryCastForwardsLoaderHandleToTargetWrapperAndInvoke()`
- fresh host rebuild and pass:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `build\\test_js_runtime_native_attach.exe`
- source regression rebuild and pass:
  - `g++ -std=c++17 tests/headers/test_java_hook_runtime_regressions.cpp -o tests/headers/test_java_hook_runtime_regressions_loader_api.exe`
  - `tests\\headers\\test_java_hook_runtime_regressions_loader_api.exe`
- Android rebuilt and pushed:
  - `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`
- added device smoke script:
  - [java_class_factory_cast_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_class_factory_cast_smoke.js)
- final device smoke output:
  - `script load ok: name=java_class_factory_cast_smoke.js`
  - `script message: script_id=0 json="java-ready-debug:immediate" data_len=0`
  - `script message: script_id=0 json="java-class-factory-cast-loader:dalvik.system.PathClassLoader" data_len=0`
  - `script message: script_id=0 json="java-class-factory-cast-wrapper:function:(Landroid/view/View;)V:false" data_len=0`
  - `script message: script_id=0 json="java-class-factory-cast-installed" data_len=0`
  - `script message: script_id=0 json="java-class-factory-cast-result:com.demo.target.TextFragment:true:BAL 10.00" data_len=0`

Current boundary:

- loader-aware `factory.cast(...)` is now in place
- the remaining obvious ClassFactory gap is:
  - loader-aware `factory.retain(...)`

## 2026-04-27 Loader-aware ClassFactory.retain

Goal in this pass:

- add the loader-scoped companion to `Java.retain(...)`:
  - `Java.ClassFactory.get(loader).retain(objectWrapper)`

Root cause / gap before this pass:

- global `Java.retain(obj)` already worked and returned a retained wrapper
- but that wrapper still came back through the default loader view
- if the script wanted to keep using loader-scoped method resolution after retain, it had to manually chain:
  - `Java.retain(obj)`
  - `Java.ClassFactory.get(loader).cast(...)`

Resolution applied:

- extended [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - `Java.ClassFactory.get(loader)` now exposes `retain(objectWrapper)`
  - `cf.retain(...)` internally:
    - calls `Java.retain(objectWrapper)` to preserve the Java object handle
    - rebuilds a loader-aware class wrapper through `__nookJavaUseWithLoader(kept.$className, loaderHandle)`
    - returns `Java.cast(kept, loaderClassWrapper)` so the final retained wrapper stays bound to the selected loader

Why this is closer to Frida:

- loader-scoped object workflows now stay consistent across:
  - `factory.cast(...)`
  - `factory.retain(...)`
- scripts no longer need an explicit two-step retain-then-cast dance to keep loader identity alive

Validation completed:

- added host regression coverage in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - `TestJavaClassFactoryGetReturnsFactoryWithRetain()`
  - `TestJavaClassFactoryRetainPreservesLoaderHandleAndInvoke()`
- fresh host rebuild and pass:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `build\\test_js_runtime_native_attach.exe`
- source regression rebuild and pass:
  - `g++ -std=c++17 tests/headers/test_java_hook_runtime_regressions.cpp -o tests/headers/test_java_hook_runtime_regressions_loader_api.exe`
  - `tests\\headers\\test_java_hook_runtime_regressions_loader_api.exe`
- Android rebuilt and pushed:
  - `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`
- added device smoke script:
  - [java_class_factory_retain_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_class_factory_retain_smoke.js)
- final device smoke output:
  - `script message: script_id=0 json="java-ready-debug:immediate" data_len=0`
  - `script message: script_id=0 json="java-class-factory-retain-loader:dalvik.system.PathClassLoader" data_len=0`
  - `script message: script_id=0 json="java-class-factory-retain-wrapper:function:(Landroid/view/View;)V:false" data_len=0`
  - `script message: script_id=0 json="java-class-factory-retain-installed" data_len=0`
  - `script message: script_id=0 json="java-class-factory-retain-result:com.demo.target.TextFragment:true:true:BAL 10.00" data_len=0`

Current boundary:

- `factory.use(...)`
- `factory.choose(...)`
- `factory.cast(...)`
- `factory.retain(...)`
- `factory.use(...).$new(...)`

still not added:

- `factory.$new(className, ...)`
- a public `Java.setClassLoader(loader)`-style global switch

## 2026-04-27 Java.setClassLoader(loader)

Goal in this pass:

- add a minimal global default-loader switch closer to Frida's loader workflow:
  - `Java.setClassLoader(loader)`
- scope of this API is intentionally narrow:
  - it only affects subsequent default `Java.use(...)`
  - it only affects subsequent default `Java.choose(...)`
  - it only affects subsequent default `Java.cast(...)`
  - it only affects subsequent default `Java.retain(...)`
- it does not retroactively rewrite wrappers that were created before the switch

Design chosen:

- keep `Java.ClassFactory.get(loader)` as the explicit loader-scoped API
- add `Java.setClassLoader(loader)` only as a JS bootstrap layer on top of the existing native bindings
- do not add a new native bridge just for this feature

Resolution applied:

- extended [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - bootstrap now keeps a default loader handle after `Java.setClassLoader(loader)`
  - default `Java.use(className)` routes to `__nookJavaUseWithLoader(className, loaderHandle)` when a default loader is set
  - default `Java.choose(className, callbacks)` forwards the loader handle to the native enumeration path
  - default `Java.cast(objectWrapper, classWrapper)` rebuilds the target class wrapper with the selected loader when needed
  - default `Java.retain(objectWrapper)` retains first and then re-casts through a loader-aware class wrapper

Why this design is correct:

- it matches the Frida-style mental model closely enough for common workflows:
  - pick a loader once
  - continue using plain `Java.use(...)` / `Java.choose(...)` / `Java.retain(...)`
- it avoids overreaching:
  - existing wrappers keep their original loader identity
  - explicit `Java.ClassFactory.get(loader)` remains available for scripts that need precise scoping

Issue hit during device validation:

- the first device smoke failed with:
  - `Java invoke overload resolution failed class=com.demo.target.MainActivity method=incrementIntercept ...`
- root cause was not the runtime implementation
- root cause was a stale smoke/test assumption:
  - `java_set_class_loader_smoke.js` still called `MainActivity.incrementIntercept(41)`
  - the current demo app method is actually [MainActivity.java](/E:/Learn/my_program/all_my_hook/TargetAppDemo/TargetDemoApp/src/main/java/com/demo/target/MainActivity.java#L92) `public static void incrementIntercept()`
- host-side `Java.setClassLoader(...)` tests had the same stale `(I)I` assumption

Fix for the validation gap:

- updated [java_set_class_loader_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_set_class_loader_smoke.js):
  - no longer invokes the stale numeric overload
  - now validates loader-aware default `Java.use(...)` by resolving `incrementIntercept.overload()` and checking `()V:true`
- updated [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - `TestJavaSetClassLoaderMakesSubsequentUseLoaderAware()`
  - `TestJavaSetClassLoaderDoesNotRetroactivelyModifyExistingWrapper()`
  - both now assert loader-aware resolution of the real zero-arg static method instead of a fake int-returning call

Validation completed:

- host regression coverage added or confirmed in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - `TestJavaSetClassLoaderBindingExists()`
  - `TestJavaSetClassLoaderRejectsNonLoaderObject()`
  - `TestJavaSetClassLoaderMakesSubsequentUseLoaderAware()`
  - `TestJavaSetClassLoaderDoesNotRetroactivelyModifyExistingWrapper()`
  - `TestJavaSetClassLoaderMakesChooseLoaderAware()`
  - `TestJavaSetClassLoaderMakesRetainLoaderAware()`
- fresh host rebuild and pass:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `build\\test_js_runtime_native_attach.exe`
- device smoke passed with [java_set_class_loader_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_set_class_loader_smoke.js)
- final device output:
  - `script load ok: name=java_set_class_loader_smoke.js`
  - `script message: script_id=0 json="java-ready-debug:immediate" data_len=0`
  - `script message: script_id=0 json="java-set-class-loader-loader:dalvik.system.PathClassLoader" data_len=0`
  - `script message: script_id=0 json="java-set-class-loader-binding:function" data_len=0`
  - `script message: script_id=0 json="java-set-class-loader-use:()V:true" data_len=0`
  - `script message: script_id=0 json="java-set-class-loader-wrapper:(Landroid/view/View;)V:false:true" data_len=0`
  - `script message: script_id=0 json="java-set-class-loader-installed" data_len=0`
  - `script message: script_id=0 json="java-set-class-loader-retain:com.demo.target.TextFragment:true:BAL 10.00" data_len=0`
  - `script message: script_id=0 json="java-set-class-loader-choose:hook:com.demo.target.TextFragment:true" data_len=0`

Current boundary after this pass:

- `Java.setClassLoader(loader)` is now available and device-validated
- it is still a minimal compatibility layer, not a full Frida clone
- remaining obvious loader-related gap:
  - `factory.$new(className, ...)`

## 2026-04-27 Loader-aware ClassFactory.$new(className, ...)

Goal in this pass:

- add the missing loader-scoped constructor convenience API:
  - `Java.ClassFactory.get(loader).$new(className, ...args)`

Design chosen:

- keep this as a minimal bootstrap-layer API
- do not change existing wrapper-level constructor behavior:
  - `Java.use(...).$new(...)`
  - `Java.ClassFactory.get(loader).use(...).$new(...)`
- only add the factory-level shortcut for the common Frida-style pattern:
  - choose loader once
  - construct by class name directly

Resolution applied:

- extended [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp)
  - `Java.ClassFactory.get(loader)` returned object now exposes `$new(className, ...args)`
  - implementation is intentionally small:
    - validate `className` is a string
    - build `classWrapper = __nookJavaUseWithLoader(className, loaderHandle)`
    - dispatch to `classWrapper.$new.apply(classWrapper, remainingArgs)`

Why this design is correct:

- it reuses the already validated loader-aware wrapper constructor path instead of introducing another native invocation branch
- it keeps semantics aligned with the rest of the loader API:
  - `factory.use(...)`
  - `factory.choose(...)`
  - `factory.cast(...)`
  - `factory.retain(...)`
  - `factory.$new(...)`

Validation completed:

- added host regression coverage in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - `TestJavaClassFactoryGetReturnsFactoryWithNew()`
  - `TestJavaClassFactoryNewForwardsLoaderHandleToConstructorResolveAndInvoke()`
- red phase was observed first:
  - host test failed because `cf.$new` did not exist yet
- green phase after implementation:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `build\\test_js_runtime_native_attach.exe`
- added device smoke script:
  - [java_class_factory_factory_new_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_class_factory_factory_new_smoke.js)
- Android rebuilt and pushed:
  - `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`
- final device smoke output:
  - `script message: script_id=0 json="java-ready-debug:immediate" data_len=0`
  - `script message: script_id=0 json="java-class-factory-factory-new-loader:dalvik.system.PathClassLoader" data_len=0`
  - `script message: script_id=0 json="java-class-factory-factory-new-binding:function" data_len=0`
  - `script message: script_id=0 json="java-class-factory-factory-new-result:com.demo.target.TextFragment:true:BAL 10.00" data_len=0`
  - `script message: script_id=0 json="java-class-factory-factory-new-result-exact:com.demo.target.TextFragment:true:BAL 10.50" data_len=0`

Current boundary after this pass:

- loader-related construction paths now cover:
  - `factory.use(...).$new(...)`
  - `factory.$new(className, ...)`
  - global `Java.setClassLoader(loader)` for default wrapper construction flows
- this is still not a full Frida `ClassFactory` clone, but the obvious loader-scoped object creation gap is now closed

## 2026-04-27 Java.openClassFile(path).load()

Goal in this pass:

- add a minimal Frida-style class loading entry:
  - `Java.openClassFile(path).load()`

Design chosen:

- keep the first version intentionally small
- implement it in the JS bootstrap layer instead of adding a new native bridge
- scope for this pass:
  - accept a file path string
  - create a `dalvik.system.DexClassLoader`
  - switch default loader through `Java.setClassLoader(loader)`
  - return the created loader wrapper
- do not try to cover the full Frida `openClassFile` object model yet

Resolution applied:

- extended [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp)
  - added `Java.openClassFile(path)`
  - it returns an object exposing `load()`
  - `load()` now:
    - uses `android.app.ActivityThread.currentApplication()` to obtain the current `Application`
    - obtains the code cache path through `app.getCodeCacheDir().getAbsolutePath()`
    - obtains the parent loader through `app.getClassLoader()`
    - constructs `dalvik.system.DexClassLoader`
    - uses the exact constructor signature:
      - `(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V`
    - calls `Java.setClassLoader(loader)`
    - returns the `DexClassLoader` wrapper

Why exact constructor signature was necessary:

- the current overload resolver still matches by exact Java type descriptors
- `DexClassLoader` constructor expects `java.lang.ClassLoader`
- the runtime object seen from JS is a concrete loader subclass
- using the exact constructor signature avoids that superclass/subclass mismatch path

Issue hit during device validation:

- the first real-device run failed on:
  - `android.app.Application.getPackageCodePath()`
- root cause was deeper than `openClassFile(...)`
- the Java method resolver only scanned the current class with `getDeclaredMethods()`
- inherited instance methods from parent classes were not considered

First resolver fix:

- updated [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp)
  - both wildcard and exact method resolvers now walk the superclass chain using `getSuperclass()`
  - each level still scans `getDeclaredMethods()`

Second resolver fix:

- the first superclass-walk version still treated overridden methods across child/parent levels as multiple matches
- this caused false:
  - `ResolveJavaMethodSignature ambiguous match`
- final fix changed the resolution rule to:
  - search nearest class first
  - if the current level yields one or more valid matches, stop walking upward
  - only treat it as ambiguous when the same inheritance level itself has multiple valid matches

Why this version is correct:

- it matches Java's practical dispatch expectations better than cross-level accumulation
- inherited methods now resolve
- overridden methods no longer become false ambiguities
- `openClassFile.load()` now relies on the same corrected inherited-method behavior for:
  - `getPackageCodePath()`
  - `getCodeCacheDir()`
  - `getClassLoader()`

Validation completed:

- added host regression coverage in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - `TestJavaOpenClassFileBindingExists()`
  - `TestJavaOpenClassFileLoadSetsDefaultLoaderForSubsequentUse()`
- red phase was observed first:
  - host test failed because `Java.openClassFile` did not exist
- green phase after implementation:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `build\\test_js_runtime_native_attach.exe`
- added source regression coverage in [test_java_hook_runtime_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_java_hook_runtime_regressions.cpp):
  - `VerifyJavaJsBridgeMethodResolutionTraversesSuperclasses()`
- source regression pass:
  - `g++ -std=c++17 tests/headers/test_java_hook_runtime_regressions.cpp -o tests/headers/test_java_hook_runtime_regressions_loader_api.exe`
  - `tests\\headers\\test_java_hook_runtime_regressions_loader_api.exe`
- added device smoke script:
  - [java_open_class_file_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_open_class_file_smoke.js)
- Android rebuilt and pushed:
  - `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`
- final device smoke output:
  - `script message: script_id=0 json="java-ready-debug:immediate" data_len=0`
  - `script message: script_id=0 json="java-open-class-file-app:true" data_len=0`
  - `script message: script_id=0 json="java-open-class-file-path:/data/app/.../base.apk" data_len=0`
  - `script message: script_id=0 json="java-open-class-file-binding:function" data_len=0`
  - `script message: script_id=0 json="java-open-class-file-loader:dalvik.system.DexClassLoader" data_len=0`
  - `script message: script_id=0 json="java-open-class-file-wrapper:com.demo.target.TextFragment:true" data_len=0`
  - `script message: script_id=0 json="java-open-class-file-result:com.demo.target.TextFragment:true:BAL 10.00" data_len=0`

Current boundary after this pass:

- minimal `Java.openClassFile(path).load()` is now available and device-validated
- inherited Java method resolution is materially stronger than before
- still not covered:
  - richer `openClassFile` object lifecycle
  - `ClassFactory.openClassFile(...)`
  - `Java.registerClass(...)`

## 2026-04-27 Java.registerClass(...) groundwork

Goal in this pass:

- start the minimal `Java.registerClass(spec)` path approved in step6
- keep scope intentionally narrow:
  - interface/listener style use-cases only
  - no `extends`
  - no fields
  - no constructor mapping
  - no rich overload declaration surface

Design chosen:

- phase 1 continues to target a Frida-like API shape
- but this pass only lands the host/runtime groundwork first
- actual Android `Proxy.newProxyInstance(...)` + helper `InvocationHandler` creation is deferred to the next pass

Why this staging was chosen:

- the first hard requirement was to prove the JS runtime and bridge can carry:
  - class name
  - interface list
  - JS method table
- that can be validated on host without prematurely committing to an Android-side helper packaging design
- it keeps the TDD cycle tight and avoids mixing API-surface bugs with ART/JNI runtime bugs in one patch

Resolution applied:

- extended [nook_java_js_bridge.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.h):
  - added `JavaJsRegisteredClassMethodRecord`
  - added `JavaJsRegisterClassRequest`
  - added `RegisterJavaClassFn`
  - added `RegisterJavaClass(...)`
- extended [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp):
  - added request validation and dependency-dispatch wrapper for `RegisterJavaClass(...)`
  - current default non-test behavior is still intentionally non-operational:
    - Android path reports `java registerClass Android proxy path is not implemented yet`
    - non-Android path reports `java registerClass bridge is not configured`
- extended [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - bootstrap now exposes `Java.registerClass(spec)`
  - validates:
    - `spec`
    - `spec.name`
    - non-empty `spec.implements`
    - `spec.methods`
  - returned class-like object now exposes `$new()`
  - `$new()` now forwards:
    - class name
    - interface class names
    - JS method table
    - loader handle
    - into a new internal bridge entry `__nookJavaRegisterClass(...)`
  - runtime state now stores per-script registered-class JS method callbacks for later dispatch work

Host validation completed:

- added host tests in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - `TestJavaRegisterClassBindingExists()`
  - `TestJavaRegisterClassReturnsClassLikeObjectWithNew()`
  - `TestJavaRegisterClassNewForwardsInterfacesAndMethodsToBridge()`
- red phase observed first:
  - host build failed because:
    - `JavaJsRegisterClassRequest` did not exist
    - `JavaJsHookInstallerDependencies.register_class` did not exist
- green phase after implementation:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `build\\test_js_runtime_native_attach.exe`

What is working now:

- `typeof Java.registerClass === 'function'`
- `Java.registerClass({...})` returns a class-like object with `$new()`
- `$new()` can forward interface names and method names through the native bridge
- host fake bridge can return a Java object wrapper end-to-end

What is intentionally still missing:

- actual Android `Proxy.newProxyInstance(...)` object creation
- helper `InvocationHandler` packaging/loading
- Java-to-JS callback dispatch through that proxy path
- real-device smoke validation

Current boundary after this pass:

- `Java.registerClass(...)` now exists as a real runtime/bridge surface instead of a docs-only gap
- but it is not yet a complete Android-usable feature
- next pass should focus only on:
  - Android proxy creation
  - callback dispatch
  - real-device smoke

## 2026-04-27 Java.registerClass(...) Android proxy path

Goal in this pass:

- finish the phase-1 Android path for `Java.registerClass(spec)`
- keep the scope minimal and Frida-like:
  - interface-only proxies
  - JS-backed callback dispatch
  - no real class generation / no `extends` / no fields

Resolution applied:

- extended [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp):
  - embedded helper dex is now loaded on demand from [nook_register_class_helper_dex.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/generated/nook_register_class_helper_dex.h)
  - helper dex is written into app `code_cache`
  - helper class `nook.java.NookJsInvocationHandler` is loaded through `DexClassLoader`
  - `nativeInvoke(long, Object, Method, Object[])` is registered through JNI
  - `RegisterJavaClass(...)` now creates a real `Proxy.newProxyInstance(...)` object and returns it as a Java wrapper
- added callback marshalling:
  - Java `String` -> JS string
  - boxed `Boolean/Integer/Long/Float/Double` -> JS primitive
  - other Java objects -> JS Java wrapper
  - JS return values now flow back to Java `Object` results, including:
    - `String`
    - boxed primitive objects
    - retained / wrapped Java objects
- added proxy fallback handling for core `Object` methods:
  - `toString()`
  - `hashCode()`
  - `equals(Object)`

Problems hit during implementation:

- the helper cannot live in host JS only; Android needs a concrete `InvocationHandler` class that ART can instantiate
- loading that helper directly from source is not viable in-process, so the smallest stable path was:
  - compile helper Java once
  - convert to dex
  - embed dex bytes into the agent
- callback argument conversion cannot rely on method signature descriptors alone because `InvocationHandler.invoke(...)` receives `Object[]`
  - solution used here is heuristic runtime conversion, which is enough for phase 1 listener-style callbacks

Verification completed:

- host rebuild:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- host tests:
  - `build\\test_js_runtime_native_attach.exe`
- Android rebuild:
  - `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
- device artifact push:
  - `adb push libs/arm64-v8a/libnook-agent.so /data/local/tmp/nook/libnook-agent.so`

New smoke added:

- [java_register_class_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_register_class_smoke.js)
  - validates:
    - `typeof Java.registerClass`
    - class-like `$new()`
    - proxy instance creation
    - Java callback reaching JS through `OnClickListener.onClick(...)`

Current boundary after this pass:

- host/runtime groundwork is still green
- Android proxy creation path now exists
- remaining work is device validation and any crash/compatibility fixes found by the new smoke

## 2026-04-27 Java.registerClass(...) device smoke result

Real-device smoke completed with [java_register_class_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_register_class_smoke.js).

Final validated path:

- `Java.registerClass` binding exists
- proxy instance is created as `$Proxy2`
- proxy instance can be passed into a real framework API:
  - `View.setOnClickListener(...)`
- framework callback returns into JS through `performClick()`

Observed device output:

- `java-register-class-binding:function`
- `java-register-class-interface:android.view.View$OnClickListener`
- `java-register-class-classlike:function`
- `java-register-class-instance:$Proxy2`
- `java-register-class-app:true`
- `java-register-class-view:android.view.View`
- `java-register-class-installed`
- `java-register-class-callback:android.view.View`
- `java-register-class-invoke-done:true`

Problems hit during smoke iteration:

- direct manual invocation of `listener.onClick(null)` was a bad validation path for this runtime
  - first it hit overload inference on `null`
  - then it hit class-resolution mismatch when trying to resolve overloads from the proxy wrapper side
- the stable smoke is to let Android framework code call the proxy naturally
  - `setOnClickListener(...)`
  - `performClick()`

Updated boundary after device validation:

- phase-1 `Java.registerClass(...)` is now device-validated for interface/listener style callbacks
- confirmed scope:
  - interface-only proxy object creation
  - Java-to-JS callback dispatch
  - JS callback argument wrapping for normal object arguments
- still intentionally out of scope:
  - real subclass generation
  - `extends`
  - custom fields
  - constructor mapping
  - richer Frida-compatible class spec semantics

## 2026-04-27 Java.performNow(fn)

Goal for this pass:

- add a minimal Frida-style `Java.performNow(fn)` surface
- keep semantics intentionally small:
  - immediate execution
  - no `Java.ready(...)` queueing
  - no extra native bridge

Frida semantic reference used:

- `Java.performNow(fn)` should execute immediately and should not wait for the app class loader to become ready
- for Nook phase 1, this maps cleanly to a JS bootstrap helper because the Java bridge already attaches JNI on demand

What was implemented:

- added `Java.performNow = function (fn) { ... }` in the Java bootstrap layer in:
  - [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp)
- current behavior:
  - non-function input throws `TypeError('Java.performNow requires a function')`
  - valid callback is executed immediately
  - callback return value is returned directly

Main problem encountered:

- initial investigation looked contradictory:
  - source search showed no `performNow`
  - but one earlier host run appeared green
- root cause turned out to be two separate issues:
  - the feature was indeed missing from the bootstrap
  - one verification attempt ran compile and test in parallel, so the test could hit the old `test_js_runtime_native_attach.exe`

How this was resolved:

- first recompiled the host test binary sequentially and confirmed the red state was real:
  - `typeof Java.performNow` was not `function`
- then added the minimal bootstrap implementation
- then verified with an isolated local probe that the new runtime exposed:
  - `performNow`
  - `ready`
  - `registerClass`
  - `setClassLoader`
  - the rest of the Java bootstrap helpers together
- finally reran the full host test binary sequentially after compile

Verification completed:

- host rebuild:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- host tests:
  - `.\\build\\test_js_runtime_native_attach.exe`

Host coverage now includes:

- `typeof Java.performNow === 'function'`
- non-function rejection
- immediate side-effect ordering:
  - callback writes happen before later top-level script statements

New smoke added:

- [java_perform_now_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_perform_now_smoke.js)
  - validates:
    - `typeof Java.performNow`
    - immediate callback ordering
    - direct framework access without `Java.ready(...)`
    - `java.lang.System.currentTimeMillis()`
    - `android.app.ActivityThread.currentApplication()`

Current boundary after this pass:

- `Java.performNow(fn)` now exists in Nook and matches the chosen phase-1 Frida-like semantics
- no new native bridge was needed
- real-device smoke is now validated

Observed device output:

- `java-perform-now-bindings:object:function:function:true`
- `java-perform-now-callback:inside:true:true`
- `java-perform-now-order:inside|after`

Validated device boundary:

- `Java.performNow` binding exists on device
- callback executes immediately instead of entering the `Java.ready(...)` queue
- callback can directly call Java framework APIs
- callback side effects are visible before later top-level script statements

## 2026-04-27 Java.ClassFactory.get(loader).openClassFile(path).load()

Goal for this pass:

- align Nook more closely with the Frida public Java API shape
- add the loader-scoped `ClassFactory.openClassFile(...)` path instead of forcing scripts through the global `Java.openClassFile(...)` entry

Frida-facing design choice:

- keep `Java.openClassFile(path).load()` as the global/default-loader path
- add `Java.ClassFactory.get(loader).openClassFile(path).load()` as the explicit loader-scoped path
- do not mutate the global default loader during the class-factory flow

What was implemented:

- extended the object returned by `Java.ClassFactory.get(loader)` in:
  - [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp)
- new public surface:
  - `factory.openClassFile(filePath)`
  - returned object exposes `load()`
- current behavior:
  - non-string input throws `TypeError('Java.ClassFactory.openClassFile requires a file path string')`
  - `load()` obtains `Application` and code-cache path
  - `load()` creates `dalvik.system.DexClassLoader`
  - the current factory loader is used as the parent loader
  - no `Java.setClassLoader(...)` side effect is performed

Why this shape was chosen:

- Nook already had working loader plumbing
- the main remaining gap was public API shape relative to Frida
- adding this at the bootstrap layer closed that gap without expanding the native bridge

Verification completed:

- host rebuild:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- host tests:
  - `.\\build\\test_js_runtime_native_attach.exe`
- Android rebuild:
  - `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
- device artifact push:
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`

Host coverage now includes:

- `typeof Java.ClassFactory.get(loader).openClassFile === 'function'`
- non-string rejection
- `load()` returns a loader wrapper
- `load()` uses the current factory loader as parent loader
- `load()` does not mutate the default-loader path used by later plain `Java.use(...)`

New smoke added:

- [java_class_factory_open_class_file_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_class_factory_open_class_file_smoke.js)

Observed device output:

- `java-ready-debug:immediate`
- `java-cf-open-class-file-loader:true:dalvik.system.PathClassLoader`
- `java-cf-open-class-file-binding:function`
- `java-cf-open-class-file-path:/data/app/~~s38HzWNCGWYG7GDjii4xKw==/com.demo.target-s_2k4PosVsL1KrbVAyILrA==/base.apk`
- `java-cf-open-class-file-result:dalvik.system.DexClassLoader:com.demo.target.TextFragment:BAL 10.00`

Validated device boundary:

- a real application `PathClassLoader` can be selected
- `factory.openClassFile(path)` binding exists on device
- `load()` returns a `DexClassLoader`
- `Java.ClassFactory.get(returnedLoader).use('com.demo.target.TextFragment')` succeeds
- instance creation and instance method invocation still work through the returned loader scope

Still intentionally out of scope:

- `Java.classFactory`
- `ClassFactory` cache/temp-file knobs
- `getClassNames()` support on the returned class-file object
- broader refactor of `Java.openClassFile(...)` object lifecycle

## 2026-04-27 Spawn / Zygote / Ready Stability Batch 1

Goal for this pass:

- tighten the existing spawn pipeline before doing larger Frida-style pre-resume work
- remove a few phase-1 nondeterministic behaviors that were still causing opaque host-side timeouts

Problems addressed:

- `SessionRegistry::BindHostToPid()` allowed one host session to remain bound to multiple pids
  - effect: after rebinding, `FindPidByHostSession()` depended on map iteration order instead of the newest authoritative pid
- `SCRIPT_CREATE` / `SCRIPT_LOAD` silently returned when the host was bound but the agent session was not ready
  - effect: host side only saw a generic request timeout, with no server-side stage information
- `HostSpawnClient::SpawnAndWait()` still collapsed two different timeout stages into coarse wording
  - spawn response timeout
  - authoritative `AgentReady` timeout

Minimal fixes implemented:

- [session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)
  - `BindHostToPid()` now removes older pid mappings for the same host session before binding the new pid
- [server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
  - added immediate `ScriptCreateResp` errors for:
    - invalid request
    - missing registry
    - host not bound to a pid
    - bound pid exists but agent session is not ready
  - added immediate `ScriptLoadResp` errors for the same not-ready states
- [host_spawn_client.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/communication/host/host_spawn_client.cpp)
  - spawn request timeout text changed to:
    - `wait spawn response timed out`
  - agent ready timeout text changed to:
    - `wait authoritative agent ready timed out`

Regression coverage added:

- [test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)
  - host rebind keeps only the newest pid mapping
  - script create returns immediate error when the agent session is not ready
  - script load returns immediate error when the agent session is not ready
- [test_host_spawn_client.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_host_spawn_client.cpp)
  - spawn response timeout is stage-specific
  - authoritative agent ready timeout is stage-specific

Verification completed:

- host spawn client rebuild:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_host_spawn_client.cpp src/communication/host/host_spawn_client.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_host_spawn_client.exe`
- host spawn client tests:
  - `.\\build\\test_host_spawn_client.exe`
- server handler rebuild:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/transport/spawn_marker.cpp src/communication/transport/path_utils.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_handlers.exe`
- server handler tests:
  - `.\\build\\test_server_handlers.exe`

Current boundary after this pass:

- host rebind behavior is now deterministic for one host session
- not-ready script create/load failures are surfaced immediately instead of timing out opaquely
- spawn timeout reporting now distinguishes:
  - `wait spawn response timed out`
  - `wait authoritative agent ready timed out`

Still intentionally not done in this batch:

- a full spawn transaction state machine
- stricter `spawn -> load -> resume` state enforcement
- REPL suspended/resumed status refinement
- cold-spawn device regression coverage for this batch

## 2026-04-27 Spawn / Zygote / Ready Stability Batch 2

Goal for this pass:

- tighten the actual spawn state transitions instead of only improving diagnostics
- prevent the server from releasing the spawn gate before the authoritative child has finished `AgentReady`

Root cause addressed:

- `MarkSpawnSuspended()` previously only recorded a boolean gate-held flag
- after `SpawnResponse`, the host could send `ResumeRequest` immediately and the server would release the gate even if no authoritative `AgentReady` had been observed yet
- if an agent session happened to exist early, `SCRIPT_LOAD` could also be forwarded before the spawned child had reached the ready phase

Minimal state model added:

- [session_registry.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.h)
- [session_registry.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/session_registry.cpp)

Added `SpawnTransactionState`:

- `kWaitingAgentReady`
- `kReadyForScriptLoad`
- `kScriptLoadDispatched`

`SpawnSuspendedEntry` now carries:

- `pid`
- `host_session_id`
- `suspended`
- `state`

Server-side transition tightening:

- [server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_handlers.cpp)
  - `HandleSpawnRequest()` now upgrades cached-ready spawn entries to `kReadyForScriptLoad` before replay
  - `HandleAgentReady()` now marks matching spawn entries as `kReadyForScriptLoad`
  - `HandleResumeRequest()` now rejects early resume while state is still `kWaitingAgentReady`
    - error:
      - `spawned process is not ready to resume`
  - `HandleScriptLoad()` now rejects startup load while state is still `kWaitingAgentReady`
    - error:
      - `spawned pid is not ready for script load`
  - forwarded startup load marks the entry as `kScriptLoadDispatched`
  - `HandleScriptCreate()` uses the same ready gate for spawn-state sessions

Regression coverage added:

- [test_server_handlers.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_server_handlers.cpp)
  - spawn entry starts in `kWaitingAgentReady`
  - resume fails before authoritative `AgentReady`
  - resume succeeds after authoritative `AgentReady`
  - resume still succeeds only once
  - spawn-state `script load` is rejected before authoritative `AgentReady`

Verification completed:

- rebuild:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/transport/spawn_marker.cpp src/communication/transport/path_utils.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_handlers.exe`
- tests:
  - `.\\build\\test_server_handlers.exe`

Current boundary after this pass:

- `SpawnResponse` no longer implies "safe to resume immediately"
- authoritative `AgentReady` is now a real state transition in the server
- startup `SCRIPT_LOAD` cannot bypass the ready phase just because an agent session object exists

Still intentionally not done in this batch:

- keeping a durable post-resume transaction record instead of clearing the entry
- CLI / REPL surfaced suspended/resumed state messaging
- device-side regression validation for this stricter ordering

## 2026-04-27 Spawn / Zygote / Ready Stability Batch 3 (CLI / REPL Semantics)

Goal for this pass:

- make suspended vs resumed state clearer in the Python CLI / REPL
- make `%resume` refusal and REPL load failures report context instead of only bubbling raw exceptions

Problems addressed:

- `%info` only showed `resumed=yes/no`, but did not explicitly label spawn-session state
- `%resume` against an already resumed spawn session printed a coarse `already resumed`
- `%resume` failure inside REPL would previously escape to the top-level handler and lose the local suspended-session context

Minimal CLI changes:

- [cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/cli.py)
  - `%info` now includes:
    - `spawn_state=suspended`
    - `spawn_state=resumed`
    - `spawn_state=n/a` for attach mode
  - `%resume` on an already resumed spawn session now prints:
    - `already resumed: pid=<pid> state=resumed`
  - `%resume` failure inside REPL now reports:
    - `resume failed while spawn session is suspended: <error>`
  - `%load` failure inside REPL now reports session context:
    - `script load failed while spawn session is suspended: <error>`
    - `script load failed while attach session is resumed: <error>`

Regression coverage added:

- [test_cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_cli.py)
  - `%info` shows `spawn_state=suspended`
  - `%info` after `%resume` shows `spawn_state=resumed`
  - second `%resume` prints the explicit resumed-state message
  - `%resume` failure keeps REPL alive and reports suspended-session context

Verification completed:

- `python -m unittest host/nook-py/tests/test_cli.py`

Current boundary after this pass:

- REPL state is clearer for suspended spawn sessions
- `%resume` refusal / failure output is now specific enough to map back to the spawn gate lifecycle
- no CLI command-surface changes were introduced

Still intentionally not done in this batch:

- device-side validation for the refined CLI messaging
- broader host-side state machine beyond the current spawn-session flags

## 2026-04-27 Cold-spawn Java.ready regression smoke

Goal for this pass:

- add a dedicated cold-spawn regression smoke for `Java.ready(...)`
- make spawn-time failures easier to classify without mixing them into the older generic `java_ready_smoke.js`

Design choice:

- keep the existing [java_ready_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_ready_smoke.js) as the small baseline smoke
- add a separate [java_ready_spawn_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_ready_spawn_smoke.js) for cold-spawn diagnosis

New smoke output stages:

- `java-ready-spawn-bindings:...`
  - confirms `Java`, `Java.ready`, `Java.performNow`, and `Java._isClassLoaderReady` are present
- `java-ready-spawn-script-enter:<ready>`
  - confirms the script top-level executed during startup
- `java-ready-spawn-perform-now:<ready>:<hasApp>`
  - immediate state at load time without waiting for `Java.ready(...)`
- `java-ready-spawn-fired:<ready>:<hasApp>:<hasLoader>:<className>:<methodType>`
  - confirms the queued `Java.ready(...)` callback actually fired and app-class resolution works

Intended failure interpretation:

- if you only see:
  - `spawn response ok`
  - `agent ready`
  - `script load ok`
  - but no `java-ready-spawn-*`
  - then startup script execution itself did not run as expected
- if you see:
  - `java-ready-spawn-bindings`
  - `java-ready-spawn-script-enter`
  - `java-ready-spawn-perform-now`
  - but no `java-ready-spawn-fired`
  - then the script loaded while gate-held, but the Java class-loader ready transition never drained the callback
- if `java-ready-spawn-fired` appears with:
  - `true:true:true:com.demo.target.LoginFragment:function`
  - then the cold-spawn `Java.ready(...)` path reached the expected state

Local verification completed:

- syntax-only parse check:
  - `node -e "new Function(require('fs').readFileSync('host/nook-py/java_ready_spawn_smoke.js','utf8')); console.log('ok')"`

Recommended device command:

- `nook-cli spawn com.demo.target -l E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\host\\nook-py\\java_ready_spawn_smoke.js --resume --wait --usb`

Current boundary after this pass:

- there is now a dedicated cold-spawn Java-ready regression script
- it is designed to separate:
  - script top-level execution
  - immediate Java availability
  - final Java-ready callback delivery

Still intentionally not done in this pass:

- no automated host-side test for this smoke
- no fresh device validation yet for this exact script

## 2026-04-27 Java.ready app-ready gate tightening

Goal for this pass:

- remove the remaining timing window where `Java.ready(...)` could run as soon as the app class loader was cached even though `ActivityThread.currentApplication()` was still null
- keep `Java.performNow(fn)` immediate
- tighten only the queued `Java.ready(...)` path

Root cause:

- the previous bootstrap treated:
  - `Java._isClassLoaderReady() == true`
  as sufficient for:
  - immediate `Java.ready(...)`
  - draining queued callbacks
- on cold-spawn device output we observed:
  - `java-ready-spawn-perform-now:true:false`
  - meaning:
    - class loader already looked ready
    - current `Application` was still unavailable
- this meant the old ready gate was still slightly earlier than the application-ready point users usually care about

Minimal implementation:

- [java_hook_loader_resolver.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/java_hook/deferred/java_hook_loader_resolver.h)
- [java_hook_loader_resolver.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/java_hook/deferred/java_hook_loader_resolver.cpp)
  - added:
    - `bool IsCurrentApplicationReady(JNIEnv* env);`
  - implementation uses `GetCurrentApplication(env)` and returns true only when the current `Application` is actually available

- [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp)
  - added internal JS binding:
    - `Java._isAppReady()`
  - `Java.ready(fn)` immediate path now uses:
    - `readyFired || Java._isAppReady()`
  - `Instrumentation.newApplication(...)`
    - still updates the cached class loader
    - no longer drains `Java.ready(...)` callbacks early
  - `Instrumentation.callApplicationOnCreate(app)`
    - refreshes the loader from `app.getClassLoader()`
    - drains queued callbacks only when `Java._isAppReady()` becomes true

Important non-change:

- `Java.performNow(fn)` was left immediate on purpose
- this pass does not turn `performNow` into a queued readiness primitive

Smoke update:

- [java_ready_spawn_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_ready_spawn_smoke.js)
  - now also reports:
    - `typeof Java._isAppReady`
    - top-level `_isAppReady()`
    - `performNow` `_isAppReady()`
    - final fired `_isAppReady()`

Updated cold-spawn interpretation:

- expected earlier startup shape is now closer to:
  - `java-ready-spawn-script-enter:false:false`
  - `java-ready-spawn-perform-now:true:false:false` or nearby timing variation
- final success target is:
  - `java-ready-spawn-fired:true:true:true:true:com.demo.target.LoginFragment:function`

Verification completed:

- header/text regression:
  - `g++ -std=c++17 tests\\headers\\test_java_ready_object_bridge.cpp -o build\\test_java_ready_object_bridge.exe`
  - `.\\build\\test_java_ready_object_bridge.exe`
- js runtime regression:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `.\\build\\test_js_runtime_native_attach.exe`
- smoke syntax check:
  - `node -e "new Function(require('fs').readFileSync('host/nook-py/java_ready_spawn_smoke.js','utf8')); console.log('ok')"`

Recommended device command:

- `nook-cli spawn com.demo.target -l E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\host\\nook-py\\java_ready_spawn_smoke.js --resume --wait --usb`

Current boundary after this pass:

- `Java.ready(...)` now waits for application readiness instead of only class-loader readiness
- cold-spawn diagnostics can now distinguish:
  - class-loader ready
  - app ready
  - final callback delivery

## 2026-04-27 _isClassLoaderReady app-aligned semantics

Goal for this pass:

- remove the remaining observable window where:
  - `Java._isClassLoaderReady() == false`
  - `Java._isAppReady() == true`
- make the public debug/helper bit `_isClassLoaderReady()` line up with the app-ready phase instead of exposing an earlier partial state

Root cause:

- after the previous pass, `Java.ready(...)` itself was already gated by `_isAppReady()`
- but the helper `_isClassLoaderReady()` still only required:
  - cached/resolved application class loader
- this left a diagnostic-only mismatch in cold spawn output:
  - script top-level could report loader `false` while app `true`

Minimal implementation:

- [java_hook_loader_resolver.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/java_hook/deferred/java_hook_loader_resolver.cpp)
  - `IsApplicationClassLoaderReady(JNIEnv* env)` now first requires:
    - `IsCurrentApplicationReady(env)`
  - if the current `Application` is not ready, loader-ready now returns `false` immediately

Effect:

- `_isClassLoaderReady()` is now effectively "application loader ready in an application-ready process"
- this keeps the helper aligned with the later-stage semantics users actually care about during cold spawn

Regression coverage tightened:

- [test_java_ready_object_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_java_ready_object_bridge.cpp)
  - now asserts the loader resolver source contains:
    - `if (!IsCurrentApplicationReady(env)) {`

Verification completed:

- `g++ -std=c++17 tests\\headers\\test_java_ready_object_bridge.cpp -o build\\test_java_ready_object_bridge.exe`
- `.\\build\\test_java_ready_object_bridge.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\\build\\test_js_runtime_native_attach.exe`
- `node -e "new Function(require('fs').readFileSync('host/nook-py/java_ready_spawn_smoke.js','utf8')); console.log('ok')"`

Expected device impact:

- the earlier cold-spawn line:
  - `java-ready-spawn-script-enter:false:true`
- should now move closer to:
  - `java-ready-spawn-script-enter:false:false`

Current boundary after this pass:

- `Java.ready(...)`
- `Java._isAppReady()`
- `Java._isClassLoaderReady()`

are now all aligned on the application-ready side of the startup boundary

## 2026-04-27 Java.ready explicit lifecycle-ready gate

Goal for this pass:

- stop deriving `Java._isAppReady()` directly from the native `currentApplication()` snapshot
- introduce a JS-layer lifecycle gate that stays false during cold-spawn startup even if `currentApplication()` becomes visible early

Why this change was needed:

- after the previous pass, device output still showed:
  - `java-ready-spawn-script-enter:false:true`
- that meant:
  - `_isClassLoaderReady()` was already tightened
  - but `_isAppReady()` was still effectively exposing the earlier native `currentApplication()` signal
- this kept `Java.ready(...)` closer to "Application object exists" than to the later lifecycle point we wanted

Chosen implementation:

- keep the native helper currently exposed as `Java._isAppReady` only as a bootstrap-time input
- in the JS bootstrap inside [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - capture the native helper as:
    - `var nativeAppReady = Java._isAppReady;`
  - create a JS-local lifecycle flag:
    - `var appLifecycleReady = Java._isClassLoaderReady() && nativeAppReady();`
  - override the public helper with:
    - `Java._isAppReady = function () { return appLifecycleReady; };`

Lifecycle behavior after this change:

- attach / already-running app:
  - class loader ready + native app ready at bootstrap time
  - `appLifecycleReady` initializes to `true`
  - `Java.ready(...)` still runs immediately
- cold spawn:
  - bootstrap may see early native app readiness
  - but if class loader is not yet ready at bootstrap time, `appLifecycleReady` remains `false`
  - `Instrumentation.newApplication(...)` only updates the loader
  - `Instrumentation.callApplicationOnCreate(app)` sets:
    - `appLifecycleReady = true;`
  - queued `Java.ready(...)` callbacks drain after the original `callApplicationOnCreate(...)` returns

This is intentionally later than the previous implementation:

- removed the old `callApplicationOnCreate-drain-before` path
- lifecycle callbacks now drain only on the `-after` path

Regression coverage tightened:

- [test_java_ready_object_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_java_ready_object_bridge.cpp)
  - now asserts the bootstrap contains:
    - `var nativeAppReady = Java._isAppReady;`
    - `var appLifecycleReady = Java._isClassLoaderReady() && nativeAppReady();`
    - `Java._isAppReady = function () {`
    - `return appLifecycleReady;`
    - `appLifecycleReady = true;`

Verification completed:

- `g++ -std=c++17 tests\\headers\\test_java_ready_object_bridge.cpp -o build\\test_java_ready_object_bridge.exe`
- `.\\build\\test_java_ready_object_bridge.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\\build\\test_js_runtime_native_attach.exe`
- `node -e "new Function(require('fs').readFileSync('host/nook-py/java_ready_spawn_smoke.js','utf8')); console.log('ok')"`

Expected device impact:

- cold-spawn top-level should move closer to:
  - `java-ready-spawn-script-enter:false:false`
- `performNow` may still observe:
  - loader ready
  - app object present
  - lifecycle-ready still false
  depending on the exact timing

## 2026-04-27 Java.ready lifecycle gate rollback to stable semantics

Observed regression:

- after introducing the JS-only lifecycle-ready gate, cold-spawn device output changed to:
  - `java-ready-spawn-script-enter:true:false`
  - `java-ready-spawn-perform-now:true:false:true`
  - `java-ready-debug:queued:1`
  - `java-ready-debug:install-hooks`
- and then `java-ready-spawn-fired:...` never arrived

Root cause:

- the JS-layer lifecycle gate made `Java.ready(...)` wait for `callApplicationOnCreate(...)`
- but by the time the startup script loaded and installed the `Instrumentation` hooks, that lifecycle event could already be in the past
- once that happened:
  - callbacks stayed queued forever
  - app startup also showed visible delay from the extra hook setup

Conclusion from this experiment:

- with the current architecture, a later lifecycle-ready point cannot be derived reliably only from JS bootstrap hooks
- if we want a stricter, later, Frida-like ready point without missed events, the signal must come from native-side state captured before script execution

Final recovery fix:

- [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp)
  - removed the JS override of `Java._isAppReady()`
  - removed the JS-local `appLifecycleReady` experiment entirely
  - restored the stable path where:
    - the native helper exposed as `Java._isAppReady()` is used directly
  - kept the safer `callApplicationOnCreate-drain-after` behavior
  - still avoids the earlier before-drain path

Effect:

- `Java.ready(...)` goes back to prioritizing "do not miss the event"
- the previous cold-spawn behavior where `Java.ready(...)` eventually fired is restored
- the stricter lifecycle gate attempt is documented as a design limitation, not left as a silent regression

Regression coverage updated:

- [test_java_ready_object_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_java_ready_object_bridge.cpp)
  - now asserts the old before-drain path is gone:
    - no `callApplicationOnCreate-drain-before`

Verification completed:

- `g++ -std=c++17 tests\\headers\\test_java_ready_object_bridge.cpp -o build\\test_java_ready_object_bridge.exe`
- `.\\build\\test_java_ready_object_bridge.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\\build\\test_js_runtime_native_attach.exe`

Current recommendation after this pass:

- keep the current working `Java.ready(...)` behavior
- if we want a stronger pre-resume / later-ready model, implement a native-side lifecycle flag rather than another JS-only delay

## 2026-04-27 Native lifecycle-ready signal scaffold

Goal:

- start the native-side follow-up promised by the rollback notes
- capture a stricter lifecycle checkpoint in native code before startup scripts execute
- avoid changing `Java.ready(...)` semantics until the lower-level signal is proven on device

Design:

- reuse the existing native bootstrap observation point already installed in:
  - [NookComm.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookComm.cpp)
  - `SpawnGateBootstrapHookCallback(...)`
- when `android.app.Instrumentation.callApplicationOnCreate(Application)` is intercepted by the spawn gate hook:
  - keep updating the cached application class loader
  - additionally mark a process-global lifecycle-ready bit in the loader resolver
- expose that bit to JS as a dedicated internal helper instead of overloading `_isAppReady()` again

Implementation:

- [java_hook_loader_resolver.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/java_hook/deferred/java_hook_loader_resolver.h)
  - added:
    - `void MarkApplicationLifecycleReady(JNIEnv* env, jobject application);`
    - `bool IsApplicationLifecycleReady(JNIEnv* env);`
- [java_hook_loader_resolver.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/java_hook/deferred/java_hook_loader_resolver.cpp)
  - added a dedicated native flag:
    - `g_application_lifecycle_ready`
  - `MarkApplicationLifecycleReady(...)`:
    - refreshes the app class loader from the passed `Application`
    - then flips the lifecycle-ready bit
  - `IsApplicationLifecycleReady(...)` currently returns the strict native flag only
- [NookComm.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookComm.cpp)
  - `SpawnGateBootstrapHookCallback(...)` now calls:
    - `JavaHookLoaderResolver::MarkApplicationLifecycleReady(...)`
  - this happens from the native spawn bootstrap interception path, before the app is released
- [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp)
  - exposed the new internal helper:
    - `Java._isLifecycleReady()`
  - intentionally did not switch `Java.ready(...)` over to it yet
- [java_ready_lifecycle_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_ready_lifecycle_smoke.js)
  - added a dedicated smoke script to print:
    - binding presence
    - script-enter state
    - `performNow` state
    - final `Java.ready(...)` state

Why this shape:

- the previous regression came from trying to infer a later lifecycle point only in JS
- this pass keeps the working `Java.ready(...)` path intact and only adds the missing native signal
- that makes the next device pass cheaper:
  - first prove the native bit toggles at the right spawn phase
  - only then decide whether `Java.ready(...)` should consume it

Regression coverage updated:

- [test_java_ready_object_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_java_ready_object_bridge.cpp)
  - now asserts:
    - `JsJavaIsLifecycleReady(...)` exists
    - `Java._isLifecycleReady` is exported
    - loader resolver exposes lifecycle-ready APIs
    - native lifecycle-ready storage exists

Verification completed:

- `g++ -std=c++17 tests\\headers\\test_java_ready_object_bridge.cpp -o build\\test_java_ready_object_bridge.exe`
- `.\\build\\test_java_ready_object_bridge.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\\build\\test_js_runtime_native_attach.exe`

Current status:

- native lifecycle-ready capture path now exists
- `Java.ready(...)` behavior is still the previously restored stable behavior
- next device validation should focus on whether `Java._isLifecycleReady()` flips to `true` during cold spawn before resume

## 2026-04-27 Spawn-only lifecycle gate wired into _isAppReady

Observed from device validation:

- cold-spawn lifecycle smoke now reports:
  - `java-ready-lifecycle-script-enter:false:true:true`
  - `java-ready-lifecycle-perform-now:true:true:true:true`
  - `java-ready-lifecycle-fired:true:true:true:true:true:com.demo.target.LoginFragment:function`
- this proves the native lifecycle-ready bit is already visible by the time the startup script begins executing

Follow-up goal:

- consume the stricter native lifecycle signal in the existing `_isAppReady()` path
- but only for processes that actually went through the spawn bootstrap hook
- keep normal attach behavior unchanged

Implementation:

- [java_hook_loader_resolver.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/java_hook/deferred/java_hook_loader_resolver.h)
  - added:
    - `void SetRequireApplicationLifecycleReady(bool required);`
- [java_hook_loader_resolver.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/java_hook/deferred/java_hook_loader_resolver.cpp)
  - added:
    - `g_require_application_lifecycle_ready`
  - `IsCurrentApplicationReady(JNIEnv* env)` now does:
    - if lifecycle-ready is required for this process:
      - return `IsApplicationLifecycleReady(env)`
    - otherwise:
      - keep the previous `ActivityThread.currentApplication()` behavior
- [NookComm.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookComm.cpp)
  - `InstallSpawnGateBootstrapHookIfNeededLocked()` now enables:
    - `JavaHookLoaderResolver::SetRequireApplicationLifecycleReady(true);`
  - this means only the spawn-gated app process switches `_isAppReady()` to the stricter native lifecycle bit

Why this is safer than the earlier JS experiment:

- the old regression came from installing JS hooks too late and then waiting on an event that could already be gone
- this version does not wait on JS hooks
- it reuses a native signal captured before the app is released from the spawn gate
- attach mode still keeps the older fallback semantics

Additional device smoke added:

- [java_ready_lifecycle_order_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_ready_lifecycle_order_smoke.js)
  - checks whether `Java.ready(...)` can run before any manual `performNow()` warm-up
  - reports callback ordering plus immediate app-class access

Verification completed:

- `g++ -std=c++17 tests\\headers\\test_java_ready_object_bridge.cpp -o build\\test_java_ready_object_bridge.exe`
- `.\\build\\test_java_ready_object_bridge.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\\build\\test_js_runtime_native_attach.exe`

Current expectation for the next device run:

- in cold spawn, `_isAppReady()` should now mean:
  - native lifecycle-ready observed
- `Java.ready(...)` immediate execution should no longer depend on a later JS-side bootstrap catch-up

Device validation result:

- cold-spawn order smoke produced:
  - `java-ready-lifecycle-order-script-enter:false:true:true`
  - `java-ready-debug:immediate`
  - `java-ready-lifecycle-order-callback:true:true:true:com.demo.target.LoginFragment:function`
  - `java-ready-lifecycle-order-perform-now:true:true:true`
  - `java-ready-lifecycle-order-seq:ready|after-ready|perform-now`

Interpretation:

- at startup script entry:
  - class-loader-ready is still `false`
  - `_isAppReady()` is already `true`
  - `_isLifecycleReady()` is already `true`
- `Java.ready(...)` fires immediately before any explicit `Java.performNow(...)` warm-up
- app-class resolution works directly inside that immediate callback

Conclusion:

- for the spawn-gated process, the native lifecycle-ready path now provides the stricter early-ready semantics we wanted
- this is materially closer to Frida's cold-spawn Java behavior than the previous JS-only fallback

## 2026-04-27 Java.array minimal parity slice

Goal:

- start closing one of the remaining Java high-level parity gaps with a small but real Frida-style helper
- support passing JS-created Java arrays into normal `Java.use(...).method(...)` calls
- keep the first slice intentionally narrow and verifiable

Scope chosen:

- support `Java.array(typeName, elements)` as a public JS helper
- support overload resolution and invoke for:
  - `int[]`
  - `java.lang.String[]`
- do not try to implement full JS-side mutable array proxy semantics yet
- do not claim generic object-array covariance yet, e.g. `String[] -> Object[]`

Why this scope:

- the existing Java wrapper model already handles real Java objects through `__jptr`
- the missing piece was a way to represent an array before it becomes a real JNI object
- adding a dedicated array value kind lets the invoke path:
  - infer overloads
  - lazily materialize the correct JNI array only when a concrete target signature is known

Implementation:

- [nook_java_js_bridge.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.h)
  - added:
    - `JavaJsValueKind::kArray`
    - `array_type_name`
    - `array_elements`
- [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp)
  - added `Java.array = function (typeName, elements) { ... }`
  - array helper returns a JS array carrying:
    - `__nookJavaArrayType`
    - `$className`
  - `ParseJavaJsValue(...)` now recognizes these tagged arrays and converts them into `JavaJsValueKind::kArray`
  - `CollectJavaInvokeArgumentTypeCandidates(...)` now forwards array type names such as:
    - `int[]`
    - `java.lang.String[]`
  - fixed a regression in Java object proxies by reserving:
    - `__nookJavaArrayType`
    - so unknown-property lookup on normal Java wrappers does not fake an array helper object
- [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp)
  - `TypeNameToDescriptor(...)` now accepts array type names:
    - `int[] -> [I`
    - `java.lang.String[] -> [Ljava/lang/String;`
    - descriptor-style `[I` passthrough
  - `ConvertJavaJsValueToNookJavaHookValue(...)` now materializes JNI arrays for array descriptors
  - first supported JNI creation paths:
    - `int[]`
    - object arrays with exact component descriptors such as `Ljava/lang/String;`

Regression coverage added:

- [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp)
  - `TestJavaArrayBindingExists()`
  - `TestJavaArrayOverloadSupportsArrayTypeNames()`
  - `TestJavaUseSupportsPrototypeNamedMethodOverload()`
  - `TestJavaInvokeInfersPrimitiveArrayOverload()`
  - `TestJavaInvokeInfersStringArrayOverload()`

Device-side root cause found during smoke:

- the first `java_array_smoke.js` failure was:
  - `error: not a function at <anonymous> (java_array_smoke.js:4:46)`
- that line was:
  - `Arrays.toString.overload("int[]")`
- root cause was not the array bridge itself
- root cause was the Java wrapper `Proxy` `get(...)` trap using:
  - `prop in target`
- for prototype-named Java methods like `toString`, that check matched `Object.prototype.toString`
- result:
  - the wrapper returned the built-in JS `toString`
  - so `.overload(...)` was missing
- fixed in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp) by switching to:
  - `Object.prototype.hasOwnProperty.call(target, prop)`
- this keeps inherited JS prototype names from shadowing real Java methods exposed lazily by the proxy

Second device-side root cause found after the proxy fix:

- after `Arrays.toString.overload("int[]")` started resolving correctly, device invoke still failed with:
  - `error: unsupported Java argument type in signature`
- root cause was in [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp):
  - `ParseTypeDescriptor(...)`
- that parser handled:
  - primitive descriptors
  - object descriptors like `Ljava/lang/String;`
- but it did not handle array descriptors beginning with `[`:
  - `([I)Ljava/lang/String;`
  - `([Ljava/lang/String;)Ljava/lang/String;`
- result:
  - overload resolution produced the correct exact signature
  - but the downstream invoke-side signature parser rejected it before JNI argument conversion
- fix:
  - teach `ParseTypeDescriptor(...)` to recurse on `[` and return the full array descriptor
  - also accept the missing primitive descriptor letters:
    - `B`
    - `C`
    - `S`

New regression added for the real failing path:

- [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp)
  - `TestJavaArrayDefaultInvokeParsesArraySignature()`
- purpose:
  - prove `Java.array('int', ...)` no longer dies at:
    - `unsupported Java argument type in signature`
  - and instead progresses to the next expected non-Android desktop failure:
    - `java method invoker is not configured`

Verification completed:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\\build\\test_js_runtime_native_attach.exe`
- `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
- pushed updated device binaries:
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libc++_shared.so /data/local/tmp/nook/libc++_shared.so`
 - device smoke passed:
   - `java-array-bindings:function:int[]:([I)Ljava/lang/String;`
   - `java-array-int-result:[1, 2, 3]`

Conclusion for this slice:

- `Java.array('int', [...])` is now working end-to-end on device
- overload resolution for `int[]` is working
- invoke-side signature parsing for array descriptors is working
- the first minimal Frida-style `Java.array(...)` parity slice is now real, not just desktop-only

## 2026-04-27 Java.array primitive-array expansion

Goal:

- extend the first `Java.array(...)` slice beyond `int[]`
- keep scope narrow and Frida-aligned
- validate both overload inference and JNI array creation for the most common primitive arrays

Scope in this pass:

- `boolean[]`
- `long[]`
- `float[]`
- `double[]`
- also expanded smoke coverage for `java.lang.String[]`

TDD flow:

- added new desktop regressions first in
  - [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp)
- new tests:
  - `TestJavaInvokeInfersBooleanArrayOverload()`
  - `TestJavaInvokeInfersLongArrayOverload()`
  - `TestJavaInvokeInfersFloatArrayOverload()`
  - `TestJavaInvokeInfersDoubleArrayOverload()`
- first run went red before the fake resolver/invoke coverage was updated
- then implemented the minimal production support and matching test harness coverage

Implementation:

- [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp)
  - `ConvertJavaJsArrayToJniArray(...)` now also materializes:
    - `boolean[] -> [Z`
    - `long[] -> [J`
    - `float[] -> [F`
    - `double[] -> [D`
  - conversion still delegates element coercion through:
    - `ConvertJavaJsValueToNookJavaHookValue(...)`
  - this keeps scalar coercion rules consistent between normal args and array elements

Smoke script expansion:

- [java_array_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_array_smoke.js)
  - now exercises:
    - `int[]`
    - `java.lang.String[]`
    - `boolean[]`
    - `long[]`
    - `float[]`
    - `double[]`
  - all routed through `java.util.Arrays.toString(...)` for stable device-visible output

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\\build\\test_js_runtime_native_attach.exe`
- `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
- `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
- pushed updated device binaries:
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook.so /data/local/tmp/nook/libnook.so`

Pending:

- final device smoke for the expanded `java_array_smoke.js`

## 2026-04-27 Java.array object-array covariance follow-up

Problem found on device:

- `java.util.Arrays.toString(strings)` still failed after the primitive-array expansion
- device error:
  - `ResolveJavaMethodSignature no method match class=java.util.Arrays method=toString static=true args=[java.lang.String[]]`

Root cause:

- `java.util.Arrays` does not provide an exact `toString(String[])` overload
- the real target overload is:
  - `toString(Object[])`
- Nook's resolver was still doing exact descriptor matching only
- after resolver support was expanded, invoke still failed one layer later because element conversion for:
  - `Object[] <- String[]`
  still tried to coerce each JS string as a Java wrapper for `java.lang.Object`

Fix:

- resolver-side argument matching now accepts reference-array covariance:
  - `String[] -> Object[]`
  - recursive array case such as `String[][] -> Object[][]`
- element conversion for object arrays now prefers the real source element descriptor when the target component descriptor is compatible
- this allows:
  - JS strings
  - to materialize as Java `String`
  - and then populate a target `Object[]`

Extra regression coverage:

- [test_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_java_js_bridge.cpp)
  - `TestJavaParameterDescriptorAcceptsArrayCovariance()`
  - now also covers:
    - `Ljava/lang/Object; <- Ljava/lang/String;`
    - `[Ljava/lang/Object; <- [Ljava/lang/String;`
    - `[[Ljava/lang/Object; <- [[Ljava/lang/String;`

Verification completed:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_java_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp -o build/test_java_js_bridge.exe`
- `.\\build\\test_java_js_bridge.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\\build\\test_js_runtime_native_attach.exe`
- `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
- pushed updated device binaries:
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook.so /data/local/tmp/nook/libnook.so`

Final device smoke passed:

- `java-array-bindings:function:int[]:([I)Ljava/lang/String;`
- `java-array-int-result:[1, 2, 3]`
- `java-array-string-result:[a, b]`
- `java-array-boolean-result:[true, false, true]`
- `java-array-long-result:[1, 2, 3]`
- `java-array-float-result:[1.25, 2.5]`
- `java-array-double-result:[1.25, 2.5]`

Conclusion:

- `Java.array(...)` now works end-to-end on device for:
  - `int[]`
  - `java.lang.String[]`
  - `boolean[]`
  - `long[]`
  - `float[]`
  - `double[]`
- resolver and invoke now cover the first practically important object-array covariance case:
  - `String[] -> Object[]`

Device smoke added:

- [java_array_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_array_smoke.js)
  - currently validates the most stable path:
    - `Java.array('int', [1, 2, 3])`
    - `java.util.Arrays.toString(int[])`

Current limitation after this pass:

- object-array support is exact-component for now
- assignability like `String[]` to `Object[]` is not handled yet
- no JS-side mutable live array wrapper semantics yet

## 2026-04-27 Java.array byte-short-char expansion

Goal:

- continue the minimal `Java.array(...)` parity slice
- add the next primitive arrays only:
  - `byte[]`
  - `short[]`
  - `char[]`
- keep `char[]` semantics intentionally narrow:
  - accept one JS string per element
  - each element must contain exactly one UTF-8 code point in the BMP

Root cause:

- the earlier array work covered:
  - `int[]`
  - `boolean[]`
  - `long[]`
  - `float[]`
  - `double[]`
  - `String[]`
- but JNI materialization in
  - [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp)
  still had no branches for:
  - `[B`
  - `[S`
  - `[C`
- scalar coercion in `ConvertJavaJsValueToNookJavaHookValue(...)` also had no support for:
  - `B`
  - `S`
  - `C`
- result:
  - JS could describe these arrays at the runtime layer
  - but Android JNI invoke could not materialize them into real Java arrays

Implementation:

- in [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp):
  - added scalar coercion for:
    - `B`
    - `S`
    - `C`
  - `byte` and `short` accept:
    - JS `int32`
    - integer-valued JS `double`
    - both with range checks
  - `char` accepts:
    - JS string containing exactly one UTF-8 code point
    - rejects invalid, multi-code-point, or surrogate values
  - added JNI array materialization for:
    - `NewByteArray` + `SetByteArrayRegion`
    - `NewShortArray` + `SetShortArrayRegion`
    - `NewCharArray` + `SetCharArrayRegion`

Smoke expansion:

- [java_array_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_array_smoke.js)
  now also exercises:
  - `Java.array("byte", [1, 2, 3])`
  - `Java.array("short", [1, 2, 3])`
  - `Java.array("char", ["a", "b"])`

Regression coverage:

- existing desktop attach regressions already covered overload inference for:
  - `byte[]`
  - `short[]`
  - `char[]`
- while validating this pass, one harness gap showed up:
  - fake `java.util.Arrays.toString(...)` handling in
    [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp)
    still only recognized the older array signatures
- fixed the fake invoker so the desktop regression path now also covers:
  - `([B)Ljava/lang/String;`
  - `([S)Ljava/lang/String;`
  - `([C)Ljava/lang/String;`

Local verification completed:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_java_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp -o build/test_java_js_bridge.exe`
- `.\\build\\test_java_js_bridge.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\\build\\test_js_runtime_native_attach.exe`
- `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`

Pending:

- push updated Android binaries
- run device smoke again
- append final device output for:
  - `java-array-byte-result`
  - `java-array-short-result`
  - `java-array-char-result`

Final device smoke passed:

- `java-array-bindings:function:int[]:([I)Ljava/lang/String;`
- `java-array-int-result:[1, 2, 3]`
- `java-array-string-result:[a, b]`
- `java-array-boolean-result:[true, false, true]`
- `java-array-byte-result:[1, 2, 3]`
- `java-array-short-result:[1, 2, 3]`
- `java-array-char-result:[a, b]`
- `java-array-long-result:[1, 2, 3]`
- `java-array-float-result:[1.25, 2.5]`
- `java-array-double-result:[1.25, 2.5]`

Conclusion for this slice:

- `Java.array(...)` now works end-to-end on device for:
  - `int[]`
  - `boolean[]`
  - `byte[]`
  - `short[]`
  - `char[]`
  - `long[]`
  - `float[]`
  - `double[]`
  - `java.lang.String[]`
- the practical covariance case also remains working:
  - `String[] -> Object[]`

## 2026-04-28 Java.array Object[] stabilization

Goal:

- continue `Java.array(...)` toward practical Frida parity
- make the direct target type
  - `java.lang.Object[]`
  stable for common JS values
- keep this pass narrow:
  - support `string`
  - support `boolean`
  - support numeric JS values through existing JS value kinds
  - support Java object wrappers
  - support `null`
  - do not expand to mutable live array wrappers or deep multi-dimensional semantics

Root cause:

- earlier work already handled:
  - exact `String[]`
  - primitive arrays
  - covariance into `Object[]` when the source array was already more specific, such as:
    - `String[] -> Object[]`
- but direct creation of:
  - `Java.array("java.lang.Object", [...])`
  still fell into the generic reference-object path in
  [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp)
- that path only accepted:
  - Java object wrappers
  - or pre-typed arrays
- plain JS values like:
  - string
  - boolean
  - number
  therefore had no boxing path when the target descriptor was exactly:
  - `Ljava/lang/Object;`

Implementation:

- in [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp):
  - added a dedicated `Ljava/lang/Object;` conversion branch in
    - `ConvertJavaJsValueToNookJavaHookValue(...)`
  - behavior in this pass:
    - `undefined` -> `null`
    - Java wrapper object -> passthrough object handle
    - JS array with `array_type_name` -> materialize nested Java array object
    - JS string -> `jstring`
    - JS boolean -> boxed `java.lang.Boolean`
    - JS numeric kinds -> boxed numeric wrapper through existing helper path
- reused the already existing boxing machinery that was previously only used by the
  `registerClass(...)` callback return path

Regression coverage:

- [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp)
  - added:
    - `TestJavaInvokeInfersObjectArrayOverload()`
  - verifies:
    - `Java.array("java.lang.Object", ["a", true, 2.5])`
    - resolves to `([Ljava/lang/Object;)Ljava/lang/String;`
    - preserves JS-side array typing as:
      - `java.lang.Object[]`
- also expanded the fake `Arrays.toString(...)` formatter so generic object-array tests can
  render:
  - booleans
  - `int64`
  - `float`
  - `double`

Smoke added:

- [java_array_object_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_array_object_smoke.js)
  - exercises:
    - `Java.array("java.lang.Object", ["a", true, 2.5])`
    - `java.util.Arrays.toString(Object[])`

Local verification completed:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_java_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp -o build/test_java_js_bridge.exe`
- `.\\build\\test_java_js_bridge.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\\build\\test_js_runtime_native_attach.exe`
- `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
- pushed updated Android binaries:
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook.so /data/local/tmp/nook/libnook.so`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libc++_shared.so /data/local/tmp/nook/libc++_shared.so`

Pending:

- final device smoke for:
  - `java-array-object-bindings`
  - `java-array-object-result`

Final device smoke passed:

- `java-ready-debug:immediate`
- `java-array-object-bindings:function:java.lang.Object[]`
- `java-array-object-result:[a, true, 2.5]`

Conclusion for this slice:

- direct `Java.array("java.lang.Object", [...])` now works end-to-end on device for the
  current minimal stable subset:
  - `string`
  - `boolean`
  - numeric JS values
  - Java wrapper objects
  - `null`
- this closes the gap between:
  - covariance into `Object[]`
  - and direct construction of `Object[]`

## 2026-04-28 Java.array multidimensional arrays

Goal:

- continue the minimal Frida-style `Java.array(...)` expansion with the next practical step:
  - `int[][]`
  - `java.lang.String[][]`
- keep scope narrow:
  - no mutable live array wrapper semantics
  - no arbitrary-depth stress pass in this slice
  - just prove nested JS `Java.array(...)` values can become real Java multidimensional arrays

Root cause:

- JNI materialization in
  [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp)
  already handled:
  - primitive arrays like `[I`
  - object arrays like `[Ljava/lang/String;`
- but it still had no branch for:
  - array-of-array component descriptors such as:
    - `[[I`
    - `[[Ljava/lang/String;`
  more precisely, there was no `component_descriptor.front() == '['` path when building the
  outer array object
- a second issue existed one layer earlier in
  [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - `ParseJavaJsValue(...)` stored `array_type_name` from `__nookJavaArrayType`
  - that field contains the element type originally passed to `Java.array(typeName, ...)`
  - for nested construction like:
    - `Java.array("int[]", [row1, row2])`
    the parsed runtime type therefore became:
    - `int[]`
    instead of the real array type:
    - `int[][]`
- result:
  - overload inference for nested arrays was wrong
  - and JNI array-of-array creation had no matching conversion branch

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - `ParseJavaJsValue(...)` now prefers `$className` when reconstructing `array_type_name`
  - this preserves the real array type for nested values, for example:
    - `int[][]`
    - `java.lang.String[][]`
- in [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp):
  - `ConvertJavaJsArrayToJniArray(...)` now handles component descriptors whose first byte is
    - `[`
  - it resolves the component array class and builds the outer `jobjectArray`
  - each nested JS array element is then recursively materialized through
    - `ConvertJavaJsValueToNookJavaHookValue(...)`

Regression coverage:

- [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp)
  - added:
    - `TestJavaInvokeInfersInt2dArrayOverload()`
    - `TestJavaInvokeInfersString2dArrayOverload()`
  - both validate:
    - nested `Java.array(...)` construction
    - runtime type names:
      - `int[][]`
      - `java.lang.String[][]`
    - overload resolution through:
      - `java.util.Arrays.deepToString(Object[])`
- the desktop fake formatter was also extended to recurse on nested `JavaJsValueKind::kArray`

Smoke added:

- [java_array_multi_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_array_multi_smoke.js)
  - exercises:
    - `Java.array("int[]", [Java.array("int", ...), ...])`
    - `Java.array("java.lang.String[]", [Java.array("java.lang.String", ...), ...])`
    - `java.util.Arrays.deepToString(...)`

Local verification completed:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_java_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp -o build/test_java_js_bridge.exe`
- `.\\build\\test_java_js_bridge.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\\build\\test_js_runtime_native_attach.exe`
- `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
- pushed updated Android binaries:
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook.so /data/local/tmp/nook/libnook.so`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libc++_shared.so /data/local/tmp/nook/libc++_shared.so`

Pending:

- final device smoke for:
  - `java-array-multi-bindings`
  - `java-array-multi-int-result`
  - `java-array-multi-string-result`

Final device smoke passed:

- `java-ready-debug:immediate`
- `java-array-multi-bindings:function:int[][]:java.lang.String[][]`
- `java-array-multi-int-result:[[1, 2], [3, 4]]`
- `java-array-multi-string-result:[[a, b], [c]]`

Conclusion for this slice:

- `Java.array(...)` now supports direct multidimensional construction on device for the current
  minimal stable subset:
  - `int[][]`
  - `java.lang.String[][]`
- nested `Java.array(...)` values now preserve their real runtime array type names, which keeps
  overload resolution and JNI materialization aligned for array-of-array paths

## 2026-04-28 Java.array Object[][] confirmation

Goal:

- verify whether the already-landed:
  - `java.lang.Object[]`
  - multidimensional array
  support is sufficient for direct:
  - `java.lang.Object[][]`
- avoid unnecessary production changes if the current bridge already covers this path

Result:

- no production bridge changes were needed in this step
- added a focused desktop regression and a device smoke only

Regression coverage:

- [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp)
  - added:
    - `TestJavaInvokeInfersObject2dArrayOverload()`
  - verifies:
    - `Java.array("java.lang.Object[]", [...])`
    - nested rows built by `Java.array("java.lang.Object", [...])`
    - runtime type name:
      - `java.lang.Object[][]`
    - `java.util.Arrays.deepToString(...)` path

Smoke added:

- [java_array_object_multi_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_array_object_multi_smoke.js)
  - exercises:
    - `Java.array("java.lang.Object[]", [row1, row2])`
    - `row1/row2` built from `Java.array("java.lang.Object", [...])`
    - `java.util.Arrays.deepToString(...)`

Verification completed:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\\build\\test_js_runtime_native_attach.exe`

Final device smoke passed:

- `java-array-object-multi-bindings:function:java.lang.Object[][]`
- `java-array-object-multi-result:[[a, true], [2.5, b]]`

Conclusion:

- the previously landed `Object[]` boxing path and multidimensional-array JNI path were already
  sufficient for:
  - `java.lang.Object[][]`
- `Java.array(...)` now also has verified device coverage for:
  - `java.lang.Object[][]`

## 2026-04-28 Java.array boolean-byte multidimensional confirmation

Goal:

- extend the multidimensional smoke coverage to the next practical primitive cases:
  - `boolean[][]`
  - `byte[][]`
- verify whether the already-landed multidimensional JNI path generalizes cleanly to these
  primitive-array rows without further production changes

Result:

- no production bridge changes were needed in this step
- added focused desktop regression coverage and expanded the existing multidimensional smoke

Regression coverage:

- [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp)
  - added:
    - `TestJavaInvokeInfersBoolean2dArrayOverload()`
    - `TestJavaInvokeInfersByte2dArrayOverload()`
  - verifies:
    - `Java.array("boolean[]", [row1, row2])`
    - `Java.array("byte[]", [row1, row2])`
    - runtime type names:
      - `boolean[][]`
      - `byte[][]`
    - `java.util.Arrays.deepToString(...)` resolution path

Smoke expansion:

- [java_array_multi_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_array_multi_smoke.js)
  now also exercises:
  - `boolean[][]`
  - `byte[][]`

Verification completed:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\\build\\test_js_runtime_native_attach.exe`

Final device smoke passed:

- `java-array-multi-int-result:[[1, 2], [3, 4]]`
- `java-array-multi-boolean-result:[[true, false], [false, true]]`
- `java-array-multi-byte-result:[[1, 2], [3, 4]]`
- `java-array-multi-string-result:[[a, b], [c]]`

Conclusion:

- the existing multidimensional-array implementation also covers:
  - `boolean[][]`
  - `byte[][]`
- `Java.array(...)` now has verified device coverage for multiple primitive and reference 2D
  array cases, not just `int[][]` and `String[][]`

## 2026-04-28 Java.array remaining primitive 2D confirmation

Goal:

- finish the 2D primitive-array smoke matrix for the remaining cases:
  - `short[][]`
  - `char[][]`
  - `long[][]`
  - `float[][]`
  - `double[][]`
- confirm whether the already-landed multidimensional JNI path generalizes without further
  production work

Result:

- no production bridge changes were needed in this step
- added focused desktop regression coverage and expanded the shared multidimensional smoke

Regression coverage:

- [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp)
  - added:
    - `TestJavaInvokeInfersShort2dArrayOverload()`
    - `TestJavaInvokeInfersChar2dArrayOverload()`
    - `TestJavaInvokeInfersLong2dArrayOverload()`
    - `TestJavaInvokeInfersFloat2dArrayOverload()`
    - `TestJavaInvokeInfersDouble2dArrayOverload()`
  - verifies:
    - nested `Java.array(...)` construction for each remaining primitive 2D case
    - runtime type names:
      - `short[][]`
      - `char[][]`
      - `long[][]`
      - `float[][]`
      - `double[][]`
    - `java.util.Arrays.deepToString(...)` resolution path

Smoke expansion:

- [java_array_multi_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_array_multi_smoke.js)
  now also exercises:
  - `short[][]`
  - `char[][]`
  - `long[][]`
  - `float[][]`
  - `double[][]`

Verification completed:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\\build\\test_js_runtime_native_attach.exe`

Final device smoke passed:

- `java-array-multi-bindings:function:int[][]:java.lang.String[][]`
- `java-array-multi-int-result:[[1, 2], [3, 4]]`
- `java-array-multi-boolean-result:[[true, false], [false, true]]`
- `java-array-multi-byte-result:[[1, 2], [3, 4]]`
- `java-array-multi-short-result:[[1, 2], [3, 4]]`
- `java-array-multi-char-result:[[a, b], [c]]`
- `java-array-multi-long-result:[[1, 2], [3, 4]]`
- `java-array-multi-float-result:[[1.25, 2.5], [3.75]]`
- `java-array-multi-double-result:[[1.25, 2.5], [3.75]]`
- `java-array-multi-string-result:[[a, b], [c]]`

Conclusion:

- the current multidimensional-array implementation also covers:
  - `short[][]`
  - `char[][]`
  - `long[][]`
  - `float[][]`
  - `double[][]`
- `Java.array(...)` now has verified device coverage for the full current 2D primitive set, plus:
  - `String[][]`
  - `Object[][]`

## 2026-04-28 Java.array error-message context

Goal:

- improve engineering quality for `Java.array(...)` conversion failures
- when an array element fails to convert, include:
  - the array type
  - the failing element index
  - nested array context when applicable

Root cause:

- before this pass, element conversion failures bubbled up only with the leaf message, for example:
  - `Java int expects JS int32`
- that message was technically correct but incomplete:
  - it did not identify which array failed
  - it did not identify which element failed
  - nested cases like `int[][]` lost the full error path

Implementation:

- in [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp):
  - added array-element error formatting helper
  - all `ConvertJavaJsArrayToJniArray(...)` element-conversion loops now wrap nested failures as:
    - `Java array <type> element[<index>]: <nested error>`
- also exported a tiny testing helper so the formatting itself can be regression-tested in:
  [test_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_java_js_bridge.cpp)

Regression coverage:

- [test_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_java_js_bridge.cpp)
  - added:
    - `TestFormatJavaArrayElementErrorIncludesArrayNameAndIndex()`
  - verifies both:
    - one-dimensional formatting
    - nested two-dimensional formatting

Device smoke added:

- [java_array_error_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_array_error_smoke.js)
  - exercises:
    - `Java.array("int", [1, "x"])`
    - nested `Java.array("int[]", [Java.array("int", [1, "x"])])`

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_java_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp -o build/test_java_js_bridge.exe`
- `.\\build\\test_java_js_bridge.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\\build\\test_js_runtime_native_attach.exe`
- `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
- pushed updated Android binaries:
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook.so /data/local/tmp/nook/libnook.so`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libc++_shared.so /data/local/tmp/nook/libc++_shared.so`

Device validation note:

- the first retry still showed the old short error text because the running
  `nook-server` / target process had not reloaded the updated native libraries yet
- after restarting both the server and target process, the new messages appeared as expected

Final device smoke passed:

- `java-ready-debug:immediate`
- `java-array-error-int:Java array int[] element[1]: Java int expects JS int32`
- `java-array-error-int2d:Java array int[][] element[0]: Java array int[] element[1]: Java int expects JS int32`

Conclusion:

- `Java.array(...)` failures now preserve enough path context to identify the exact failing element
- nested array conversion errors now remain debuggable on device without guessing which row or
  element triggered the failure

## 2026-04-28 Java instance wrapper $dispose()

Goal:

- close a small but important Frida-compatibility gap in Java wrapper lifecycle handling
- add explicit disposal for owned Java object wrappers through:
  - `wrapper.$dispose()`

Problem:

- Nook already had:
  - `Java.retain(obj)`
  - `Java.cast(obj, Klass)`
  - `Java.choose(...)`
  - constructor / method return wrappers backed by retained global refs
- but there was no explicit release path for wrappers that owned a retained/global handle
- this left a mismatch with Frida's explicit instance-wrapper cleanup flow

Implementation:

- in [nook_java_js_bridge.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.h):
  - added `ReleaseJavaObjectFn`
  - extended `JavaJsHookInstallerDependencies` with `release_object`
  - declared `ReleaseJavaObject(...)`
- in [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp):
  - added `DefaultReleaseJavaObject(...)`
  - on Android, it releases the retained handle with `DeleteGlobalRef(...)`
  - added the public `ReleaseJavaObject(...)` helper beside `RetainJavaObject(...)`
- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - added internal JS bridge function:
    - `__nookJavaRelease(...)`
  - extended `CreateJavaUseWrapper(...)` with an explicit ownership bit
  - object wrappers created from retained/global handles are now marked with:
    - `__nookJavaOwnedHandle = true`
  - class wrappers returned by `Java.use(...)` remain non-owning
  - Java wrapper objects now expose:
    - `$dispose()`

`$dispose()` semantics in this pass:

- only owned wrappers release
- repeated calls are idempotent
- disposal clears:
  - `__nookJavaReceiverHandle`
  - `__jptr`
  - `__nookJavaOwnedHandle`
- this pass intentionally does not add:
  - automatic GC cleanup
  - script-unload bulk disposal
  - wrapper refcounting across multiple casted views of the same handle

Important design boundary:

- this pass only guarantees explicit disposal for wrappers that Nook itself knows are owned
- `Java.cast(...)` still re-wraps the same underlying handle without creating a second retained ref
- that keeps this change narrow and avoids accidental double-release semantics

Regression coverage:

- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added:
    - `TestJavaDisposeReleasesOwnedRetainedHandleOnce()`
- the test verifies:
  - retained wrappers expose `$dispose()`
  - owned state starts as `true`
  - repeated `$dispose()` only calls the release dependency once
  - disposal clears the wrapper handle state to `0x0`

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\\build\\test_js_runtime_native_attach.exe`

Current boundary after this pass:

- explicit disposal for owned Java instance wrappers is now in place
- this is the Frida-aligned first step
- automatic cleanup remains a separate follow-up

## 2026-04-28 Java owned wrapper automatic cleanup on unload/shutdown

Goal:

- close the follow-up lifecycle gap left after `wrapper.$dispose()`
- automatically release owned Java wrapper handles when scripts or the runtime are torn down

Problem:

- after the previous pass, Nook had explicit disposal through:
  - `wrapper.$dispose()`
- but if a script retained a Java object and never disposed it:
  - `ScriptRegistry::UnloadScript(...)`
  - `ScriptRegistry::Clear()`
  - `JsRuntime::Shutdown()`
  did not release the owned retained/global ref
- this meant runtime teardown already cleaned several native resources, but not owned Java wrapper handles

Root cause:

- `CreateJavaUseWrapper(...)` knew whether a wrapper owned a handle through `owns_handle`
- but `JsRuntime` did not persist that ownership anywhere outside the wrapper object itself
- teardown paths therefore had no way to find leftover owned Java handles and release them

Implementation:

- added two new docs:
  - [2026-04-28-nook-java-wrapper-auto-cleanup-design.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-04-28-nook-java-wrapper-auto-cleanup-design.md)
  - [2026-04-28-nook-java-wrapper-auto-cleanup-implementation-plan.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-04-28-nook-java-wrapper-auto-cleanup-implementation-plan.md)
- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - extended `RuntimeState` with:
    - `owned_java_handles`
  - added helpers to:
    - register an owned handle under the current `script_id`
    - unregister a handle on explicit release
    - bulk-release all remaining owned handles for one script
  - `CreateJavaUseWrapper(...)` now registers owned handles when an owning wrapper is created successfully
  - `__nookJavaRelease(...)` now unregisters the handle after successful explicit release
  - `JsRuntime::RemoveMessageHandler(...)` now bulk-releases owned Java handles before dropping per-script state
  - `JsRuntime::Shutdown()` now bulk-releases remaining owned Java handles before clearing Java bridge dependencies

Cleanup semantics in this pass:

- tracking is per script
- duplicate tracking of the same handle in one script collapses to one release
- explicit `$dispose()` removes the handle from tracking
- unload/shutdown therefore do not double-release handles already explicitly disposed
- shutdown cleanup is best-effort, matching the rest of the runtime teardown style

Regression coverage:

- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added:
    - `TestJavaOwnedHandleCleanupOnUnloadReleasesRetainedHandle()`
    - `TestJavaOwnedHandleCleanupOnRegistryClearReleasesRetainedHandle()`
    - `TestJavaOwnedHandleCleanupOnShutdownReleasesRetainedHandle()`
    - `TestJavaOwnedHandleCleanupAfterExplicitDisposeDoesNotDoubleRelease()`
- these tests verify:
  - unload releases a retained handle once
  - `registry.Clear()` releases a retained handle once
  - runtime shutdown releases a retained handle once
  - explicit `$dispose()` still prevents unload from releasing the same handle again

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\\build\\test_js_runtime_native_attach.exe`

Current boundary after this pass:

- owned Java wrappers now clean up automatically on unload and runtime shutdown
- cleanup still only applies to wrappers Nook explicitly marks as owning a retained/global handle
- QuickJS finalizers / GC-driven cleanup remain intentionally out of scope

## 2026-04-28 Wait-mode CLI cleanup for Java auto-release verification

Goal:

- make `attach --wait` / `spawn --wait` actually trigger script unload on `Ctrl+C`
- unblock real device verification of Java owned-wrapper auto cleanup

Observed device symptom:

- `java_auto_cleanup_diag.js` could retain an owned Java handle on device
- but `adb logcat -d -s NookCommApi` only showed:
  - `script create ok`
  - `script load ok`
- it did not show:
  - `java auto cleanup begin ...`
  - `java auto cleanup released ...`

Root cause:

- the native runtime cleanup was already implemented correctly
- the missing piece was host-side CLI behavior in wait mode
- in [cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/cli.py):
  - `_wait_for_messages(...)` returned immediately on `KeyboardInterrupt`
  - `attach --wait` and `spawn --wait` then exited without calling `script.unload()`
- because unload never happened, device-side runtime teardown for that script never ran

Implementation:

- in [cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/cli.py):
  - added `_cleanup_wait_script(...)`
  - wrapped both `spawn --wait` and `attach --wait` message loops in `try/finally`
  - the `finally` path now:
    - joins the interactive post thread if present
    - calls `script.unload()`
    - prints `script unload ok: script_id=...` in text mode

Regression coverage:

- in [test_cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_cli.py):
  - strengthened `test_spawn_command_waits_and_prints_messages_until_interrupted()`
    - now asserts the script is unloaded
    - now asserts `script unload ok: script_id=1000`
  - added `test_attach_command_waits_and_unloads_script_on_interrupt()`

Verification completed locally:

- `python -m unittest host.nook-py.tests.test_cli`

Final device verification:

- loaded [java_auto_cleanup_diag.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_auto_cleanup_diag.js)
- retained one owned handle on device
- interrupted `attach --wait` with `Ctrl+C`
- observed:
  - `java auto cleanup begin script_id=1 count=1`
  - `java auto cleanup released script_id=1 handle=0x2eba`
  - `script unload ok script_id=1`

Additional spawn verification:

- re-ran the same diagnostic through `spawn --resume --wait`
- observed ready-state progression:
  - `java-auto-cleanup-script-enter:false:true`
  - `java-auto-cleanup-perform-now:true:true:true`
  - `java-auto-cleanup-ready-fired:true:true:true:true`
  - `java-auto-cleanup-retained:true:0x2ed2`
- interrupted wait mode with `Ctrl+C`
- observed:
  - `java auto cleanup begin script_id=1 count=4`
  - `java auto cleanup released script_id=1 handle=0x2ed2`
  - `java auto cleanup released script_id=1 handle=0x2d86`
  - `java auto cleanup released script_id=1 handle=0x2d76`
  - `java auto cleanup released script_id=1 handle=0x2d5a`
  - `script unload ok script_id=1`

Interpretation of `count=4`:

- this is expected for the diagnostic script
- besides the deliberately retained `TextFragment` wrapper, the script also materializes additional owned Java object wrappers during ready/bootstrap diagnostics, such as the current application and class loader
- automatic cleanup correctly released all remaining owned handles on unload, not just the explicitly retained fragment wrapper

Conclusion:

- Java owned-wrapper automatic cleanup is now verified end-to-end on device
- the native cleanup path and the host CLI unload path now line up correctly in wait mode

## 2026-04-28 Frida-aligned direction change: Script.bindWeak before Java finalizers

Decision:

- after validating explicit disposal and unload/shutdown cleanup, the next Frida-aligned step should not start from a Java-only QuickJS finalizer
- the better reference-aligned direction is to add a generic script lifecycle primitive first:
  - `Script.bindWeak(...)`
  - `Script.unbindWeak(...)`
  - optionally later:
    - `Script.pin()`
    - `Script.unpin()`

Reasoning:

- Frida's public lifecycle model is centered on script-level weak binding semantics
- Java wrapper GC cleanup is better modeled as a consumer of that generic primitive
- Nook's current Java wrappers are pure JS `Proxy`/plain objects, so a direct "just add a finalizer" approach would be both less Frida-like and structurally awkward

Design outcome:

- added:
  - [2026-04-28-nook-script-bindweak-design.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-04-28-nook-script-bindweak-design.md)
  - [2026-04-28-nook-script-bindweak-implementation-plan.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-04-28-nook-script-bindweak-implementation-plan.md)
- kept the earlier GC/finalizer docs as implementation-background material, not the primary API direction:
  - [2026-04-28-nook-java-wrapper-gc-finalizer-design.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-04-28-nook-java-wrapper-gc-finalizer-design.md)
  - [2026-04-28-nook-java-wrapper-gc-finalizer-implementation-plan.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/plans/2026-04-28-nook-java-wrapper-gc-finalizer-implementation-plan.md)

Practical implication:

- the next implementation step, if we continue, should be:
  - generic `Script.bindWeak(...)` runtime support
- Java GC-driven wrapper cleanup should then be implemented on top of that primitive instead of inventing a Java-only lifetime path

## 2026-04-28 Hidden script GC hook for weak-binding verification

Goal:

- close the last device-side verification gap for `Script.bindWeak(...)`
- make Java owned-wrapper auto cleanup observable without depending on nondeterministic QuickJS GC timing

Observed gap:

- desktop already had `JsRuntimeRunGcForTesting()`
- device scripts could retain Java wrappers and attach weak bindings successfully
- but on device there was no script-callable way to force QuickJS GC, so:
  - `Java.retain(...)` weak cleanup could only be inferred from unload-time cleanup
  - natural GC timing was too unstable for a reliable smoke test

Root cause:

- the runtime exposed a test-only GC helper only through C++ test API
- the `Script` global exposed:
  - `bindWeak`
  - `unbindWeak`
- but not a script-side GC trigger

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - added hidden `Script._runGcForTesting()`
  - implementation mirrors `JsRuntimeRunGcForTesting()`
  - each invocation runs multiple `JS_RunGC(...)` passes and drains pending weak-binding maintenance
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - enabled script-visible regression coverage for:
    - `typeof Script._runGcForTesting === "function"`
    - `Script._runGcForTesting()` firing a pending weak binding
- added device diagnostic script:
  - [java_auto_cleanup_gc_diag.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_auto_cleanup_gc_diag.js)
  - this script:
    - retains a `TextFragment` wrapper once
    - binds an extra visible weak callback to that wrapper
    - exports `rpc.exports.gc()` so device tests can force QuickJS GC and observe the callback

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `build\\test_js_runtime_native_attach.exe`

Expected device verification flow:

- load [java_auto_cleanup_gc_diag.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_auto_cleanup_gc_diag.js)
- trigger one `TextFragment.initView(...)`
- call exported `gc()` from CLI / REPL
- observe:
  - `java-auto-cleanup-gc-retained:...`
  - `java-auto-cleanup-gc-fired:1`

Conclusion:

- weak-binding verification no longer depends on incidental GC timing
- Nook now has the missing Frida-style building block needed to prove wrapper weak cleanup end-to-end on device

## 2026-04-28 Frida-aligned `Script.unbindWeak(...)` semantics

Goal:

- align Nook's public `Script.unbindWeak(...)` behavior with Frida's documented semantics
- preserve exact-once Java retained-wrapper release after that semantic change

Observed mismatch:

- Nook's first public implementation treated `Script.unbindWeak(token)` as a silent cancellation primitive
- Frida's documented behavior is different:
  - stop monitoring the bound value
  - invoke the callback immediately

Why this mattered:

- this was no longer just an internal detail; it was a public behavior mismatch in an API we explicitly introduced for Frida parity
- simply changing `unbindWeak(...)` was not enough, because Java retained wrappers were using it during `$dispose()`
- if left unchanged, `$dispose()` would trigger the weak callback immediately and double-release the owned Java handle

Implementation:

- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - changed the generic weak-binding regression so `unbindWeak(...)` must fire the callback immediately
- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - changed `Script.unbindWeak(...)` to:
    - unregister the weak binding from `FinalizationRegistry`
    - dispatch the bound callback immediately
    - return `true` when an active binding was found
  - reworked Java retained-wrapper weak cleanup so the weak callback no longer captures the wrapper itself
  - introduced a small wrapper-owned weak state object instead:
    - keeps the retained handle
    - tracks whether `$dispose()` is already in progress
    - lets explicit `$dispose()` suppress the immediate weak callback without breaking GC/unload cleanup
- updated [script_bindweak_unload_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/script_bindweak_unload_smoke.js) to reflect the new public behavior

Root cause of the intermediate failure:

- the first attempt at protecting Java `$dispose()` made the weak callback capture the JS wrapper directly
- that accidentally created a strong reference path from the runtime's stored callback back to the target wrapper
- result:
  - weak GC cleanup could no longer collect the wrapper naturally
  - desktop unload-path tests hit a QuickJS shutdown assertion
- switching to a separate weak-state object fixed that without regressing exact-once release

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `cmd /c build\\test_js_runtime_native_attach.exe`

## 2026-04-29 Java.registerClass unsupported spec fail-fast

Goal:

- stop silently accepting Frida-style `registerClass` spec members that Nook's
  current proxy architecture does not actually support

Why this mattered:

- current Nook `Java.registerClass(...)` already supports:
  - interface/listener proxies
  - declaration objects
  - declaration arrays
  - signature-aware callback dispatch
- but it still creates:
  - `Proxy.newProxyInstance(...)`
  - helper `InvocationHandler`
- under that architecture, silently accepting:
  - `spec.fields`
  - `spec.superClass`
  would be misleading because those semantics are not real

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - tightened `Java.registerClass(spec)` bootstrap validation
  - now throws when:
    - `spec.fields` is present
    - `spec.superClass` is present
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added regression coverage proving:
    - `fields` rejects with a clear error
    - `superClass` rejects with a clear error
- added convenience smoke:
  - [java_register_class_spec_boundary_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_register_class_spec_boundary_smoke.js)

Design conclusion:

- this is intentionally stricter than silent ignore behavior
- it does not reduce real capability
- it improves user expectation management and keeps Nook honest about the gap
  relative to full Frida `registerClass`

Current boundary after this pass:

- supported:
  - interface-style proxy registration
  - method declarations
  - multi-signature callback dispatch
- explicitly unsupported:
  - `fields`
  - `superClass`
  - full dynamic subclass generation

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `cmd /c build\\test_js_runtime_native_attach.exe`

## 2026-04-29 Java.array generic reference-array covariance follow-up

Goal:

- continue the remaining honest `Java.array(...)` Frida-alignment work from `step6.md`
- extend reference-array covariance beyond the earlier `String[] -> Object[]` special case
- avoid drifting into fake `registerClass.fields` semantics under the current proxy architecture

Why this next step instead of `registerClass.fields`:

- current `Java.registerClass(...)` is still implemented through:
  - `Proxy.newProxyInstance(...)`
  - `InvocationHandler`
- that architecture is good enough for interface/listener callbacks
- but it is not a real dynamic Java class model
- adding `fields` there would either:
  - create fake semantics
  - or require a much larger architecture change
- by contrast, `Java.array(...)` still had a real remaining gap from `step6.md`:
  - richer reference-array covariance

Problem:

- the bridge already handled:
  - exact reference-array conversion
  - the practical special case `String[] -> Object[]`
- but it still did not generally widen more specific source arrays into assignable reference-array targets like:
  - `String[] -> CharSequence[]`
- that meant overload resolution and invocation were no longer aligned in the broader reference-array case

Fix:

- added a small internal helper in:
  - [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp)
- the helper now selects the element descriptor used during array materialization by:
  - starting from the target array component descriptor
  - deriving the source array descriptor from `value.array_type_name`
  - reusing reflective Java assignability when the source component is more specific but still assignable to the target component
- updated the reference-array branch in `ConvertJavaJsArrayToJniArray(...)` to use that helper instead of only the old `Object[]`-style widening shortcut

What this now enables:

- `String[] -> CharSequence[]`
- `String[][] -> CharSequence[][]`
- previously working cases like:
  - `String[] -> Object[]`
  - `String[][] -> Object[][]`
  remain intact

Test coverage added:

- [test_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_java_js_bridge.cpp)
  - `TestChooseJavaArrayElementDescriptorSupportsGenericReferenceCovariance()`
  - covers:
    - `[Ljava/lang/CharSequence; <- java.lang.String[]`
    - `[[Ljava/lang/CharSequence; <- java.lang.String[][]`
- existing larger runtime coverage remains green in:
  - [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp)

Device smoke added:

- [java_array_reference_covariance_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_array_reference_covariance_smoke.js)

Device validation result:

- `java-array-ref-cov-bindings:function:([Ljava/lang/CharSequence;)Ljava/lang/CharSequence;`
- `java-array-ref-cov-result:ab:java.lang.String[]`

This confirms:

- the target overload expected `CharSequence[]`
- the script-provided source array remained `java.lang.String[]`
- Nook now widened the reference array through real assignability instead of only exact match or `Object[]` special-casing

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_java_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp -o build/test_java_js_bridge.exe`
- `.\build\test_java_js_bridge.exe`
- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\build\test_js_runtime_native_attach.exe`
- `E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`

Current boundary kept explicit:

- this pass still does not add:
  - fake `registerClass.fields`
  - local refs
  - local frames
  - revived `monitorEnter/monitorExit`

## 2026-04-29 Java reference-type overload specificity

Goal:

- keep pushing Nook's default Java direct-invoke path toward Frida
- fix the remaining gap where multiple reference overloads matched, but Nook still treated them as ambiguous unless the descriptors were identical

What changed:

- in [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp):
  - extended reflected overload matching so reference parameters may use real Java assignability, not only exact descriptor equality plus the old `Object` shortcut
  - added conservative "strictly more specific" comparison across matching reference overloads
  - when multiple candidates match:
    - choose a winner only if one signature is strictly more specific than every other match
    - otherwise keep the result ambiguous
  - kept the existing boundary:
    - no synthetic numeric scoring engine
    - no JS-side superclass/interface guessing
- in [nook_java_js_bridge.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.h):
  - added small testing-only helpers for:
    - reference-specificity comparison
    - most-specific overload selection
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added host coverage for:
    - `String` more specific than `Object`
    - `List` more specific than `Object`
    - unrelated references such as `String` vs `Integer` staying ambiguous
    - cross-parameter mixed-specificity staying ambiguous
    - `null` preferring `String` over `Object` when both are valid reference targets

Why this matters:

- the previous pass already improved direct invoke candidate generation for:
  - `null`
  - boxed values
  - `Object` / `Number` fallbacks
- but the native resolver still stopped at:
  - "all matches are equal unless exact descriptors differ in a trivial way"
- this pass moves the real type-relationship decision into the reflected resolver, which is much closer to Frida's model

Boundary kept explicit:

- this is still not a full Frida-grade overload scorer
- if assignability cannot prove a unique strict winner, Nook still returns ambiguity
- no `Env` architecture changes were introduced in this pass

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/agent_runtime/nook_java_js_bridge.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\\build\\test_js_runtime_native_attach.exe`

### Follow-up fix: JNI assignability descriptor normalization

Device smoke for the new reference-specificity path exposed a separate production-only bug:

- exact overload resolution on framework instance methods like `StringBuilder.append.overload("java.lang.String")` still failed on device
- the user-facing error surfaced as a fallback static-resolution miss, which masked the real instance-side problem

Root cause:

- the new reflected assignability path compared reference descriptors such as:
  - `Ljava/lang/String;`
  - `Ljava/lang/Object;`
- but then passed those raw object descriptors into `ResolveJavaClass(...)`
- JNI class lookup expects object class names like:
  - `java.lang.String`
  - `java.lang.Object`
- array descriptors may stay descriptor-shaped, but object descriptors must be normalized first

Fix:

- normalized object descriptors before JNI class resolution inside the reflected assignability helper
- kept array descriptors descriptor-shaped
- added host coverage for:
  - object descriptor normalization
  - array descriptor preservation

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/agent_runtime/nook_java_js_bridge.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\\build\\test_js_runtime_native_attach.exe`

## 2026-04-29 Java.array mutation semantics

Goal:

- verify whether Nook still had a gap for the most important Frida-style array mutation case:
  - mutate `arr[i]`
  - pass the array back into Java
  - Java observes the updated contents

What was expected initially:

- the earlier design assumption was that `Java.array(...)` might still behave like a construction snapshot for later Java marshaling

What host TDD actually found:

- that assumption was wrong for simple index mutation
- the behavior already works today for:
  - primitive arrays
  - object arrays
  - nested 2D arrays

Why it already works:

- Nook's current Java marshaling path does not reuse a frozen native snapshot for `Java.array(...)`
- instead, when a JS value carries the Java-array marker, `ParseJavaJsValue(...)` re-reads:
  - the array type metadata
  - the current numeric element properties
- that means `arr[i] = ...` is naturally reflected when the array is marshaled into `JavaJsValue` later

Implementation result:

- no runtime code change was needed for the core `arr[i] = ...` mutation case
- the work in this pass was:
  - add host regression tests proving the behavior
  - add device smoke coverage
  - document the exact supported boundary

Host regression coverage added:

- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - primitive array mutation survives into Java invocation
  - object array mutation survives into Java invocation
  - nested array mutation survives into Java invocation

Device smoke added:

- [java_array_mutation_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_array_mutation_smoke.js)

Current supported boundary:

- supported and now regression-covered:
  - `arr[i] = ...` before passing the array into Java
  - nested-array inner element mutation before passing the outer array into Java
- still not claimed:
  - full live semantics for `push/pop/splice/sort`
  - arbitrary plain JS arrays automatically treated as Java arrays
  - live Java-side array object binding

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/agent_runtime/nook_java_js_bridge.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\build\test_js_runtime_native_attach.exe`

Android note:

- no runtime code changed in this pass
- Android rebuild/push was therefore intentionally skipped

## 2026-04-29 Java.registerClass methods Frida-style declaration support

Goal:

- move `Java.registerClass(spec)` one step closer to Frida without pretending Nook already has full dynamic-class semantics
- support Frida-style method declaration shapes in `spec.methods`

Design chosen:

- keep the current proxy/listener architecture
- accept these forms in `spec.methods`:
  - `onClick: function (...) {}`
  - `onClick: { returnType: 'void', argumentTypes: [...], implementation: function (...) {} }`
  - `onClick: [{ ...single declaration... }]`
- explicitly reject multiple declarations for the same method name
- validate `returnType` and `argumentTypes`, but do not use them for callback dispatch yet

Why this shape:

- it improves Frida script compatibility at the public API layer
- it avoids fake `fields` semantics and avoids claiming overload-aware callback dispatch that Nook does not have yet
- it fits the current `Proxy.newProxyInstance(...)` + native callback bridge design

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - added runtime normalization for `registerClass` method entries
  - plain functions still work unchanged
  - declaration objects now validate:
    - `implementation` is a function
    - `returnType` is a string when present
    - `argumentTypes` is an array of strings when present
  - declaration arrays are accepted only when they contain exactly one entry
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added coverage for:
    - declaration object success
    - single-entry declaration array success
    - missing `implementation` rejection
    - multi-declaration rejection
- added device smoke script:
  - [java_register_class_method_spec_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_register_class_method_spec_smoke.js)

Issue found during implementation:

- the shared `GetArrayLength(...)` helper is intentionally loose and only reads `.length`
- that means a plain declaration object could be misdetected as an array-like value in this parsing path

Fix:

- `registerClass` declaration-array parsing now first checks `JS_IsArray(...)`
- metadata validation for `argumentTypes` also now requires a real JS array

Current compatibility boundary:

- this is closer to Frida's `registerClass` spec shape
- this is still not full Frida `registerClass`
- Nook still does not support here:
  - `fields`
  - `superClass` / `extends`
  - constructor declarations
  - multiple method declarations with signature-aware callback dispatch

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/agent_runtime/nook_java_js_bridge.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\build\test_js_runtime_native_attach.exe`

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - added `Java.Env.getObjectRefType(obj)`
  - kept the API surface Frida-aligned at the JS layer by returning a string ref kind instead of a raw JNI enum
  - accepted only Java object wrappers, not raw pointers or class wrappers
  - mapped JNI ref kinds to:
    - `invalid`
    - `local`
    - `global`
    - `weak-global`
  - implemented it as a single JNI query, which keeps it inside the current safe `Env` boundary
- in [js_runtime_test_api.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime_test_api.h):
  - added testing hook declarations for `getObjectRefType`
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added regression coverage for:
    - returning `global`
    - returning `invalid`
    - rejecting non-Java objects
  - added a fake JNI bridge callback to capture:
    - env pointer
    - object handle
- added device smoke script:
  - [java_env_wrapper_ref_type_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_env_wrapper_ref_type_smoke.js)

Issue found during implementation:

- `jobjectRefType` and the `JNI*RefType` enum constants are only available in the Android build path in the current source layout
- the desktop unit-test build therefore failed even though the Android-side logic was correct

Fix:

- kept the Android JNI call unchanged
- changed the JS-facing mapping layer to use the stable JNI numeric ref-type values:
  - `0 = invalid`
  - `1 = local`
  - `2 = global`
  - `3 = weak-global`
- this preserves Android behavior while keeping the host-side test build portable

Why this helper is safe:

- unlike `monitorEnter/monitorExit` or local-frame-style operations, this does not require cross-call pairing
- it is one read-only JNI query against an existing wrapper-backed object
- that makes it a good fit for Nook's current runtime-managed `Env` facade

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/agent_runtime/nook_java_js_bridge.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\build\test_js_runtime_native_attach.exe`
- Android rebuild:
  - `E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
- Android push:
  - `adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`
  - `adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so`
  - `adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server`
  - `adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libc++_shared.so /data/local/tmp/nook/libc++_shared.so`
  - `adb shell su -c 'chmod 755 /data/local/tmp/nook/nook-server'`
- Device smoke to run next:
  - [java_env_wrapper_ref_type_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_env_wrapper_ref_type_smoke.js)

## 2026-04-28 Java Env phase 6: `Env.getStringUtfChars(...)` and `Env.releaseStringUtfChars(...)`

Goal:

- continue the `Java.vm.getEnv()` surface toward Frida by adding the raw UTF-8 acquire/release pair
- keep ownership explicit instead of adding Nook-only convenience semantics too early
- preserve the Android attach/lifetime rule already established in earlier `Env` phases

Design chosen:

- add only:
  - `env.getStringUtfChars(jstr)`
  - `env.releaseStringUtfChars(jstr, cstr)`
- reject malformed input with explicit type errors
- keep `Nook.Jni.readJStringUtf8(...)` unchanged
- keep auto-release and JS-string convenience helpers out of scope

Issue found while implementing:

- `env.newStringUtf(...)` previously returned a raw local `jstring` reference created inside a temporary `JavaEnv jenv` scope
- phase 6 needs that `jstring` to survive long enough to be passed into a later `getStringUtfChars(...)` call
- with per-call `JavaEnv` reacquisition, returning a local ref was not safe enough

Fix:

- on Android, `env.newStringUtf(...)` now:
  - creates the `jstring` as a local ref
  - promotes it to a `GlobalRef`
  - deletes the local ref
  - returns the `GlobalRef` handle
- the returned handle is registered in existing script-owned Java cleanup so unload/shutdown still releases it through the normal Java object cleanup path

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - added `Env.getStringUtfChars(...)`
  - added `Env.releaseStringUtfChars(...)`
  - exposed both on `MakeJavaEnvWrapper(...)`
  - added test callback plumbing for:
    - `GetStringUTFChars`
    - `ReleaseStringUTFChars`
  - updated `Env.newStringUtf(...)` Android path to return a `GlobalRef`
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added regression coverage for:
    - successful UTF-8 char acquisition
    - successful UTF-8 char release
    - invalid input rejection for both methods
- added device smoke script:
  - [java_env_wrapper_phase6_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_env_wrapper_phase6_smoke.js)

Current boundary relative to Frida:

- this matches Frida more closely than any Nook-specific helper layer would
- ownership is still manual and explicit, which is the intended phase boundary
- no auto-release or string-copy helper has been introduced in this pass

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `cmd /c .\\build\\test_js_runtime_native_attach.exe`

Pending verification:

- Android device smoke completed with:
  - `script load ok: name=java_env_wrapper_phase6_smoke.js`
  - `java-env-wrapper-phase6-bindings:object:object:function:function`
  - `java-env-wrapper-phase6-direct:object:function:function:function`
  - `java-env-wrapper-phase6-new:0x2e36:false`
  - `java-env-wrapper-phase6-chars:0xb400007408201298:false`
  - `java-env-wrapper-phase6-release:true`
  - `java-env-wrapper-phase6-exception:false`

## 2026-04-28 Java Env phase 7: `Env.newGlobalRef(...)` and `Env.deleteGlobalRef(...)`

Goal:

- continue the `Java.vm.getEnv()` surface toward Frida by adding the smallest stable explicit reference-lifetime primitive
- expose only the global-ref pair that is actually safe in Nook's current `Env` execution model

Initial phase-7 attempt and root cause:

- the first phase-7 cut included:
  - `Env.newLocalRef(obj)`
  - `Env.deleteLocalRef(ref)`
  - `Env.newGlobalRef(obj)`
  - `Env.deleteGlobalRef(ref)`
- desktop tests passed, but device smoke crashed
- root cause was architectural, not a simple bug:
  - each `Env` method call in Nook is a separate native/JNI entry
  - `NewLocalRef(...)` created a local ref during one JNI call
  - `DeleteLocalRef(...)` attempted to consume that ref in a later independent JNI call
  - JNI local refs are not safe cross-call handles in that model

Decision:

- narrow phase 7 to:
  - `Env.newGlobalRef(obj)`
  - `Env.deleteGlobalRef(ref)`
- explicitly defer:
  - `Env.newLocalRef(obj)`
  - `Env.deleteLocalRef(ref)`
  - weak global refs

Why this shape:

- it stays Frida-directed without exposing a primitive that is unsafe in the current architecture
- it matches the practical use-case for script-visible handles that survive later calls
- it keeps ownership explicit instead of inventing Nook-only cleanup behavior

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - added `Env.newGlobalRef(...)`
  - added `Env.deleteGlobalRef(...)`
  - added narrow host-test callback plumbing for:
    - `NewGlobalRef`
    - `DeleteGlobalRef`
  - removed the earlier local-ref phase-7 surface before final verification
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added regression coverage for:
    - successful global-ref creation
    - successful global-ref deletion
    - invalid input rejection for both methods
- added device smoke script:
  - [java_env_wrapper_phase7_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_env_wrapper_phase7_smoke.js)

Ownership boundary:

- user-created global refs remain caller-owned
- `Env.newGlobalRef(...)` does not auto-register the returned handle for script cleanup
- callers must explicitly delete refs through `Env.deleteGlobalRef(...)`

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `cmd /c .\\build\\test_js_runtime_native_attach.exe`

Android device smoke completed with:

- `script load ok: name=java_env_wrapper_phase7_smoke.js`
- `java-env-wrapper-phase7-bindings:object:object:function:function`
- `java-env-wrapper-phase7-direct:object:function:function`
- `java-env-wrapper-phase7-global:choose:com.demo.target.AdWallFragment:0x2e56:false:true`
- `java-env-wrapper-phase7-exception:false`

Current boundary relative to Frida:

- the public surface now includes a stable explicit global-ref path
- local refs remain intentionally absent from the script-visible `Env` API until the underlying JNI execution model can support them safely
- weak global refs are still pending

## 2026-04-28 Java Env phase 8: `Env.newWeakGlobalRef(...)` and `Env.deleteWeakGlobalRef(...)`

Goal:

- continue the `Java.vm.getEnv()` surface toward Frida with the next smallest persistent-reference primitive
- expose only the weak-global pair that is stable in Nook's current `Env` execution model

Decision:

- add:
  - `Env.newWeakGlobalRef(obj)`
  - `Env.deleteWeakGlobalRef(ref)`
- explicitly keep out:
  - `Env.newLocalRef(obj)`
  - `Env.deleteLocalRef(ref)`
  - weak-ref liveness helpers
  - weak-ref resurrection helpers
  - auto-cleanup for user-created weak refs

Why this shape:

- weak global refs are persistent JNI references, so they fit the current cross-call `Env` model
- this keeps the surface Frida-directed without reopening the known-unsafe local-ref path
- ownership remains explicit and low-level

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - added `Env.newWeakGlobalRef(...)`
  - added `Env.deleteWeakGlobalRef(...)`
  - added narrow runtime query helpers for:
    - `NewWeakGlobalRef`
    - `DeleteWeakGlobalRef`
  - added host-test callback plumbing for the same pair
- in [js_runtime_test_api.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime_test_api.h):
  - added weak-global test callback contracts
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added regression coverage for:
    - successful weak-global creation
    - successful weak-global deletion
    - invalid input rejection for both methods
- added device smoke script:
  - [java_env_wrapper_phase8_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_env_wrapper_phase8_smoke.js)

Ownership boundary:

- user-created weak global refs remain caller-owned
- `Env.newWeakGlobalRef(...)` does not auto-register the returned handle for script cleanup
- callers must explicitly delete refs through `Env.deleteWeakGlobalRef(...)`

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `cmd /c .\\build\\test_js_runtime_native_attach.exe`

Android smoke prepared:

- `nook-cli attach com.demo.target -l E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\host\\nook-py\\java_env_wrapper_phase8_smoke.js --wait --usb`

Current boundary relative to Frida:

- Nook now exposes stable explicit global and weak-global reference primitives on `Java.vm.getEnv()`
- local refs remain intentionally absent because they are unsafe as cross-call `Env` handles in the current architecture
- GC- or resurrection-oriented weak-ref helpers remain deferred until there is a concrete Frida-driven need

## 2026-04-29 Java Env monitor phase: `Env.monitorEnter(...)` and `Env.monitorExit(...)`

Goal:

- continue the `Java.vm.getEnv()` surface toward Frida with the next smallest practical synchronization primitives
- add only the low-level JNI monitor pair and keep the phase narrow

Decision:

- add:
  - `Env.monitorEnter(obj)`
  - `Env.monitorExit(obj)`
- explicitly do not add:
  - `getSuperclass(...)`
  - `isAssignableFrom(...)`
  - JS helpers such as `withMonitor(...)` or `synchronized(...)`

Why this shape:

- monitor primitives are real JNI operations that fit the current `Env` execution model
- they work on persistent Java object wrappers and do not rely on frame-local reference semantics
- they are more practically useful than another round of type-query helpers at this stage

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - added `Env.monitorEnter(...)`
  - added `Env.monitorExit(...)`
  - added narrow runtime query helpers for:
    - `MonitorEnter`
    - `MonitorExit`
  - added host-test callback plumbing for the same pair
- in [js_runtime_test_api.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime_test_api.h):
  - added monitor test callback contracts
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added regression coverage for:
    - successful `monitorEnter(...)`
    - successful `monitorExit(...)`
    - invalid input rejection for both methods
- added device smoke script:
  - [java_env_wrapper_monitor_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_env_wrapper_monitor_smoke.js)

Why this is safe in the current architecture:

- each monitor operation executes while a live local `JavaEnv jenv` exists at the actual call site
- unlike local refs, monitor operations do not require a frame-local handle to survive into a later independent JNI call

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `cmd /c .\\build\\test_js_runtime_native_attach.exe`

Android smoke prepared:

- `nook-cli attach com.demo.target -l E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\host\\nook-py\\java_env_wrapper_monitor_smoke.js --wait --usb`

Current boundary relative to Frida:

- Nook now covers another real low-level `Env` primitive without changing the current `Env` architecture
- local refs and local frames remain intentionally deferred
- helper-layer synchronization sugar is still unnecessary at this stage

### 2026-04-29 correction

The conclusion above turned out to be wrong on device and is superseded.

Observed on device:

- `env.monitorEnter(obj)` succeeds
- `env.monitorExit(obj)` fails on the same wrapper
- the failure reproduces for fresh objects, `Java.choose(...)` matches, and retained wrappers

Root cause:

- Nook's public `Env` is a helper facade, not a stable live `JNIEnv*`
- each `env.xxx()` call is a separate JNI re-entry
- on Android this path currently depends on temporary `JavaEnv` attach/detach lifetime

Updated decision:

- remove public `Env.monitorEnter(...)`
- remove public `Env.monitorExit(...)`
- remove their test hooks and smoke scripts
- treat monitor pairs as the same current architecture boundary as local refs and local frames

Verification after rollback:

- red: `cmd /c build\\test_js_runtime_native_attach.exe`
- green:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `cmd /c build\\test_js_runtime_native_attach.exe`

## 2026-04-28 Java Env wrapper phase 2

Goal:

- extend the new `Env` wrapper by one small Frida-aligned step
- keep the surface narrow and practical instead of jumping straight to full JNI breadth

Public shape landed:

- `env.exceptionOccurred()`
- `env.exceptionClear()`

## 2026-04-29 Java Env class query phase: `Env.getSuperclass(...)` and `Env.isAssignableFrom(...)`

Goal:

- continue `Java.vm.getEnv()` Frida alignment with helpers that remain safe across independent JNI re-entry
- explicitly avoid the monitor/local-ref architecture boundary discovered earlier on 2026-04-29

Decision:

- add:
  - `Env.getSuperclass(classWrapper)`
  - `Env.isAssignableFrom(targetClassWrapper, sourceClassWrapper)`
- do not add:
  - object-wrapper auto-conversion
  - any helper sugar above the raw class-query pair

Why these are safe:

- both are single-shot JNI queries
- neither depends on one stable cross-call attached-thread lifetime
- both fit the current runtime-managed `Env` helper model

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - added class-wrapper parsing helper
  - added `Env.getSuperclass(...)`
  - added `Env.isAssignableFrom(...)`
  - added narrow internal Android query helpers for both
- in [js_runtime_test_api.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime_test_api.h):
  - added host test callback contracts for:
    - `GetSuperclass`
    - `IsAssignableFrom`
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added regression coverage for:
    - superclass wrapper return
    - no-superclass `null` return
    - assignability boolean return
    - invalid class-wrapper rejection
- added Android smoke:
  - [java_env_wrapper_superclass_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_env_wrapper_superclass_smoke.js)

Verification:

- red:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - failed with undefined references to the new test-hook entrypoints
- green:
  - same desktop build command
  - `cmd /c build\\test_js_runtime_native_attach.exe`
  - Android rebuild:
    - `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`

Current status:

- desktop regression is green
- Android smoke script is ready
- device validation still needs to be run against freshly pushed binaries
- `env.getObjectClass(obj)`

Why these three next:

- `exceptionOccurred()` is the natural read-side companion to `exceptionCheck()`
- `exceptionClear()` makes exception-state workflows minimally usable
- `getObjectClass(obj)` is the smallest useful object-reference helper and exercises Java-wrapper argument parsing

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - extended the `Env` wrapper factory with:
    - `exceptionOccurred`
    - `exceptionClear`
    - `getObjectClass`
  - added narrow runtime query helpers for:
    - exception object lookup
    - exception clearing
    - runtime class lookup from a Java wrapper object
  - preserved the phase-1 Android rule that real JNI calls happen only while a local `JavaEnv jenv` is alive at the actual call site
- in [js_runtime_test_api.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime_test_api.h):
  - added test callback contracts for:
    - `exceptionOccurred`
    - `exceptionClear`
    - `getObjectClass`
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added focused regression coverage for:
    - pointer-like return from `env.exceptionOccurred()`
    - boolean return from `env.exceptionClear()`
    - pointer-like return from `env.getObjectClass(obj)`
    - type rejection for non-Java-object input
- added device smoke script:
  - [java_env_wrapper_phase2_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_env_wrapper_phase2_smoke.js)

Boundary kept explicit:

- this phase still does not add:
  - `isSameObject`
  - `isInstanceOf`
  - class-name decoding helpers
  - reference lifetime helpers on `Env`

Android lifetime note:

- phase 1 already proved that returning a raw `JNIEnv*` from a temporary attach scope is unsafe
- phase 2 keeps the same corrected rule:
  - `env.handle` remains diagnostic only
  - JNI work in `exceptionOccurred()`, `exceptionClear()`, and `getObjectClass()` reacquires a live `JavaEnv jenv` locally at the actual JNI call site

Verification completed locally:

- desktop build:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- desktop test:
  - `cmd /c .\\build\\test_js_runtime_native_attach.exe`
- Android build:
  - `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
- Android push:
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook.so /data/local/tmp/nook/libnook.so`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\nook-server /data/local/tmp/nook/nook-server`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libc++_shared.so /data/local/tmp/nook/libc++_shared.so`
- device smoke result from [java_env_wrapper_phase2_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_env_wrapper_phase2_smoke.js):
  - `java-env-wrapper-phase2-bindings:object:object:function:function`
  - `java-env-wrapper-phase2-direct:object:object:Env(...):...`
  - `java-env-wrapper-phase2-exception-occurred:0x0:true`
  - `java-env-wrapper-phase2-exception-clear:true`
  - `java-env-wrapper-phase2-try:true:null`
  - `java-env-wrapper-phase2-get-object-class:function:com.demo.target.TextFragment:0x5`

## 2026-04-28 Java Env wrapper phase 3

Goal:

- add the first Java object identity helper on top of the new `Env` wrapper
- keep the scope smaller than `isInstanceOf(...)`

Public shape landed:

- `env.isSameObject(a, b)`

Why this next:

- phase 2 already validated object-wrapper parsing and class lookup
- the next missing Frida-aligned primitive was object identity
- raw wrapper handle text must not be treated as Java object identity

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - added runtime-backed `Env.isSameObject(a, b)`
  - added a narrow JNI query helper for object identity comparison
  - kept the Android rule from phases 1 and 2:
    - the real `IsSameObject(...)` JNI call is executed only while a local `JavaEnv jenv` is alive at the actual call site
- in [js_runtime_test_api.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime_test_api.h):
  - added one minimal test callback contract for object identity comparison
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added focused regression coverage for:
    - boolean return from `env.isSameObject(left, right)`
    - forwarding of both Java object handles into the JNI helper
    - non-Java-object rejection
- added device smoke script:
  - [java_env_wrapper_phase3_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_env_wrapper_phase3_smoke.js)

Why this does not collapse into raw handle equality:

- different wrappers may represent the same Java object through different JNI references
- Frida-style semantics require Java identity, not wrapper-property string equality
- `IsSameObject(...)` is the correct primitive to validate before adding broader relationship helpers

Boundary kept explicit:

- this phase still does not add:
  - `isInstanceOf(...)`
  - class-name helpers
  - reference lifetime helpers on `Env`

Verification completed locally:

- desktop build:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- desktop test:
  - `cmd /c build\\test_js_runtime_native_attach.exe`
- Android build:
  - `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
- Android push:
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook.so /data/local/tmp/nook/libnook.so`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\nook-server /data/local/tmp/nook/nook-server`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libc++_shared.so /data/local/tmp/nook/libc++_shared.so`
- device smoke result from [java_env_wrapper_phase3_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_env_wrapper_phase3_smoke.js):
  - `java-env-wrapper-phase3-direct:object:function`
  - `java-env-wrapper-phase3-same:true`
  - `java-env-wrapper-phase3-different:false`

## 2026-04-28 Java Env wrapper phase 4

Goal:

- add the first object-to-class relationship helper on top of the `Env` wrapper
- keep the public shape aligned with Frida usage

Public shape landed:

- `env.isInstanceOf(obj, klass)`

Why this next:

- phase 3 already validated Java object identity through `isSameObject(...)`
- the next narrow Frida-aligned helper is checking whether an object is an instance of a class wrapper
- this builds naturally on the existing `Java.use(...)` class-wrapper model

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - added runtime-backed `Env.isInstanceOf(obj, klass)`
  - added a narrow JNI query helper for instance checking
  - validated:
    - first argument must be a Java object wrapper
    - second argument must be a Java class wrapper
  - kept Android attach lifetime safe by performing the real JNI `IsInstanceOf(...)` call only while a local `JavaEnv jenv` is alive
- class resolution on Android is loader-aware:
  - if the class wrapper carries `__nookJavaLoaderHandle`, the runtime now resolves the target class through [JavaHook::FindClassWithLoader](E:/Learn/my_program/all_my_hook/kanxue/Nook/src/java_hook/JavaHook.h)
  - otherwise it falls back to the default [JavaHook::FindClass](E:/Learn/my_program/all_my_hook/kanxue/Nook/src/java_hook/JavaHook.h)
  - this avoids misresolving app classes on native threads where plain `FindClass(...)` is not enough
- in [js_runtime_test_api.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime_test_api.h):
  - added one minimal test callback contract for instance checking
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added focused regression coverage for:
    - boolean return from `env.isInstanceOf(obj, klass)`
    - forwarding of object handle and class name into the JNI helper
    - non-Java-object rejection
    - non-class-wrapper rejection
- added device smoke script:
  - [java_env_wrapper_phase4_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_env_wrapper_phase4_smoke.js)

Why the second argument is limited to class wrappers:

- this matches Frida's higher-level calling style
- it avoids mixing wrapper-based APIs with raw class pointers
- it keeps validation and error reporting clear

Boundary kept explicit:

- this phase still does not add:
  - string class-name overloads
  - class-pointer second arguments
  - class-name helpers

Verification completed locally:

- desktop build:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- desktop test:
  - `cmd /c .\\build\\test_js_runtime_native_attach.exe`
- Android build:
  - `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
- Android push:
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook.so /data/local/tmp/nook/libnook.so`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\nook-server /data/local/tmp/nook/nook-server`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libc++_shared.so /data/local/tmp/nook/libc++_shared.so`
- device smoke result from [java_env_wrapper_phase4_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_env_wrapper_phase4_smoke.js):
  - `java-env-wrapper-phase4-direct:object:function`
  - `java-env-wrapper-phase4-own:true`
  - `java-env-wrapper-phase4-other:false`

## 2026-04-28 Java Env wrapper phase 5

Goal:

- add the first write-side JNI string helper on top of the `Env` wrapper
- keep the string bridge narrower than a full read/write UTF-8 pair

Public shape landed:

- `env.newStringUtf(str)`

Why this next:

- phase 4 already proved object/class relationship helpers on `Env`
- the next practical Frida-aligned primitive is creating a real JNI `jstring` from JS text
- this is narrower and safer than introducing `GetStringUTFChars` / release-pair ownership semantics immediately

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - added runtime-backed `Env.newStringUtf(str)`
  - added a narrow JNI query helper for UTF-8 string creation
  - validated that the argument must be a JS string
  - kept Android attach lifetime safe by performing the real JNI `NewStringUTF(...)` call only while a local `JavaEnv jenv` is alive
- in [js_runtime_test_api.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime_test_api.h):
  - added one minimal test callback contract for UTF-8 string creation
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added focused regression coverage for:
    - pointer-like return from `env.newStringUtf("hello")`
    - forwarding of the exact source string into the JNI helper
    - non-string rejection
- added device smoke script:
  - [java_env_wrapper_phase5_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_env_wrapper_phase5_smoke.js)

Why `Nook.Jni.readJStringUtf8(...)` stays unchanged here:

- it still exists at the JS API layer
- in the current async native-hook runtime it is still intentionally guarded
- phase 5 does not broaden it into a newly safe general-purpose read bridge
- this phase is intentionally write-side only

Boundary kept explicit:

- this phase still does not add:
  - `env.getStringUtfChars(...)`
  - `env.releaseStringUtfChars(...)`
  - changed semantics for `Nook.Jni.readJStringUtf8(...)`

Verification completed locally:

- desktop build:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- desktop test:
  - `cmd /c .\\build\\test_js_runtime_native_attach.exe`
- Android build:
  - `E:\\SDK\\ndk\\25.2.9519653\\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
- Android push:
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libnook.so /data/local/tmp/nook/libnook.so`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\nook-server /data/local/tmp/nook/nook-server`
  - `adb push E:\\Learn\\my_program\\all_my_hook\\kanxue\\Nook\\libs\\arm64-v8a\\libc++_shared.so /data/local/tmp/nook/libc++_shared.so`
- device smoke result from [java_env_wrapper_phase5_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_env_wrapper_phase5_smoke.js):
  - `java-env-wrapper-phase5-direct:object:function`
  - `java-env-wrapper-phase5-new:0x5:false`
  - `java-env-wrapper-phase5-exception:false`

## 2026-04-28 Java Env wrapper phase 1

Goal:

- move `Java.vm.getEnv()` / `Java.vm.tryGetEnv()` away from raw `NativePointer` returns
- establish the first Frida-aligned `Env` object surface without broadening into full JNI yet

Public shape landed:

- `Java.vm.getEnv()` now returns an `Env` wrapper
- `Java.vm.tryGetEnv()` now returns `Env | null`
- phase-1 `Env` surface is intentionally narrow:
  - `handle`
  - `toString()`
  - `exceptionCheck()`
  - `findClass(name)`

Why raw pointer returns were replaced:

- a bare `JNIEnv*` is not a good long-term public API
- Frida-style evolution depends on an object surface that can grow method-by-method
- switching now avoids freezing a pointer-only contract too early

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - added a minimal `Env` wrapper factory backed by `JNIEnv*`
  - changed `Java.vm.getEnv()` and `Java.vm.tryGetEnv()` to return the wrapper instead of `NativePointer`
  - added runtime-backed `Env.toString()`, `Env.exceptionCheck()`, and `Env.findClass(name)`
  - added narrow host-test callbacks for:
    - `exceptionCheck`
    - `findClass`
- in [js_runtime_test_api.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime_test_api.h):
  - exposed the two new test hooks needed by the desktop host
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - replaced the old raw-pointer assertions with wrapper-shape assertions
  - added regression coverage for:
    - wrapper object shape
    - `handle` exposure
    - `toString()` format
    - `exceptionCheck()`
    - `findClass("java/lang/String")`
    - non-string rejection for `findClass(...)`
- added device smoke script:
  - [java_env_wrapper_phase1_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_env_wrapper_phase1_smoke.js)

Boundary kept explicit:

- this phase does not add:
  - string helpers
  - reference lifetime helpers
  - method invocation through `Env`
  - broader exception mutation helpers

Frida alignment note:

- this moves Nook closer to Frida's direction by making `Java.vm` return an object surface instead of a raw pointer
- it is still only phase 1; later work can add Frida-style `Env` breadth on top of the same object model

Device crash found after first rollout:

- `java_env_wrapper_phase1_smoke.js` crashed the target during `env.exceptionCheck()`
- the crash happened before any script message was emitted from the evaluated script body
- logcat backtrace mapped the fault to:
  - `JsJavaEnvExceptionCheck(...)`
  - specifically the native `JNIEnv->ExceptionCheck()` dereference path

Root cause:

- `Java.vm.getEnv()` used `QueryCurrentJavaEnvPointer(true, ...)`
- on Android that path obtained `JNIEnv*` through a stack `JavaEnv jenv`
- `JavaEnv` detaches the current thread in its destructor when it attached it
- the `Env` wrapper kept the raw pointer after `jenv` went out of scope
- raw pointer stringification still worked, but the first real JNI call dereferenced a stale `JNIEnv*` and crashed

Fix:

- kept `env.handle` and `env.toString()` as wrapper-visible diagnostics
- changed `Env` JNI methods to reacquire the current live `JNIEnv*` with `QueryCurrentJavaEnvPointer(true, ...)` immediately before native JNI use
- added a desktop regression proving wrapper methods do not rely on the originally stored env pointer:
  - first `getEnv()` query returns one fake pointer
  - the subsequent JNI method query returns a second fake pointer
  - `env.exceptionCheck()` must use the second, live pointer

Follow-up issue found on device:

- the first fix was still insufficient on Android
- ART `CheckJNI` aborted with:
  - `a thread ... is making JNI calls without being attached in call to ExceptionCheck`

Refined root cause:

- `QueryCurrentJavaEnvPointer(true, ...)` internally used a stack `JavaEnv jenv`
- it returned the raw `JNIEnv*`, but `jenv` detached the thread before the caller used that pointer
- so even "freshly requeried" pointers were still obtained from an attach scope that had already ended

Final fix:

- moved Android attach scope to the actual JNI call sites in:
  - `QueryJavaEnvExceptionCheck(...)`
  - `QueryJavaEnvFindClass(...)`
- those paths now create `JavaEnv jenv` locally and execute `ExceptionCheck()` / `FindClass(...)` while the thread is still attached
- desktop test coverage remains in place and Android was rebuilt and repushed after this change

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `cmd /c build\\test_js_runtime_native_attach.exe`

## 2026-04-28 Java.performNow refactor: delegate to `Java.vm.perform(...)`

Goal:

- align `Java.performNow(fn)` more closely with Frida by making it a thin wrapper over the VM execution primitive
- keep one execution core for immediate Java work

Scope:

- refactor bootstrap only
- do not change:
  - `Java.ready(...)`
  - `Java.perform(...)`
  - any broader `Java.vm` API surface

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - changed `Java.performNow(fn)` from direct `fn()` execution to `Java.vm.perform(fn)`
  - preserved the existing argument validation and error text:
    - `Java.performNow requires a function`
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added a regression proving `Java.performNow(fn)` delegates through `Java.vm.perform(fn)`
  - kept coverage for:
    - non-function rejection
    - immediate execution order

Why this change:

- `Java.vm.perform(fn)` is now the single immediate Java execution base
- `Java.perform(fn)` already routes through:
  - `Java.ready(function () { return Java.vm.perform(fn); })`
- after this change:
  - `Java.performNow(fn)` and `Java.perform(fn)` share the same VM execution path
  - future VM-level fixes can stay centralized

Verification completed locally:

- red:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `cmd /c build\\test_js_runtime_native_attach.exe`
  - expected failure observed:
    - new delegation assertion failed before the runtime change
- green:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `cmd /c build\\test_js_runtime_native_attach.exe`

Device verification boundary:

- this pass has not added any new ready semantics
- device smoke should only confirm that `Java.performNow(...)` remains immediate and Java-capable after the internal delegation change

## 2026-04-28 Java.vm.getEnv minimal pointer primitive

Goal:

- add the next low-level `Java.vm` primitive after `Java.vm.perform(fn)`
- expose the current thread's `JNIEnv*` as a `NativePointer`
- stay narrow and avoid expanding into `tryGetEnv()` or an `Env` wrapper

Scope:

- add `Java.vm.getEnv()`
- reuse the existing Java runtime path on Android
- add only the minimum host-test injection needed to cover the API on desktop

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - added runtime-backed `Java.vm.getEnv()`
  - added a focused internal helper to resolve the current thread's env pointer
  - on Android:
    - initialize the Java hook runtime if needed
    - use `JavaEnv` to acquire `JNIEnv*`
    - return it as a `NativePointer`
  - on host tests:
    - use a test-only callback override instead of hardcoding fake production behavior
- in [js_runtime_test_api.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime_test_api.h):
  - added test-only setter/resetter for supplying a fake env pointer source
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added regression coverage for:
    - binding existence
    - `NativePointer` return shape
    - repeated same-pointer results
    - direct use inside `Java.vm.perform(...)`
- added device smoke script:
  - [java_vm_getenv_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_vm_getenv_smoke.js)

Why this shape:

- it keeps `Java.vm` aligned with Frida in small, usable pieces
- it avoids exposing a fake JS placeholder for a native-only concept
- it keeps future env-related work centralized under `Java.vm`

Boundary kept explicit:

- not added in this pass:
  - `Java.vm.tryGetEnv()`
  - any `Env` wrapper object
  - direct JNI helper APIs layered on top of the returned pointer
- `Java.vm.getEnv()` currently exposes only the pointer primitive

Verification completed locally:

- desktop red:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - observed failure:
    - undefined references for `JsRuntimeSetGetJavaEnvPointerForTesting(...)`
    - confirmed missing runtime/test support for the new API
- desktop green:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `cmd /c build\\test_js_runtime_native_attach.exe`
- Android rebuild:
  - `E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
- Android push:
  - `adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`
  - `adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so`
  - `adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server`
  - `adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libc++_shared.so /data/local/tmp/nook/libc++_shared.so`
- device smoke:
  - `nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_vm_getenv_smoke.js --wait --usb`
  - observed:
    - `java-vm-getenv-bindings:object:object:function`
    - `java-vm-getenv-direct:false:0xb4000073ab3e7140`
    - `java-vm-getenv-perform:false:0xb4000073ab3e7140`

Interpretation:

- `Java.vm.getEnv()` is present on the public `Java.vm` surface
- it returns a non-null `NativePointer`
- the same env pointer is visible directly and from inside `Java.vm.perform(...)`
- this is the intended minimal Frida-aligned primitive for this pass

## 2026-04-28 Java.vm.perform semantic fix: attach before callback execution

Goal:

- correct `Java.vm.perform(fn)` so it behaves like an attach-and-execute primitive instead of a plain synchronous callback wrapper
- make `Java.vm.tryGetEnv()` return a non-null pointer inside `Java.vm.perform(...)`

Problem found on device:

- `java-vm-trygetenv-direct:true:null`
- `java-vm-trygetenv-perform:true:null`

Interpretation:

- direct `tryGetEnv()` returning `null` was acceptable
- `perform`-scoped `tryGetEnv()` returning `null` was not
- this proved `Java.vm.perform(fn)` still was not ensuring JVM attachment before invoking the callback

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - refactored `Java.vm.perform(fn)` through a small internal helper that ensures env availability first
  - added a scoped runtime override so host tests can model:
    - direct `tryGetEnv()` unavailable
    - `perform`-scoped `tryGetEnv()` available
  - kept callback execution synchronous and preserved existing ordering
- in [JVM.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/java_hook/JVM.h) and [JVM.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/java_hook/JVM.cpp):
  - exposed the minimum static `JavaVM*` accessor needed by the runtime's env-query path
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - upgraded the env-query test callback to receive the attach intent
  - added a regression proving:
    - direct `tryGetEnv()` can be `null`
    - inside `Java.vm.perform(...)`, `tryGetEnv()` must become non-null

Why this change:

- without this fix, `Java.vm.perform(fn)` could not serve as the real base primitive for:
  - `Java.performNow(fn)`
  - `Java.perform(fn)`
  - future `Java.vm` surfaces
- this closes the semantic gap that Frida's `perform()` explicitly solves

Boundary kept explicit:

- this pass does not add:
  - worker-thread Java dispatch
  - async VM scheduling
  - env wrapper objects
  - broader VM lifecycle surfaces

Verification completed locally:

- desktop red:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - observed failure:
    - runtime still called the old env callback shape, so the new perform-scoped attach test failed at compile time until the runtime contract was updated
- desktop green:
  - `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
  - `cmd /c build\\test_js_runtime_native_attach.exe`
- Android rebuild:
  - `E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`
- Android push:
  - `adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook-agent.so /data/local/tmp/nook/libnook-agent.so`
  - `adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libnook.so /data/local/tmp/nook/libnook.so`
  - `adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server`
  - `adb push E:\Learn\my_program\all_my_hook\kanxue\Nook\libs\arm64-v8a\libc++_shared.so /data/local/tmp/nook/libc++_shared.so`
- device smoke:
  - `nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_vm_perform_smoke.js --wait --usb`
  - observed:
    - `java-vm-perform-bindings:object:object:function`
    - `java-vm-perform-callback:inside:true:true`
    - `java-vm-perform-order:inside|after`
  - `nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_vm_trygetenv_smoke.js --wait --usb`
  - observed:
    - `java-vm-trygetenv-bindings:object:object:function`
    - `java-vm-trygetenv-direct:true:null`
    - `java-vm-trygetenv-perform:false:0xb4000073ab336b00`

Interpretation:

- direct `tryGetEnv()` remains a non-attaching query
- inside `Java.vm.perform(...)`, env is now available as required
- `Java.vm.perform(fn)` is now much closer to Frida's intended semantics

## 2026-04-28 Java.vm.perform phase-1 baseline

Goal:

- add the first explicit `Java.vm` execution primitive to Nook
- start building a lower-level VM-facing layer that later `Java.performNow(fn)` and `Java.perform(fn)` can converge on

Chosen scope:

- add only:
  - `Java.vm.perform(fn)`
- do not add yet:
  - `Java.vm.getEnv()`
  - `Java.vm.tryGetEnv()`
  - explicit attach-state APIs
  - `Env` wrappers

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - added `JsJavaVmPerform(...)`
  - extracted the shared immediate-callback path into an internal helper used by:
    - `Java.perform(fn)`
    - `Java.vm.perform(fn)`
  - added `Java.vm` as an object on the public `Java` surface
  - added `Java.vm.perform` as a runtime-backed C function
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added regression coverage for:
    - `typeof Java.vm === 'object'`
    - `typeof Java.vm.perform === 'function'`
    - non-function rejection
    - synchronous execution ordering
    - immediate Java bridge use from inside `Java.vm.perform(...)`
- added device smoke script:
  - [java_vm_perform_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_vm_perform_smoke.js)

Why this shape:

- it gives Nook a true VM-level public entrypoint instead of only another JS helper alias
- it stays narrow enough to avoid prematurely designing a full `Env` API
- it is a cleaner long-term base for later Frida alignment:
  - `Java.performNow(fn)` can later become a thin wrapper over `Java.vm.perform(fn)`
  - `Java.perform(fn)` can later be re-expressed as readiness/lifecycle gating plus `Java.vm.perform(fn)`

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `cmd /c build\\test_js_runtime_native_attach.exe`
- `E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`

Current blocker:

- pushing fresh Android binaries for device smoke is currently blocked because `adb devices` reports the test device as:
  - `21ce24db    offline`
- once the device is back online, the next verification step is:
  - push rebuilt binaries
  - run `java_vm_perform_smoke.js`

## 2026-04-28 Java.perform now composes Java.ready and Java.vm.perform

Goal:

- stop treating `Java.perform(fn)` as its own execution primitive
- make it a composition layer over:
  - `Java.ready(fn)` for lifecycle readiness
  - `Java.vm.perform(fn)` for actual VM/thread execution

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - overrode bootstrap-level `Java.perform(fn)` with:
    - function validation
    - `Java.ready(function () { return Java.vm.perform(fn); })`
  - left these unchanged in this pass:
    - `Java.performNow(fn)`
    - `Java.ready(fn)`
    - `Java.vm.perform(fn)`
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - kept synchronous behavior coverage by forcing `Java.ready(...)` immediate in the test
  - added regression coverage for:
    - `Java.perform(123)` rejection
    - delegation through `Java.vm.perform(...)`

Why this shape:

- it preserves current user-facing behavior while cleaning up internal layering
- it matches the intended architecture:
  - `Java.vm.perform(fn)` handles VM execution
  - `Java.ready(fn)` handles readiness timing
  - `Java.perform(fn)` becomes the combination of the two
- it avoids dragging `Java.performNow(fn)` into the same refactor pass

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `cmd /c build\\test_js_runtime_native_attach.exe`
- `E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application.mk APP_ABI=arm64-v8a -j4`

Verification completed on device:

- pushed fresh Android binaries after rebuild
- ran:
  - `nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_perform_smoke.js --wait --usb`
- observed:
  - `java-ready-debug:immediate`
  - `java-bindings:object:function:function`
  - `java-wrapper:object:function:function`
  - `java-implementation-installed`

Interpretation:

- `Java.perform(...)` still reaches the ready path correctly
- the callback still executes
- Java wrapper access and implementation install still work after the internal refactor

Current boundary:

- `Java.performNow(fn)` is still separate in this pass
- broader `Java.vm` APIs such as `getEnv()` remain intentionally out of scope

Conclusion:

- `Script.unbindWeak(...)` now matches the Frida-facing lifecycle model more closely
- Java retained-wrapper cleanup still preserves exact-once release across:
  - explicit `$dispose()`
  - GC
  - unload

## 2026-04-29 Java.registerClass signature-aware callback dispatch

Goal:

- align `Java.registerClass(spec).methods` more closely with Frida for same-name multi-declaration methods
- stop dispatching callbacks by method name alone when the native side already has the real reflected `Method`

What changed:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - `registerClass` method parsing now accepts:
    - plain function
    - single declaration object
    - single-entry declaration array
    - multi-entry declaration array
  - declaration metadata is normalized into JNI signatures
  - callback storage is now keyed by:
    - method name
    - method signature
  - legacy fallback by method name is still preserved for:
    - plain function form
    - single declaration form
- in [nook_java_js_bridge.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.h):
  - `JavaJsRegisteredClassMethodRecord` now carries `signature`
- in [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp):
  - the native `InvocationHandler` path now reflects:
    - parameter types
    - return type
  - it builds the real JNI signature from the reflected `Method`
  - runtime dispatch now uses:
    - `method_name + method_signature`
    - then falls back to legacy name-only callback when appropriate
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added regression coverage for:
    - distinct-signature multi-declaration acceptance
    - exact callback selection for each signature
    - duplicate signature rejection
    - backward-compatible single-declaration behavior

Public semantics after this pass:

- this now works:
  - `methods.foo = [{ returnType, argumentTypes, implementation }, { ... }]`
- Nook dispatches by the actual runtime Java method signature instead of guessing from JS values
- duplicate declaration signatures for one method name are rejected
- multi-declaration arrays must provide:
  - `returnType`
  - `argumentTypes`

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/agent_runtime/nook_java_js_bridge.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\build\test_js_runtime_native_attach.exe`

Boundary kept explicit:

- this pass does not add:
  - `fields`
  - `extends` / `superClass`
  - constructor overload declarations
  - multi-interface same-name conflict policy beyond the actual reflected `Method`
- no Android smoke was added in this pass because the current demo target does not expose a suitable real multi-signature interface callback site

## 2026-04-29 Java direct-invoke overload resolution tightening

Goal:

- tighten Nook's default Java direct-invoke overload resolution so it behaves more like Frida in common ambiguous cases
- keep the fix inside the current architecture instead of adding Nook-only special cases

What changed:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - broadened direct-invoke candidate generation for:
    - `null`
    - JS booleans
    - JS strings
    - JS numeric values
    - Java object wrappers
    - Java arrays
  - added boxed/object fallback candidates where safe:
    - `java.lang.Boolean`
    - `java.lang.String` -> `java.lang.CharSequence` -> `java.lang.Object`
    - numeric wrapper classes
    - `java.lang.Number`
    - `java.lang.Object`
  - introduced one internal overload candidate token for nullable direct invoke:
    - `__nook_null__`
- in [nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp):
  - taught the reflected overload resolver to treat `__nook_null__` as:
    - matching reference parameters
    - not matching primitive parameters
  - kept ambiguity conservative instead of inventing a fake ranking rule for every nullable reference case
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added host coverage for:
    - nullable reference direct invoke
    - primitive-only `null` rejection
    - boxed boolean fallback
    - string -> `Object` fallback
    - number -> `Number` fallback
    - object-wrapper -> `Object` fallback

Why this matters:

- Nook already had good exact `.overload(...)` support
- the remaining practical gap was the plain direct-invoke path users hit constantly in Frida-style scripts
- this pass makes direct invoke less brittle without claiming a full reflected overload-scoring engine

Behavior after this pass:

- `null` can now participate in overload resolution for reference targets
- primitive-only overloads still reject `null`
- direct invoke can now fall back to boxed/object signatures for booleans, strings, and numbers
- direct invoke from wrapper objects can now fall back to `java.lang.Object`

Boundary kept explicit:

- this is still not a full Frida-grade overload scorer
- this pass does not add:
  - full superclass/interface ranking for arbitrary wrapper objects
  - "most specific nullable reference overload" selection in every ambiguous `null` case
  - any `Env` architecture change

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp src/agent_runtime/nook_java_js_bridge.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `.\build\test_js_runtime_native_attach.exe`

## 2026-04-28 Initial `Script.pin()` / `Script.unpin()` baseline

Goal:

- add the next missing Frida-facing lifecycle primitive after `bindWeak(...)` / `unbindWeak(...)`
- make `Script.pin()` actually block script unload in a predictable way

Design boundary for this pass:

- Frida's lifecycle intent is that pinning prevents unload
- Nook's current CLI/runtime model is synchronous and does not yet have a good host-side "wait until unpinned" workflow
- so the safest first step is:
  - `Script.pin()` increments a script-scoped pin count
  - `Script.unpin()` decrements it
  - unload fails with a clear error while the script is still pinned

Why this shape was chosen:

- it preserves the most important public semantic immediately: pinned scripts cannot be unloaded
- it avoids inventing a half-baked asynchronous unload waiter in the CLI/runtime before the underlying lifecycle model needs it
- it is strictly closer to Frida than having no pinning primitive at all

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - added `Script.pin()`
  - added `Script.unpin()`
  - added per-script pin counts in runtime state
  - `RemoveMessageHandler(...)` now refuses unload with:
    - `script is pinned`
  - `Script.unpin()` throws on underflow:
    - `Script.unpin called while pin count is zero`
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added regression coverage for:
    - binding existence
    - unload blocked while pinned
    - reference-counted pin/unpin
    - underflow rejection
- added device smoke script:
  - [script_pin_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/script_pin_smoke.js)

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `cmd /c build\\test_js_runtime_native_attach.exe`

Current limitation:

- unload currently fails immediately while pinned instead of waiting for later `unpin()`
- that is intentional for this first pass and keeps behavior explicit

Conclusion:

- Nook now exposes the initial `Script.pin()` / `Script.unpin()` baseline
- the public lifecycle surface is one step closer to Frida
- if needed later, this can be extended into a deferred unload/resume model without changing the basic script-side API

## 2026-04-28 REPL `%unload` failure no longer kills the session

Goal:

- keep `nook-cli repl` usable after `%unload` fails, especially for the new pinned-script case

Observed issue:

- after adding `Script.pin()` / `Script.unpin()`, `%unload` can now fail with:
  - `script is pinned`
- in REPL mode, that failure used to bubble out of `_repl_unload_script(...)`
- the exception then reached top-level `main(...)`, which exited the whole CLI process
- result:
  - users could not run `%call getcount`
  - users could not `%call unpin`
  - users could not retry `%unload`

Root cause:

- [cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/cli.py) had no local exception handling inside `_repl_unload_script(...)`
- unlike exit-time cleanup, explicit `%unload` just called `context.script.unload()` directly

Implementation:

- in [cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/cli.py):
  - wrapped `_repl_unload_script(...)` in local `try/except`
  - on unload failure:
    - print `script unload failed: ...`
    - keep the current active script/context intact
    - continue the REPL loop
  - only clear the active script after a successful unload
- in [test_cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_cli.py):
  - added a regression covering:
    - first `%unload` fails with `script is pinned`
    - `%call ping []` still works afterwards
    - `%exit` later unloads successfully

Verification completed locally:

- `python -m unittest host.nook-py.tests.test_cli`

Conclusion:

- pinned unload failures are now recoverable in REPL mode
- users can unpin and retry without restarting `nook-cli`

## 2026-04-28 Java main-thread helpers: `Java.isMainThread()` and `Java.scheduleOnMainThread(...)`

Goal:

- add the next practical Frida-aligned Java helper pair after `Java.ready(...)`, `Java.performNow(...)`, and `Java.registerClass(...)`
- make it possible to:
  - detect whether code is already on Android's main thread
  - post a JS callback onto the main thread without adding a new native bridge first

Design chosen:

- keep this pass JS-only inside the Java bootstrap
- build on top of existing runtime surfaces:
  - `Java.use(...)`
  - direct Java method invocation
  - constructor support through `$new(...)`
  - `Java.registerClass(...)`
- use:
  - `android.os.Looper.myLooper()`
  - `android.os.Looper.getMainLooper()`
  - `android.os.Handler`
  - a synthetic `java.lang.Runnable`

Why this shape:

- it is the smallest step toward Frida semantics
- it avoids growing the native bridge surface before existing Java primitives are exhausted
- it keeps the implementation easy to smoke-test on device

Implementation:

- in [js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp):
  - added `Java.isMainThread()`
  - added `Java.scheduleOnMainThread(fn)`
  - cached `Looper`, `Handler`, `Runnable`, and the main-thread `Handler` lazily in bootstrap state
  - `scheduleOnMainThread(...)` now:
    - validates `fn`
    - registers a synthetic `Runnable`
    - instantiates it
    - posts it through `Handler.post.overload('java.lang.Runnable')(...)`
- in [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp):
  - added regression coverage for:
    - binding existence
    - non-function rejection
    - `isMainThread()` handle comparison
    - `scheduleOnMainThread(...)` wiring through `Looper`, `Handler`, and `Runnable`
  - extended the fake Java resolver/invoker with the minimum framework coverage needed for:
    - `Looper.myLooper()`
    - `Looper.getMainLooper()`
    - `Handler.<init>(Looper)`
    - `Handler.post(Runnable)`
- added device smoke script:
  - [java_main_thread_smoke.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/java_main_thread_smoke.js)

Issue found during implementation:

- calling `Handler.post(runnable)` directly was not enough in Nook's current overload inference path
- `Java.registerClass(...)` currently produces a wrapper whose concrete class name is `java.lang.reflect.Proxy`
- that caused automatic overload inference to see `Proxy` instead of the declared interface `java.lang.Runnable`

Fix:

- switched the helper to explicit overload selection:
  - `post.overload('java.lang.Runnable')(runnable)`

Spawn-path issue found on device:

- in early `spawn --resume` timing, `Java.performNow(...)` could run before `ActivityThread.currentApplication()` became available
- `Java.scheduleOnMainThread(...)` used `Java.registerClass(...)`
- the current `registerClass(...)` helper-dex bootstrap still depends on `currentApplication()` to get:
  - app code cache dir
  - parent class loader
- result:
  - `registerClass currentApplication is unavailable`

Fix:

- `Java.scheduleOnMainThread(...)` now checks whether `currentApplication()` is available first
- if not, it defers itself through `Java.ready(...)` instead of touching `registerClass(...)` immediately
- added a desktop regression covering this exact fallback path

Main-thread detection issue found on device:

- `java-main-thread-scheduled:false` appeared even after `Handler.post(...)` and `Java.ready(...)` had both succeeded
- root cause was not thread dispatch itself
- root cause was `Java.isMainThread()` using:
  - `current.__jptr === main.__jptr`
- on JNI, two different local references may point to the same Java object while having different raw reference values
- that made same-thread object identity checks unreliable and produced false negatives

Fix:

- `Java.isMainThread()` now uses Java object equality:
  - `current.equals.overload('java.lang.Object')(main)`
- added a regression proving that:
  - different local-reference handles can still represent the same `Looper`
  - `Java.isMainThread()` must return `true` in that case

Current boundary relative to Frida:

- the public shape is now closer to Frida for common Android Java workflows
- this is still a helper-layer implementation, not a full `Java.vm`-style threading model
- if later needed, a native bridge can replace the internals without changing the public JS API

Verification completed locally:

- `g++ -std=c++17 -I . -I include -I src tests/communication/test_js_runtime_native_attach.cpp src/agent_runtime/js_runtime.cpp src/agent_runtime/script_registry.cpp src/agent_runtime/nook_native_js_bridge.cpp src/agent_runtime/nook_java_js_bridge.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp build/quickjs.o build/cutils.o build/libunicode.o build/libregexp.o build/dtoa.o -o build/test_js_runtime_native_attach.exe`
- `cmd /c build\\test_js_runtime_native_attach.exe`
