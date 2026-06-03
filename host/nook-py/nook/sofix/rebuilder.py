from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

from .elf import (
    DT_FINI_ARRAY,
    DT_FINI_ARRAYSZ,
    DT_GNU_HASH,
    DT_HASH,
    DT_INIT_ARRAY,
    DT_INIT_ARRAYSZ,
    DT_JMPREL,
    DT_NULL,
    DT_PLTREL,
    DT_PLTRELSZ,
    DT_REL,
    DT_RELA,
    DT_RELASZ,
    DT_RELSZ,
    DT_STRTAB,
    DT_STRSZ,
    DT_SYMENT,
    DT_SYMTAB,
    DT_VERSYM,
    DT_VERNEED,
    DT_VERNEEDNUM,
    ELF64_REL_SIZE,
    ELF64_RELA_SIZE,
    ELF64_SECTION_HEADER_SIZE,
    ELF64_SYMBOL_SIZE,
    PF_W,
    PF_X,
    PT_DYNAMIC,
    PT_LOAD,
    SHF_ALLOC,
    SHF_EXECINSTR,
    SHF_WRITE,
    SHT_DYNAMIC,
    SHT_DYNSYM,
    SHT_FINI_ARRAY,
    SHT_GNU_HASH,
    SHT_HASH,
    SHT_INIT_ARRAY,
    SHT_PROGBITS,
    SHT_REL,
    SHT_RELA,
    SHT_STRTAB,
    SHT_GNU_VERNEED,
    SHT_GNU_VERSYM,
    Elf64DynamicEntry,
    Elf64Header,
    Elf64ProgramHeader,
    Elf64SectionHeader,
    append_elf64_section_headers,
    dynamic_entries_to_map,
    infer_file_region_size,
    parse_elf64_gnu_hash,
    parse_elf64_dynamic_entries,
    parse_elf64_verneed,
    parse_elf64_header,
    parse_elf64_program_headers,
    parse_elf64_sysv_hash,
    replace_elf64_header,
    replace_elf64_program_headers,
)


@dataclass
class RepairResult:
    data: bytes
    modified_program_headers: int
    warnings: List[str] = field(default_factory=list)
    synthesized_sections: int = 0


@dataclass
class _SyntheticSection:
    name: str
    sh_type: int
    sh_flags: int
    sh_addr: int
    sh_offset: int
    sh_size: int
    sh_link_name: Optional[str] = None
    sh_info: int = 0
    sh_addralign: int = 1
    sh_entsize: int = 0


def _find_dynamic_header(program_headers: List[Elf64ProgramHeader]) -> Optional[Elf64ProgramHeader]:
    return next((entry for entry in program_headers if entry.p_type == PT_DYNAMIC), None)


def _dynamic_header_in_loadable_segment(
    dynamic_header: Optional[Elf64ProgramHeader],
    program_headers: List[Elf64ProgramHeader],
) -> bool:
    if dynamic_header is None:
        return False
    load_ranges = [
        (int(entry.p_vaddr), int(entry.p_vaddr) + int(entry.p_memsz or entry.p_filesz))
        for entry in program_headers
        if entry.p_type == PT_LOAD and int(entry.p_memsz or entry.p_filesz) > 0
    ]
    if not load_ranges:
        return False
    start = int(dynamic_header.p_vaddr)
    end = start + int(dynamic_header.p_memsz or dynamic_header.p_filesz)
    return any(load_start <= start and end <= load_end for load_start, load_end in load_ranges)


def _load_base_dynamic_data(
    base_so_data: Optional[bytes] = None,
    base_so_path: Optional[str] = None,
) -> Optional[bytes]:
    if base_so_data is None and not base_so_path:
        return None
    if base_so_data is None:
        with open(base_so_path, "rb") as handle:
            base_so_data = handle.read()
    base_header = parse_elf64_header(base_so_data)
    base_headers = parse_elf64_program_headers(base_so_data, base_header)
    base_dynamic_header = _find_dynamic_header(base_headers)
    if base_dynamic_header is None:
        raise ValueError("base so does not contain PT_DYNAMIC")
    start = int(base_dynamic_header.p_offset)
    size = int(base_dynamic_header.p_filesz or base_dynamic_header.p_memsz)
    end = start + size
    if start < 0 or end > len(base_so_data):
        raise ValueError("base so PT_DYNAMIC is truncated")
    return bytes(base_so_data[start:end])


def _apply_base_dynamic_data(
    data: bytes,
    program_headers: List[Elf64ProgramHeader],
    dynamic_data: bytes,
) -> Tuple[bytes, List[Elf64ProgramHeader], int]:
    dynamic_header = _find_dynamic_header(program_headers)
    if dynamic_header is None or not dynamic_data:
        return data, program_headers, 0

    append_offset = _align_up(len(data), 8)
    output = bytearray(data)
    if append_offset > len(output):
        output.extend(b"\x00" * (append_offset - len(output)))
    output.extend(dynamic_data)

    updated_headers: List[Elf64ProgramHeader] = []
    modified_program_headers = 0
    for entry in program_headers:
        if entry.p_type != PT_DYNAMIC:
            updated_headers.append(entry)
            continue
        updated_entry = entry.__class__(
            p_type=entry.p_type,
            p_flags=entry.p_flags,
            p_offset=append_offset,
            p_vaddr=append_offset,
            p_paddr=append_offset,
            p_filesz=len(dynamic_data),
            p_memsz=len(dynamic_data),
            p_align=entry.p_align,
        )
        if updated_entry != entry:
            modified_program_headers += 1
        updated_headers.append(updated_entry)
    return bytes(output), updated_headers, modified_program_headers


def _find_load_limit(address: int, program_headers: List[Elf64ProgramHeader], image_size: int) -> int:
    for entry in program_headers:
        if entry.p_type != PT_LOAD:
            continue
        start = int(entry.p_vaddr)
        end = start + int(entry.p_filesz or entry.p_memsz)
        if start <= address < end:
            return min(end, image_size)
    return image_size


def _compute_dump_load_sizes(
    program_headers: List[Elf64ProgramHeader],
    image_size: int,
) -> Dict[int, int]:
    load_headers = [
        (index, entry)
        for index, entry in enumerate(program_headers)
        if entry.p_type == PT_LOAD
    ]
    load_headers.sort(key=lambda item: int(item[1].p_vaddr))
    expanded_sizes: Dict[int, int] = {}
    for position, (index, entry) in enumerate(load_headers):
        start = int(entry.p_vaddr)
        if start >= image_size:
            continue
        if position + 1 < len(load_headers):
            next_start = int(load_headers[position + 1][1].p_vaddr)
            size = max(0, next_start - start)
        else:
            size = max(0, image_size - start)
        if size > 0:
            expanded_sizes[index] = size
    return expanded_sizes


def _collect_known_dynamic_addresses(dynamic_map: Dict[int, int]) -> List[int]:
    addresses = []
    for tag in (
        DT_HASH,
        DT_GNU_HASH,
        DT_STRTAB,
        DT_SYMTAB,
        DT_RELA,
        DT_REL,
        DT_JMPREL,
        DT_INIT_ARRAY,
        DT_FINI_ARRAY,
        DT_VERSYM,
        DT_VERNEED,
    ):
        value = dynamic_map.get(tag)
        if value:
            addresses.append(int(value))
    return sorted(set(addresses))


def _parse_sysv_hash_info(dynamic_map: Dict[int, int], image: bytes) -> Tuple[Optional[object], Optional[str]]:
    hash_addr = dynamic_map.get(DT_HASH)
    if not hash_addr:
        return None, None
    try:
        return parse_elf64_sysv_hash(image, int(hash_addr)), None
    except ValueError as exc:
        return None, str(exc)


def _parse_gnu_hash_info(dynamic_map: Dict[int, int], image: bytes) -> Tuple[Optional[object], Optional[str]]:
    gnu_hash_addr = dynamic_map.get(DT_GNU_HASH)
    if not gnu_hash_addr:
        return None, None
    try:
        return parse_elf64_gnu_hash(image, int(gnu_hash_addr)), None
    except ValueError as exc:
        return None, str(exc)


def _parse_verneed_info(dynamic_map: Dict[int, int], image: bytes) -> Tuple[Optional[object], Optional[str]]:
    verneed_addr = dynamic_map.get(DT_VERNEED)
    verneed_count = int(dynamic_map.get(DT_VERNEEDNUM) or 0)
    if not verneed_addr or verneed_count <= 0:
        return None, None
    try:
        return parse_elf64_verneed(image, int(verneed_addr), verneed_count), None
    except ValueError as exc:
        return None, str(exc)


def _infer_symbol_table_size(
    dynamic_map: Dict[int, int],
    program_headers: List[Elf64ProgramHeader],
    image: bytes,
    sysv_hash_info=None,
    gnu_hash_info=None,
) -> Tuple[int, List[str]]:
    warnings: List[str] = []
    symtab = dynamic_map.get(DT_SYMTAB)
    if not symtab:
        return 0, warnings
    symbol_size = int(dynamic_map.get(DT_SYMENT) or ELF64_SYMBOL_SIZE)
    if symbol_size <= 0:
        symbol_size = ELF64_SYMBOL_SIZE
    if sysv_hash_info is not None and getattr(sysv_hash_info, "symbol_count", 0) > 0:
        return int(sysv_hash_info.symbol_count) * symbol_size, warnings
    if gnu_hash_info is not None and getattr(gnu_hash_info, "symbol_count", 0) > 0:
        return int(gnu_hash_info.symbol_count) * symbol_size, warnings

    image_size = len(image)
    limit = _find_load_limit(int(symtab), program_headers, image_size)
    candidates = _collect_known_dynamic_addresses(dynamic_map)
    size = infer_file_region_size(int(symtab), candidates, limit)
    if size == 0:
        warnings.append("unable to infer .dynsym size from dynamic layout")
        return 0, warnings
    size -= size % symbol_size
    if size == 0:
        warnings.append("inferred .dynsym region is smaller than one symbol entry")
    return size, warnings


def _build_dynamic_sections(
    header: Elf64Header,
    program_headers: List[Elf64ProgramHeader],
    dynamic_entries: List[Elf64DynamicEntry],
    image: bytes,
) -> Tuple[List[_SyntheticSection], List[str]]:
    del header
    warnings: List[str] = []
    dynamic_map = dynamic_entries_to_map(dynamic_entries)
    image_size = len(image)
    sysv_hash_info, sysv_hash_warning = _parse_sysv_hash_info(dynamic_map, image)
    gnu_hash_info, gnu_hash_warning = _parse_gnu_hash_info(dynamic_map, image)
    verneed_info, verneed_warning = _parse_verneed_info(dynamic_map, image)
    if sysv_hash_warning:
        warnings.append(f"sysv hash parse failed: {sysv_hash_warning}")
    if gnu_hash_warning:
        warnings.append(f"gnu hash parse failed: {gnu_hash_warning}")
    if verneed_warning:
        warnings.append(f"verneed parse failed: {verneed_warning}")
    sections: List[_SyntheticSection] = []
    dynamic_header = _find_dynamic_header(program_headers)
    if dynamic_header is not None:
        sections.append(
            _SyntheticSection(
                name=".dynamic",
                sh_type=SHT_DYNAMIC,
                sh_flags=SHF_ALLOC | SHF_WRITE,
                sh_addr=int(dynamic_header.p_vaddr),
                sh_offset=int(dynamic_header.p_offset),
                sh_size=int(dynamic_header.p_filesz or dynamic_header.p_memsz),
                sh_link_name=".dynstr",
                sh_addralign=8,
                sh_entsize=16,
            )
        )

    strtab = dynamic_map.get(DT_STRTAB)
    strsz = int(dynamic_map.get(DT_STRSZ) or 0)
    if strtab and strsz > 0:
        sections.append(
            _SyntheticSection(
                name=".dynstr",
                sh_type=SHT_STRTAB,
                sh_flags=SHF_ALLOC,
                sh_addr=int(strtab),
                sh_offset=int(strtab),
                sh_size=strsz,
                sh_addralign=1,
            )
        )

    symtab = dynamic_map.get(DT_SYMTAB)
    dynsym_symbol_count = 0
    if symtab:
        dynsym_entsize = int(dynamic_map.get(DT_SYMENT) or ELF64_SYMBOL_SIZE)
        dynsym_size, dynsym_warnings = _infer_symbol_table_size(
            dynamic_map,
            program_headers,
            image,
            sysv_hash_info=sysv_hash_info,
            gnu_hash_info=gnu_hash_info,
        )
        warnings.extend(dynsym_warnings)
        if dynsym_size > 0:
            dynsym_symbol_count = dynsym_size // max(1, dynsym_entsize)
            sections.append(
                _SyntheticSection(
                    name=".dynsym",
                    sh_type=SHT_DYNSYM,
                    sh_flags=SHF_ALLOC,
                    sh_addr=int(symtab),
                    sh_offset=int(symtab),
                    sh_size=dynsym_size,
                    sh_link_name=".dynstr",
                    sh_addralign=8,
                    sh_entsize=dynsym_entsize,
                )
            )

    if dynamic_map.get(DT_HASH):
        hash_addr = int(dynamic_map[DT_HASH])
        limit = _find_load_limit(hash_addr, program_headers, image_size)
        size = int(getattr(sysv_hash_info, "size", 0) or 0)
        if size == 0:
            size = infer_file_region_size(hash_addr, _collect_known_dynamic_addresses(dynamic_map), limit)
        if size > 0:
            sections.append(
                _SyntheticSection(
                    name=".hash",
                    sh_type=SHT_HASH,
                    sh_flags=SHF_ALLOC,
                    sh_addr=hash_addr,
                    sh_offset=hash_addr,
                    sh_size=size,
                    sh_link_name=".dynsym",
                    sh_addralign=8,
                    sh_entsize=4,
                )
            )

    if dynamic_map.get(DT_GNU_HASH):
        gnu_hash_addr = int(dynamic_map[DT_GNU_HASH])
        limit = _find_load_limit(gnu_hash_addr, program_headers, image_size)
        size = int(getattr(gnu_hash_info, "size", 0) or 0)
        if size == 0:
            size = infer_file_region_size(gnu_hash_addr, _collect_known_dynamic_addresses(dynamic_map), limit)
        if size > 0:
            sections.append(
                _SyntheticSection(
                    name=".gnu.hash",
                    sh_type=SHT_GNU_HASH,
                    sh_flags=SHF_ALLOC,
                    sh_addr=gnu_hash_addr,
                    sh_offset=gnu_hash_addr,
                    sh_size=size,
                    sh_link_name=".dynsym",
                    sh_addralign=8,
                    sh_entsize=0,
                )
            )

    rela_addr = dynamic_map.get(DT_RELA)
    rela_size = int(dynamic_map.get(DT_RELASZ) or 0)
    if rela_addr and rela_size > 0:
        sections.append(
            _SyntheticSection(
                name=".rela.dyn",
                sh_type=SHT_RELA,
                sh_flags=SHF_ALLOC,
                sh_addr=int(rela_addr),
                sh_offset=int(rela_addr),
                sh_size=rela_size,
                sh_link_name=".dynsym",
                sh_addralign=8,
                sh_entsize=ELF64_RELA_SIZE,
            )
        )

    rel_addr = dynamic_map.get(DT_REL)
    rel_size = int(dynamic_map.get(DT_RELSZ) or 0)
    if rel_addr and rel_size > 0:
        sections.append(
            _SyntheticSection(
                name=".rel.dyn",
                sh_type=SHT_REL,
                sh_flags=SHF_ALLOC,
                sh_addr=int(rel_addr),
                sh_offset=int(rel_addr),
                sh_size=rel_size,
                sh_link_name=".dynsym",
                sh_addralign=8,
                sh_entsize=ELF64_REL_SIZE,
            )
        )

    jmprel_addr = dynamic_map.get(DT_JMPREL)
    jmprel_size = int(dynamic_map.get(DT_PLTRELSZ) or 0)
    plt_type = int(dynamic_map.get(DT_PLTREL) or DT_NULL)
    if jmprel_addr and jmprel_size > 0:
        if plt_type == DT_RELA:
            rela_count = jmprel_size // ELF64_REL_SIZE
            sections.append(
                _SyntheticSection(
                    name=".rela.plt",
                    sh_type=SHT_RELA,
                    sh_flags=SHF_ALLOC,
                    sh_addr=int(jmprel_addr),
                    sh_offset=int(jmprel_addr),
                    sh_size=rela_count * ELF64_RELA_SIZE if rela_count > 0 else jmprel_size,
                    sh_link_name=".dynsym",
                    sh_addralign=8,
                    sh_entsize=ELF64_RELA_SIZE,
                )
            )
        elif plt_type == DT_REL:
            sections.append(
                _SyntheticSection(
                    name=".rel.plt",
                    sh_type=SHT_REL,
                    sh_flags=SHF_ALLOC,
                    sh_addr=int(jmprel_addr),
                    sh_offset=int(jmprel_addr),
                    sh_size=jmprel_size,
                    sh_link_name=".dynsym",
                    sh_addralign=8,
                    sh_entsize=ELF64_REL_SIZE,
                )
            )

    init_array = dynamic_map.get(DT_INIT_ARRAY)
    init_array_size = int(dynamic_map.get(DT_INIT_ARRAYSZ) or 0)
    if init_array and init_array_size > 0:
        sections.append(
            _SyntheticSection(
                name=".init_array",
                sh_type=SHT_INIT_ARRAY,
                sh_flags=SHF_ALLOC | SHF_WRITE,
                sh_addr=int(init_array),
                sh_offset=int(init_array),
                sh_size=init_array_size,
                sh_addralign=8,
                sh_entsize=8,
            )
        )

    fini_array = dynamic_map.get(DT_FINI_ARRAY)
    fini_array_size = int(dynamic_map.get(DT_FINI_ARRAYSZ) or 0)
    if fini_array and fini_array_size > 0:
        sections.append(
            _SyntheticSection(
                name=".fini_array",
                sh_type=SHT_FINI_ARRAY,
                sh_flags=SHF_ALLOC | SHF_WRITE,
                sh_addr=int(fini_array),
                sh_offset=int(fini_array),
                sh_size=fini_array_size,
                sh_addralign=8,
                sh_entsize=8,
            )
        )

    versym_addr = dynamic_map.get(DT_VERSYM)
    if versym_addr:
        versym_addr = int(versym_addr)
        limit = _find_load_limit(versym_addr, program_headers, image_size)
        if dynsym_symbol_count > 0:
            size = dynsym_symbol_count * 2
        else:
            size = infer_file_region_size(versym_addr, _collect_known_dynamic_addresses(dynamic_map), limit)
        if size > 0:
            sections.append(
                _SyntheticSection(
                    name=".gnu.version",
                    sh_type=SHT_GNU_VERSYM,
                    sh_flags=SHF_ALLOC,
                    sh_addr=versym_addr,
                    sh_offset=versym_addr,
                    sh_size=size,
                    sh_link_name=".dynsym",
                    sh_addralign=2,
                    sh_entsize=2,
                )
            )

    verneed_addr = dynamic_map.get(DT_VERNEED)
    if verneed_addr:
        verneed_addr = int(verneed_addr)
        limit = _find_load_limit(verneed_addr, program_headers, image_size)
        size = int(getattr(verneed_info, "size", 0) or 0)
        if size == 0:
            size = infer_file_region_size(verneed_addr, _collect_known_dynamic_addresses(dynamic_map), limit)
        if size > 0:
            sections.append(
                _SyntheticSection(
                    name=".gnu.version_r",
                    sh_type=SHT_GNU_VERNEED,
                    sh_flags=SHF_ALLOC,
                    sh_addr=verneed_addr,
                    sh_offset=verneed_addr,
                    sh_size=size,
                    sh_link_name=".dynstr",
                    sh_addralign=8,
                )
            )

    sections.sort(key=lambda entry: (entry.sh_offset, entry.name))
    deduped: List[_SyntheticSection] = []
    seen = set()
    for section in sections:
        key = (section.name, section.sh_offset, section.sh_size)
        if key in seen:
            continue
        seen.add(key)
        deduped.append(section)
    return deduped, warnings


def _merge_intervals(intervals: List[Tuple[int, int]]) -> List[Tuple[int, int]]:
    if not intervals:
        return []
    merged = []
    for start, end in sorted(intervals):
        if not merged or start > merged[-1][1]:
            merged.append([start, end])
        else:
            merged[-1][1] = max(merged[-1][1], end)
    return [(start, end) for start, end in merged]


def _subtract_intervals(start: int, end: int, occupied: List[Tuple[int, int]]) -> List[Tuple[int, int]]:
    cursor = start
    segments: List[Tuple[int, int]] = []
    for occ_start, occ_end in _merge_intervals(occupied):
        if occ_end <= cursor:
            continue
        if occ_start > cursor:
            segments.append((cursor, min(occ_start, end)))
        cursor = max(cursor, occ_end)
        if cursor >= end:
            break
    if cursor < end:
        segments.append((cursor, end))
    return [(seg_start, seg_end) for seg_start, seg_end in segments if seg_end > seg_start]


def _segment_section_name(prefix: str, index: int) -> str:
    return prefix if index == 0 else f"{prefix}.{index}"


def _align_up(value: int, alignment: int) -> int:
    if alignment <= 1:
        return value
    return (value + (alignment - 1)) & ~(alignment - 1)


def _find_section(sections: List[_SyntheticSection], name: str) -> Optional[_SyntheticSection]:
    for section in sections:
        if section.name == name:
            return section
    return None


def _find_first_writable_load(program_headers: List[Elf64ProgramHeader]) -> Optional[Elf64ProgramHeader]:
    for entry in program_headers:
        if entry.p_type == PT_LOAD and (entry.p_flags & PF_W):
            return entry
    return None


def _add_sofix_style_exec_sections(
    program_headers: List[Elf64ProgramHeader],
    dynamic_map: Dict[int, int],
    sections: List[_SyntheticSection],
) -> Tuple[List[_SyntheticSection], bool]:
    plt_reloc = _find_section(sections, ".rela.plt") or _find_section(sections, ".rel.plt")
    if plt_reloc is None:
        return sections, False

    plt_relsz = int(dynamic_map.get(DT_PLTRELSZ) or 0)
    if plt_relsz <= 0:
        return sections, False

    # Match the current SoFixer sizing heuristic for parity: it derives PLT
    # entry count using the REL-sized stride even when the relocation table is RELA.
    plt_count = plt_relsz // ELF64_REL_SIZE
    if plt_count <= 0:
        return sections, False

    plt_start = plt_reloc.sh_offset + plt_reloc.sh_size
    plt_size = 0x14 + (0x0C * plt_count)
    plt_end = plt_start + plt_size

    writable_candidates = [
        int(value)
        for value in (
            dynamic_map.get(DT_INIT_ARRAY),
            dynamic_map.get(DT_FINI_ARRAY),
            dynamic_map.get(DT_JMPREL),
            dynamic_map.get(DT_RELA),
            dynamic_map.get(DT_REL),
        )
        if value and int(value) > plt_end
    ]
    rw_load = _find_first_writable_load(program_headers)
    if rw_load is not None:
        writable_candidates.append(int(rw_load.p_offset))
    if not writable_candidates:
        return sections, False

    text_end = min(writable_candidates)
    text_start = _align_up(plt_end, 8)
    if text_end <= plt_start:
        return sections, False

    sections = list(sections)
    sections.append(
        _SyntheticSection(
            name=".plt",
            sh_type=SHT_PROGBITS,
            sh_flags=SHF_ALLOC | SHF_EXECINSTR,
            sh_addr=plt_start,
            sh_offset=plt_start,
            sh_size=max(0, min(plt_size, max(0, text_end - plt_start))),
            sh_addralign=4,
        )
    )
    if text_end > text_start:
        sections.append(
            _SyntheticSection(
                name=".text&ARM.extab",
                sh_type=SHT_PROGBITS,
                sh_flags=SHF_ALLOC | SHF_EXECINSTR,
                sh_addr=text_start,
                sh_offset=text_start,
                sh_size=text_end - text_start,
                sh_addralign=8,
            )
        )
    return sections, True


def _add_sofix_style_data_section(
    program_headers: List[Elf64ProgramHeader],
    sections: List[_SyntheticSection],
) -> Tuple[List[_SyntheticSection], bool]:
    rw_load = _find_first_writable_load(program_headers)
    dynamic_section = _find_section(sections, ".dynamic")
    if rw_load is None or dynamic_section is None:
        return sections, False

    data_start = dynamic_section.sh_offset + dynamic_section.sh_size
    data_end = _align_up(max(
        int(entry.p_offset) + int(entry.p_filesz)
        for entry in program_headers
        if entry.p_type == PT_LOAD
    ), 0x1000)
    if data_end <= data_start:
        return sections, False

    sections = list(sections)
    sections.append(
        _SyntheticSection(
            name=".data",
            sh_type=SHT_PROGBITS,
            sh_flags=SHF_ALLOC | SHF_WRITE,
            sh_addr=data_start,
            sh_offset=data_start,
            sh_size=data_end - data_start,
            sh_addralign=4,
        )
    )
    return sections, True


def _build_segment_sections(
    program_headers: List[Elf64ProgramHeader],
    dynamic_map: Dict[int, int],
    synthetic_sections: List[_SyntheticSection],
) -> List[_SyntheticSection]:
    sections = list(synthetic_sections)
    sections, has_sofix_exec_sections = _add_sofix_style_exec_sections(program_headers, dynamic_map, sections)
    has_sofix_data_section = False
    if has_sofix_exec_sections:
        sections, has_sofix_data_section = _add_sofix_style_data_section(program_headers, sections)
    existing_ranges = [(section.sh_offset, section.sh_offset + section.sh_size) for section in sections if section.sh_size > 0]
    prefix_counts: Dict[str, int] = {}
    executable_load_index = 0

    for entry in program_headers:
        if entry.p_type != PT_LOAD:
            continue
        seg_start = int(entry.p_offset)
        seg_end = seg_start + int(entry.p_filesz)
        if seg_end <= seg_start:
            continue
        is_executable_load = bool(entry.p_flags & PF_X)
        if is_executable_load and has_sofix_exec_sections:
            executable_load_index += 1
            continue
        if (entry.p_flags & PF_W) and has_sofix_data_section:
            continue

        occupied = []
        for occ_start, occ_end in existing_ranges:
            overlap_start = max(seg_start, occ_start)
            overlap_end = min(seg_end, occ_end)
            if overlap_end > overlap_start:
                occupied.append((overlap_start, overlap_end))

        residual_ranges = _subtract_intervals(seg_start, seg_end, occupied)
        if not residual_ranges:
            if is_executable_load:
                executable_load_index += 1
            continue

        if entry.p_flags & PF_X:
            sh_flags = SHF_ALLOC | SHF_EXECINSTR
            align = max(16, int(entry.p_align or 16))
        elif entry.p_flags & PF_W:
            prefix = ".data"
            sh_flags = SHF_ALLOC | SHF_WRITE
            align = max(8, int(entry.p_align or 8))
        else:
            prefix = ".rodata"
            sh_flags = SHF_ALLOC
            align = max(8, int(entry.p_align or 8))

        for residual_start, residual_end in residual_ranges:
            size = residual_end - residual_start
            if size < 0x20:
                continue
            if entry.p_flags & PF_X:
                prefix = ".plt" if executable_load_index > 0 and int(entry.p_memsz) <= 0x10000 else ".text"
            name_index = prefix_counts.get(prefix, 0)
            prefix_counts[prefix] = name_index + 1
            sections.append(
                _SyntheticSection(
                    name=_segment_section_name(prefix, name_index),
                    sh_type=SHT_PROGBITS,
                    sh_flags=sh_flags,
                    sh_addr=residual_start,
                    sh_offset=residual_start,
                    sh_size=size,
                    sh_addralign=align,
                )
            )
        if is_executable_load:
            executable_load_index += 1
    return sections


def _synthesize_section_table(
    repaired_data: bytes,
    synthetic_sections: List[_SyntheticSection],
) -> Tuple[bytes, int]:
    if not synthetic_sections:
        return repaired_data, 0

    shstrtab = bytearray(b"\x00")
    section_headers: List[Elf64SectionHeader] = [
        Elf64SectionHeader(
            sh_name=0,
            sh_type=0,
            sh_flags=0,
            sh_addr=0,
            sh_offset=0,
            sh_size=0,
            sh_link=0,
            sh_info=0,
            sh_addralign=0,
            sh_entsize=0,
        )
    ]
    section_indices: Dict[str, int] = {}
    name_offsets: Dict[str, int] = {}

    for section in synthetic_sections:
        name_offsets[section.name] = len(shstrtab)
        shstrtab.extend(section.name.encode("ascii"))
        shstrtab.append(0)

    shstrtab_name_offset = len(shstrtab)
    shstrtab.extend(b".shstrtab\x00")
    shstrtab_offset = len(repaired_data)

    for section in synthetic_sections:
        section_indices[section.name] = len(section_headers)
        section_headers.append(
            Elf64SectionHeader(
                sh_name=name_offsets[section.name],
                sh_type=section.sh_type,
                sh_flags=section.sh_flags,
                sh_addr=section.sh_addr,
                sh_offset=section.sh_offset,
                sh_size=section.sh_size,
                sh_link=0,
                sh_info=section.sh_info,
                sh_addralign=section.sh_addralign,
                sh_entsize=section.sh_entsize,
            )
        )

    for section, header in zip(synthetic_sections, section_headers[1:]):
        if section.sh_link_name:
            header.sh_link = section_indices.get(section.sh_link_name, 0)

    shstrtab_index = len(section_headers)
    section_headers.append(
        Elf64SectionHeader(
            sh_name=shstrtab_name_offset,
            sh_type=SHT_STRTAB,
            sh_flags=0,
            sh_addr=0,
            sh_offset=shstrtab_offset,
            sh_size=len(shstrtab),
            sh_link=0,
            sh_info=0,
            sh_addralign=1,
            sh_entsize=0,
        )
    )

    output, section_table_offset, _ = append_elf64_section_headers(repaired_data, section_headers, bytes(shstrtab))
    return output, shstrtab_index


def rebuild_loaded_elf_image(
    data: bytes,
    base_address: Optional[int] = None,
    debug: bool = False,
    base_so_data: Optional[bytes] = None,
    base_so_path: Optional[str] = None,
) -> RepairResult:
    del debug

    header = parse_elf64_header(data)
    program_headers = parse_elf64_program_headers(data, header)
    repaired_headers = []
    modified_program_headers = 0
    warnings: List[str] = []
    dump_load_sizes = _compute_dump_load_sizes(program_headers, len(data)) if base_address is not None else {}

    for index, entry in enumerate(program_headers):
        new_offset = int(entry.p_offset)
        new_filesz = int(entry.p_filesz)
        new_memsz = int(entry.p_memsz)
        new_paddr = int(entry.p_paddr)

        if entry.p_type == PT_LOAD:
            new_offset = int(entry.p_vaddr)
            if index in dump_load_sizes:
                new_filesz = dump_load_sizes[index]
                new_memsz = dump_load_sizes[index]
            else:
                new_filesz = int(entry.p_memsz)
                new_memsz = int(entry.p_memsz)
            new_paddr = int(entry.p_vaddr)
            if new_offset >= len(data):
                raise ValueError("PT_LOAD virtual address is outside the dumped image")
            if new_offset + new_filesz > len(data):
                warnings.append("PT_LOAD memsz extends beyond dumped image size")
        elif base_address is not None:
            new_offset = int(entry.p_vaddr)
            new_filesz = int(entry.p_memsz)
            new_paddr = int(entry.p_vaddr)
        elif int(entry.p_vaddr) < len(data):
            if entry.p_type == PT_DYNAMIC or int(entry.p_offset) >= len(data):
                new_offset = int(entry.p_vaddr)
                new_paddr = int(entry.p_vaddr)

        if (
            new_offset != entry.p_offset
            or new_filesz != entry.p_filesz
            or new_memsz != entry.p_memsz
            or new_paddr != entry.p_paddr
        ):
            modified_program_headers += 1

        repaired_headers.append(
            entry.__class__(
                p_type=entry.p_type,
                p_flags=entry.p_flags,
                p_offset=new_offset,
                p_vaddr=entry.p_vaddr,
                p_paddr=new_paddr,
                p_filesz=new_filesz,
                p_memsz=new_memsz,
                p_align=entry.p_align,
            )
        )

    repaired_data = data
    dynamic_header = _find_dynamic_header(repaired_headers)
    if dynamic_header is not None and not _dynamic_header_in_loadable_segment(dynamic_header, repaired_headers):
        base_dynamic_data = _load_base_dynamic_data(base_so_data=base_so_data, base_so_path=base_so_path)
        if base_dynamic_data is not None:
            repaired_data, repaired_headers, extra_modified_headers = _apply_base_dynamic_data(
                repaired_data,
                repaired_headers,
                base_dynamic_data,
            )
            modified_program_headers += extra_modified_headers
            warnings.append("recovered PT_DYNAMIC from base so")

    repaired_data = replace_elf64_program_headers(repaired_data, header, repaired_headers)
    dynamic_entries = parse_elf64_dynamic_entries(repaired_data, repaired_headers)
    dynamic_map = dynamic_entries_to_map(dynamic_entries)
    synthetic_sections, section_warnings = _build_dynamic_sections(header, repaired_headers, dynamic_entries, repaired_data)
    synthetic_sections = _build_segment_sections(repaired_headers, dynamic_map, synthetic_sections)
    warnings.extend(section_warnings)

    if synthetic_sections:
        repaired_data, shstrtab_index = _synthesize_section_table(repaired_data, synthetic_sections)
        updated_header = Elf64Header(
            e_ident=header.e_ident,
            e_type=header.e_type,
            e_machine=header.e_machine,
            e_version=header.e_version,
            e_entry=header.e_entry,
            e_phoff=header.e_phoff,
            e_shoff=len(repaired_data) - (len(synthetic_sections) + 2) * ELF64_SECTION_HEADER_SIZE,
            e_flags=header.e_flags,
            e_ehsize=header.e_ehsize,
            e_phentsize=header.e_phentsize,
            e_phnum=header.e_phnum,
            e_shentsize=ELF64_SECTION_HEADER_SIZE,
            e_shnum=len(synthetic_sections) + 2,
            e_shstrndx=shstrtab_index,
        )
        repaired_data = replace_elf64_header(repaired_data, updated_header)

    return RepairResult(
        data=repaired_data,
        modified_program_headers=modified_program_headers,
        warnings=warnings,
        synthesized_sections=len(synthetic_sections),
    )
