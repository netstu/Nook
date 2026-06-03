# Nook SoFix Hash-Driven Repair Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add hash-driven ELF64 dynamic repair so `sofix` can derive `.hash`, `.gnu.hash`, `.dynsym`, and versioned section bounds from parsed linker metadata instead of relying primarily on address-order heuristics.

**Architecture:** Add low-level ELF64 hash parsers in `elf.py`, feed their results into a new inference context in `rebuilder.py`, and keep the current heuristic path as the fallback when parsed hash metadata is malformed or incomplete. Keep the change isolated to host-side repair; do not change transport or device runtime behavior.

**Tech Stack:** Python 3, unittest, existing `host/nook-py/nook/sofix` module, synthetic ELF64 fixtures.

---

### Task 1: Add failing SysV-hash fixture coverage

**Files:**
- Modify: `host/nook-py/tests/test_sofix.py`

**Step 1: Write the failing test**

Add a synthetic ELF64 fixture with:

- `DT_HASH`
- `DT_SYMTAB`
- `DT_STRTAB`
- `DT_SYMENT`
- enough bytes for a real SysV hash header and chain table

Add a test asserting:

- `.hash` size matches parsed `(2 + nbucket + nchain) * 4`
- `.dynsym` size matches `nchain * ELF64_SYMBOL_SIZE`

**Step 2: Run test to verify it fails**

Run:

```powershell
python .\host\nook-py\tests\test_sofix.py
```

Expected: FAIL because current code still sizes `.hash` / `.dynsym` heuristically.

**Step 3: Commit**

```powershell
git add host/nook-py/tests/test_sofix.py
git commit -m "test: add sysv hash driven sofix fixture"
```

### Task 2: Add failing GNU-hash fixture coverage

**Files:**
- Modify: `host/nook-py/tests/test_sofix.py`

**Step 1: Write the failing test**

Add a synthetic ELF64 fixture with:

- `DT_GNU_HASH`
- `DT_SYMTAB`
- `DT_STRTAB`
- `DT_SYMENT`

Add a test asserting:

- `.gnu.hash` size comes from parsed GNU-hash structure
- `.dynsym` size is derived from GNU-hash symbol chain walk when SysV hash is absent

**Step 2: Run test to verify it fails**

Run:

```powershell
python .\host\nook-py\tests\test_sofix.py
```

Expected: FAIL because current code does not parse GNU-hash semantics.

**Step 3: Commit**

```powershell
git add host/nook-py/tests/test_sofix.py
git commit -m "test: add gnu hash driven sofix fixture"
```

### Task 3: Add failing malformed-hash fallback coverage

**Files:**
- Modify: `host/nook-py/tests/test_sofix.py`

**Step 1: Write the failing test**

Add malformed SysV-hash and GNU-hash fixture variants where:

- the tables are truncated
- or the computed span exceeds image bounds

Assert:

- repair still succeeds
- a warning is emitted
- a fallback section is still synthesized when the current heuristic can do so

**Step 2: Run test to verify it fails**

Run:

```powershell
python .\host\nook-py\tests\test_sofix.py
```

Expected: FAIL because no structured hash-parse fallback warnings exist yet.

**Step 3: Commit**

```powershell
git add host/nook-py/tests/test_sofix.py
git commit -m "test: add malformed hash fallback coverage"
```

### Task 4: Add ELF64 hash parsing helpers

**Files:**
- Modify: `host/nook-py/nook/sofix/elf.py`
- Test: `host/nook-py/tests/test_sofix.py`

**Step 1: Write the minimal implementation**

Add helpers for:

- parsing SysV hash header
- computing SysV hash byte span
- parsing GNU-hash header
- computing GNU-hash byte span
- deriving dynsym count / upper bound from parsed hash data

Keep the helpers small and pure; return explicit parsed metadata or raise `ValueError` for malformed content.

**Step 2: Run focused tests**

Run:

```powershell
python .\host\nook-py\tests\test_sofix.py
```

Expected: earlier SysV / GNU-hash tests move from FAIL to partial PASS, with rebuilder still incomplete.

**Step 3: Commit**

```powershell
git add host/nook-py/nook/sofix/elf.py host/nook-py/tests/test_sofix.py
git commit -m "feat: add elf64 hash parsing helpers for sofix"
```

### Task 5: Add hash-derived inference context to rebuilder

**Files:**
- Modify: `host/nook-py/nook/sofix/rebuilder.py`
- Test: `host/nook-py/tests/test_sofix.py`

**Step 1: Write the minimal implementation**

Add a hash-derived inference context that:

- prefers exact `.hash` sizing from `DT_HASH`
- prefers exact / conservative `.gnu.hash` sizing from `DT_GNU_HASH`
- derives dynsym count from SysV hash first
- falls back to GNU-hash-derived upper bound second
- falls back to current address-order heuristic last

Use this context inside `_build_dynamic_sections()`.

**Step 2: Run focused tests**

Run:

```powershell
python .\host\nook-py\tests\test_sofix.py
```

Expected: SysV / GNU-hash tests PASS.

**Step 3: Commit**

```powershell
git add host/nook-py/nook/sofix/rebuilder.py host/nook-py/tests/test_sofix.py
git commit -m "feat: add hash driven dynsym inference to sofix"
```

### Task 6: Add version-section sizing improvements

**Files:**
- Modify: `host/nook-py/nook/sofix/rebuilder.py`
- Modify: `host/nook-py/nook/sofix/elf.py`
- Test: `host/nook-py/tests/test_sofix.py`

**Step 1: Write the failing test**

Add a fixture asserting:

- `.gnu.version` size follows known dynsym count
- `.gnu.version_r` prefers parsed `DT_VERNEED` linkage when available

**Step 2: Run test to verify it fails**

Run:

```powershell
python .\host\nook-py\tests\test_sofix.py
```

Expected: FAIL because version sections still rely on heuristic sizing.

**Step 3: Write minimal implementation**

Implement:

- `.gnu.version` sizing from dynsym count when known
- bounded `.gnu.version_r` sizing from parsed verneed chain when possible

**Step 4: Run test to verify it passes**

Run:

```powershell
python .\host\nook-py\tests\test_sofix.py
```

Expected: PASS.

**Step 5: Commit**

```powershell
git add host/nook-py/nook/sofix/rebuilder.py host/nook-py/nook/sofix/elf.py host/nook-py/tests/test_sofix.py
git commit -m "feat: improve version section sizing in sofix"
```

### Task 7: Run regression suite for host repair surfaces

**Files:**
- Modify: none unless regressions appear
- Test: `host/nook-py/tests/test_sofix.py`
- Test: `host/nook-py/tests/test_sodump.py`
- Test: `host/nook-py/tests/test_cli.py`

**Step 1: Run regression commands**

Run:

```powershell
python .\host\nook-py\tests\test_sofix.py
python .\host\nook-py\tests\test_sodump.py
python .\host\nook-py\tests\test_cli.py
```

Expected: all PASS.

**Step 2: Fix only regressions caused by the new hash-driven logic**

Keep changes scoped to repair metadata or tests. Do not widen feature scope.

**Step 3: Commit**

```powershell
git add host/nook-py/nook/sofix/rebuilder.py host/nook-py/nook/sofix/elf.py host/nook-py/tests/test_sofix.py host/nook-py/tests/test_sodump.py host/nook-py/tests/test_cli.py
git commit -m "test: lock hash driven sofix regressions"
```

### Task 8: Update user-facing documentation

**Files:**
- Modify: `docs/nook-sodump-usage.md`

**Step 1: Update documentation**

Document that `sofix` now:

- parses SysV hash metadata when present
- parses GNU-hash metadata when present
- prefers hash-derived dynsym sizing over address-order heuristics
- still falls back with warnings on malformed inputs

**Step 2: Sanity-check the wording**

Read the updated section and make sure it matches the implemented scope exactly.

**Step 3: Commit**

```powershell
git add docs/nook-sodump-usage.md
git commit -m "docs: describe hash driven sofix repair"
```

### Task 9: Record remaining boundaries

**Files:**
- Modify: `docs/plans/2026-05-28-nook-sofix-hash-driven-repair-design.md`

**Step 1: Add short residual-risk notes**

Capture what still remains after this phase:

- ELF32
- deeper relocation graph reconstruction
- loader-specific oddities beyond current dynamic recovery

**Step 2: Commit**

```powershell
git add docs/plans/2026-05-28-nook-sofix-hash-driven-repair-design.md
git commit -m "docs: record post-hash sofix boundaries"
```
