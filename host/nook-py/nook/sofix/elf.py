import struct
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Tuple


ELF_MAGIC = b"\x7fELF"
ELFCLASS64 = 2
ELFDATA2LSB = 1

PT_NULL = 0
PT_LOAD = 1
PT_DYNAMIC = 2
PF_X = 0x1
PF_W = 0x2
PF_R = 0x4

SHT_NULL = 0
SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_RELA = 4
SHT_HASH = 5
SHT_DYNAMIC = 6
SHT_NOTE = 7
SHT_NOBITS = 8
SHT_REL = 9
SHT_DYNSYM = 11
SHT_INIT_ARRAY = 14
SHT_FINI_ARRAY = 15
SHT_GNU_HASH = 0x6FFFFFF6
SHT_GNU_VERSYM = 0x6FFFFFFF
SHT_GNU_VERNEED = 0x6FFFFFFE

SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4

DT_NULL = 0
DT_NEEDED = 1
DT_PLTRELSZ = 2
DT_HASH = 4
DT_STRTAB = 5
DT_SYMTAB = 6
DT_RELA = 7
DT_RELASZ = 8
DT_RELAENT = 9
DT_STRSZ = 10
DT_SYMENT = 11
DT_INIT = 12
DT_FINI = 13
DT_SONAME = 14
DT_REL = 17
DT_RELSZ = 18
DT_RELENT = 19
DT_PLTREL = 20
DT_JMPREL = 23
DT_INIT_ARRAY = 25
DT_FINI_ARRAY = 26
DT_INIT_ARRAYSZ = 27
DT_FINI_ARRAYSZ = 28
DT_GNU_HASH = 0x6FFFFEF5
DT_VERSYM = 0x6FFFFFF0
DT_VERNEED = 0x6FFFFFFE
DT_VERNEEDNUM = 0x6FFFFFFF

ELF64_HEADER_FORMAT = "<16sHHIQQQIHHHHHH"
ELF64_PROGRAM_HEADER_FORMAT = "<IIQQQQQQ"
ELF64_SECTION_HEADER_FORMAT = "<IIQQQQIIQQ"
ELF64_DYNAMIC_ENTRY_FORMAT = "<QQ"
ELF64_HEADER_SIZE = struct.calcsize(ELF64_HEADER_FORMAT)
ELF64_PROGRAM_HEADER_SIZE = struct.calcsize(ELF64_PROGRAM_HEADER_FORMAT)
ELF64_SECTION_HEADER_SIZE = struct.calcsize(ELF64_SECTION_HEADER_FORMAT)
ELF64_DYNAMIC_ENTRY_SIZE = struct.calcsize(ELF64_DYNAMIC_ENTRY_FORMAT)
ELF64_SYMBOL_SIZE = 0x18
ELF64_RELA_SIZE = 0x18
ELF64_REL_SIZE = 0x10


@dataclass
class Elf64Header:
    e_ident: bytes
    e_type: int
    e_machine: int
    e_version: int
    e_entry: int
    e_phoff: int
    e_shoff: int
    e_flags: int
    e_ehsize: int
    e_phentsize: int
    e_phnum: int
    e_shentsize: int
    e_shnum: int
    e_shstrndx: int


@dataclass
class Elf64ProgramHeader:
    p_type: int
    p_flags: int
    p_offset: int
    p_vaddr: int
    p_paddr: int
    p_filesz: int
    p_memsz: int
    p_align: int


@dataclass
class Elf64SectionHeader:
    sh_name: int
    sh_type: int
    sh_flags: int
    sh_addr: int
    sh_offset: int
    sh_size: int
    sh_link: int
    sh_info: int
    sh_addralign: int
    sh_entsize: int


@dataclass
class Elf64DynamicEntry:
    d_tag: int
    d_val: int


@dataclass
class Elf64SysvHashInfo:
    nbucket: int
    nchain: int
    size: int
    symbol_count: int


@dataclass
class Elf64GnuHashInfo:
    nbuckets: int
    symoffset: int
    bloom_size: int
    bloom_shift: int
    size: int
    symbol_count: int


@dataclass
class Elf64VerneedInfo:
    count: int
    size: int


def parse_elf64_header(data: bytes) -> Elf64Header:
    if len(data) < ELF64_HEADER_SIZE:
        raise ValueError("ELF image is too small")
    values = struct.unpack_from(ELF64_HEADER_FORMAT, data, 0)
    e_ident = values[0]
    if e_ident[:4] != ELF_MAGIC:
        raise ValueError("ELF magic is invalid")
    if e_ident[4] != ELFCLASS64:
        raise NotImplementedError("only ELF64 is supported")
    if e_ident[5] != ELFDATA2LSB:
        raise NotImplementedError("only little-endian ELF is supported")
    return Elf64Header(*values)


def parse_elf64_program_headers(data: bytes, header: Elf64Header) -> List[Elf64ProgramHeader]:
    if header.e_phentsize != ELF64_PROGRAM_HEADER_SIZE:
        raise ValueError("unexpected ELF64 program header entry size")
    entries = []
    phoff = int(header.e_phoff)
    for index in range(int(header.e_phnum)):
        entry_offset = phoff + (index * header.e_phentsize)
        if entry_offset + header.e_phentsize > len(data):
            raise ValueError("program header table is truncated")
        entries.append(Elf64ProgramHeader(*struct.unpack_from(ELF64_PROGRAM_HEADER_FORMAT, data, entry_offset)))
    return entries


def parse_elf64_section_headers(data: bytes, header: Elf64Header) -> List[Elf64SectionHeader]:
    if header.e_shnum == 0:
        return []
    if header.e_shentsize != ELF64_SECTION_HEADER_SIZE:
        raise ValueError("unexpected ELF64 section header entry size")
    entries = []
    shoff = int(header.e_shoff)
    for index in range(int(header.e_shnum)):
        entry_offset = shoff + (index * header.e_shentsize)
        if entry_offset + header.e_shentsize > len(data):
            raise ValueError("section header table is truncated")
        entries.append(Elf64SectionHeader(*struct.unpack_from(ELF64_SECTION_HEADER_FORMAT, data, entry_offset)))
    return entries


def parse_elf64_dynamic_entries(data: bytes, program_headers: Iterable[Elf64ProgramHeader]) -> List[Elf64DynamicEntry]:
    dynamic_header = next((entry for entry in program_headers if entry.p_type == PT_DYNAMIC), None)
    if dynamic_header is None:
        return []
    start = int(dynamic_header.p_offset)
    size = int(dynamic_header.p_filesz or dynamic_header.p_memsz)
    end = min(start + size, len(data))
    if start >= len(data) or start >= end:
        raise ValueError("dynamic segment is outside the dumped image")
    entries = []
    offset = start
    while offset + ELF64_DYNAMIC_ENTRY_SIZE <= end:
        item = Elf64DynamicEntry(*struct.unpack_from(ELF64_DYNAMIC_ENTRY_FORMAT, data, offset))
        entries.append(item)
        offset += ELF64_DYNAMIC_ENTRY_SIZE
        if item.d_tag == DT_NULL:
            break
    return entries


def dynamic_entries_to_map(entries: Iterable[Elf64DynamicEntry]) -> Dict[int, int]:
    return {entry.d_tag: entry.d_val for entry in entries if entry.d_tag != DT_NULL}


def parse_elf64_sysv_hash(data: bytes, offset: int) -> Elf64SysvHashInfo:
    if offset < 0 or offset + 8 > len(data):
        raise ValueError("sysv hash header is truncated")
    nbucket, nchain = struct.unpack_from("<LL", data, offset)
    size = (2 + nbucket + nchain) * 4
    if offset + size > len(data):
        raise ValueError("sysv hash table is truncated")
    return Elf64SysvHashInfo(
        nbucket=nbucket,
        nchain=nchain,
        size=size,
        symbol_count=nchain,
    )


def parse_elf64_gnu_hash(data: bytes, offset: int) -> Elf64GnuHashInfo:
    if offset < 0 or offset + 16 > len(data):
        raise ValueError("gnu hash header is truncated")
    nbuckets, symoffset, bloom_size, bloom_shift = struct.unpack_from("<LLLL", data, offset)
    bloom_bytes = bloom_size * 8
    buckets_offset = offset + 16 + bloom_bytes
    buckets_end = buckets_offset + (nbuckets * 4)
    if buckets_end > len(data):
        raise ValueError("gnu hash bucket table is truncated")

    buckets = list(struct.unpack_from("<" + ("L" * nbuckets), data, buckets_offset)) if nbuckets > 0 else []
    chains_offset = buckets_end
    highest_index = symoffset - 1
    for bucket in buckets:
        if bucket < symoffset:
            continue
        chain_index = bucket - symoffset
        cursor = chains_offset + (chain_index * 4)
        while True:
            if cursor + 4 > len(data):
                raise ValueError("gnu hash chain table is truncated")
            value = struct.unpack_from("<L", data, cursor)[0]
            symbol_index = symoffset + ((cursor - chains_offset) // 4)
            if symbol_index > highest_index:
                highest_index = symbol_index
            cursor += 4
            if value & 1:
                break

    chain_words = 0 if highest_index < symoffset else (highest_index - symoffset + 1)
    size = 16 + bloom_bytes + (nbuckets * 4) + (chain_words * 4)
    if offset + size > len(data):
        raise ValueError("gnu hash table is truncated")
    symbol_count = max(symoffset, highest_index + 1) if chain_words > 0 else symoffset
    return Elf64GnuHashInfo(
        nbuckets=nbuckets,
        symoffset=symoffset,
        bloom_size=bloom_size,
        bloom_shift=bloom_shift,
        size=size,
        symbol_count=symbol_count,
    )


def parse_elf64_verneed(data: bytes, offset: int, count: int) -> Elf64VerneedInfo:
    if count <= 0:
        raise ValueError("verneed count must be positive")
    cursor = offset
    end = offset
    for _ in range(count):
        if cursor < 0 or cursor + 16 > len(data):
            raise ValueError("verneed entry is truncated")
        vn_version, vn_cnt, vn_file, vn_aux, vn_next = struct.unpack_from("<HHLLL", data, cursor)
        del vn_version, vn_file
        if vn_cnt <= 0:
            raise ValueError("verneed entry has no vernaux records")
        aux_cursor = cursor + vn_aux
        for aux_index in range(vn_cnt):
            if aux_cursor < 0 or aux_cursor + 16 > len(data):
                raise ValueError("vernaux entry is truncated")
            _, _, _, _, vna_next = struct.unpack_from("<LHHLL", data, aux_cursor)
            end = max(end, aux_cursor + 16)
            if aux_index + 1 == vn_cnt:
                break
            if vna_next <= 0:
                raise ValueError("vernaux chain terminated before vn_cnt entries")
            aux_cursor += vna_next
        end = max(end, cursor + 16)
        if vn_next == 0:
            cursor += 16
            break
        cursor += vn_next
    return Elf64VerneedInfo(count=count, size=max(0, end - offset))


def replace_elf64_program_headers(data: bytes, header: Elf64Header, program_headers: List[Elf64ProgramHeader]) -> bytes:
    output = bytearray(data)
    phoff = int(header.e_phoff)
    for index, entry in enumerate(program_headers):
        entry_offset = phoff + (index * header.e_phentsize)
        struct.pack_into(
            ELF64_PROGRAM_HEADER_FORMAT,
            output,
            entry_offset,
            entry.p_type,
            entry.p_flags,
            entry.p_offset,
            entry.p_vaddr,
            entry.p_paddr,
            entry.p_filesz,
            entry.p_memsz,
            entry.p_align,
        )
    return bytes(output)


def replace_elf64_header(data: bytes, header: Elf64Header) -> bytes:
    output = bytearray(data)
    struct.pack_into(
        ELF64_HEADER_FORMAT,
        output,
        0,
        header.e_ident,
        header.e_type,
        header.e_machine,
        header.e_version,
        header.e_entry,
        header.e_phoff,
        header.e_shoff,
        header.e_flags,
        header.e_ehsize,
        header.e_phentsize,
        header.e_phnum,
        header.e_shentsize,
        header.e_shnum,
        header.e_shstrndx,
    )
    return bytes(output)


def append_elf64_section_headers(data: bytes, section_headers: List[Elf64SectionHeader], shstrtab: bytes) -> Tuple[bytes, int, int]:
    output = bytearray(data)
    output.extend(shstrtab)
    while len(output) % 8 != 0:
        output.append(0)
    section_table_offset = len(output)
    for entry in section_headers:
        output.extend(
            struct.pack(
                ELF64_SECTION_HEADER_FORMAT,
                entry.sh_name,
                entry.sh_type,
                entry.sh_flags,
                entry.sh_addr,
                entry.sh_offset,
                entry.sh_size,
                entry.sh_link,
                entry.sh_info,
                entry.sh_addralign,
                entry.sh_entsize,
            )
        )
    return bytes(output), section_table_offset, len(section_headers) - 1


def infer_file_region_size(start: int, candidates: Iterable[Optional[int]], limit: int) -> int:
    valid_candidates = sorted(candidate for candidate in candidates if candidate is not None and candidate > start)
    end = valid_candidates[0] if valid_candidates else limit
    return max(0, min(end, limit) - start)
