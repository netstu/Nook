import os
import struct
import sys
import unittest


TEST_ROOT = os.path.dirname(__file__)
PACKAGE_ROOT = os.path.abspath(os.path.join(TEST_ROOT, ".."))
if PACKAGE_ROOT not in sys.path:
    sys.path.insert(0, PACKAGE_ROOT)


from nook.sofix.elf import (  # noqa: E402
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
    DT_RELA,
    DT_RELASZ,
    DT_STRTAB,
    DT_STRSZ,
    DT_SYMENT,
    DT_SYMTAB,
    DT_VERNEED,
    DT_VERNEEDNUM,
    DT_VERSYM,
    ELF64_DYNAMIC_ENTRY_FORMAT,
    ELF64_HEADER_FORMAT,
    ELF64_HEADER_SIZE,
    ELF64_PROGRAM_HEADER_FORMAT,
    ELF64_SECTION_HEADER_SIZE,
    PT_DYNAMIC,
    PT_LOAD,
    SHT_DYNAMIC,
    SHT_DYNSYM,
    SHT_STRTAB,
    parse_elf64_header,
    parse_elf64_program_headers,
    parse_elf64_section_headers,
)
from nook.sofix.rebuilder import rebuild_loaded_elf_image  # noqa: E402


def make_minimal_elf64_image() -> bytes:
    image = bytearray(0x200)
    e_ident = b"\x7fELF" + bytes([2, 1, 1, 0]) + bytes(8)
    struct.pack_into(
        ELF64_HEADER_FORMAT,
        image,
        0,
        e_ident,
        3,
        0xB7,
        1,
        0,
        ELF64_HEADER_SIZE,
        0,
        0,
        ELF64_HEADER_SIZE,
        struct.calcsize(ELF64_PROGRAM_HEADER_FORMAT),
        1,
        0,
        0,
        0,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE,
        1,
        5,
        0x40,
        0,
        0,
        0x20,
        0x40,
        0x1000,
    )
    return bytes(image)


def make_dynamic_elf64_image() -> bytes:
    image = bytearray(0x400)
    e_ident = b"\x7fELF" + bytes([2, 1, 1, 0]) + bytes(8)
    struct.pack_into(
        ELF64_HEADER_FORMAT,
        image,
        0,
        e_ident,
        3,
        0xB7,
        1,
        0,
        ELF64_HEADER_SIZE,
        0,
        0,
        ELF64_HEADER_SIZE,
        struct.calcsize(ELF64_PROGRAM_HEADER_FORMAT),
        2,
        0,
        0,
        0,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE,
        PT_LOAD,
        5,
        0x40,
        0,
        0,
        0x200,
        0x200,
        0x1000,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE + struct.calcsize(ELF64_PROGRAM_HEADER_FORMAT),
        PT_DYNAMIC,
        6,
        0x120,
        0x120,
        0x120,
        0x50,
        0x50,
        0x8,
    )

    struct.pack_into(ELF64_DYNAMIC_ENTRY_FORMAT, image, 0x120, DT_STRTAB, 0x180)
    struct.pack_into(ELF64_DYNAMIC_ENTRY_FORMAT, image, 0x130, DT_STRSZ, 0x20)
    struct.pack_into(ELF64_DYNAMIC_ENTRY_FORMAT, image, 0x140, DT_SYMTAB, 0x1A0)
    struct.pack_into(ELF64_DYNAMIC_ENTRY_FORMAT, image, 0x150, DT_SYMENT, 0x18)
    struct.pack_into(ELF64_DYNAMIC_ENTRY_FORMAT, image, 0x160, DT_NULL, 0)

    image[0x180:0x180 + 0x20] = b"\x00puts\x00printf\x00malloc\x00free\x00"
    image[0x1A0:0x1A0 + 0x30] = bytes(0x30)
    return bytes(image)


def make_loaded_dynamic_offset_elf64_image() -> bytes:
    image = bytearray(make_dynamic_elf64_image())
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE + struct.calcsize(ELF64_PROGRAM_HEADER_FORMAT),
        PT_DYNAMIC,
        6,
        0x70000120,
        0x120,
        0x70000120,
        0x50,
        0x50,
        0x8,
    )
    return bytes(image)


def make_broken_dynamic_elf64_image() -> bytes:
    image = bytearray(make_dynamic_elf64_image())
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE + struct.calcsize(ELF64_PROGRAM_HEADER_FORMAT),
        PT_DYNAMIC,
        6,
        0x300,
        0x300,
        0x300,
        0x50,
        0x50,
        0x8,
    )
    image[0x300:0x350] = b"\x00" * 0x50
    return bytes(image)


def make_gapped_load_elf64_image() -> bytes:
    image = bytearray(0x400)
    e_ident = b"\x7fELF" + bytes([2, 1, 1, 0]) + bytes(8)
    ph_size = struct.calcsize(ELF64_PROGRAM_HEADER_FORMAT)
    struct.pack_into(
        ELF64_HEADER_FORMAT,
        image,
        0,
        e_ident,
        3,
        0xB7,
        1,
        0,
        ELF64_HEADER_SIZE,
        0,
        0,
        ELF64_HEADER_SIZE,
        ph_size,
        3,
        0,
        0,
        0,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE,
        PT_LOAD,
        5,
        0x40,
        0x0,
        0x0,
        0x180,
        0x180,
        0x1000,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE + ph_size,
        PT_LOAD,
        6,
        0x220,
        0x220,
        0x220,
        0x80,
        0x80,
        0x1000,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE + ph_size * 2,
        0x6474E552,
        4,
        0x180,
        0x1A0,
        0x180,
        0x20,
        0x20,
        0x8,
    )
    return bytes(image)


def make_segmented_dynamic_elf64_image() -> bytes:
    image = bytearray(0x800)
    e_ident = b"\x7fELF" + bytes([2, 1, 1, 0]) + bytes(8)
    ph_size = struct.calcsize(ELF64_PROGRAM_HEADER_FORMAT)
    struct.pack_into(
        ELF64_HEADER_FORMAT,
        image,
        0,
        e_ident,
        3,
        0xB7,
        1,
        0,
        ELF64_HEADER_SIZE,
        0,
        0,
        ELF64_HEADER_SIZE,
        ph_size,
        3,
        0,
        0,
        0,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE,
        PT_LOAD,
        5,
        0x40,
        0x0,
        0x0,
        0x180,
        0x180,
        0x1000,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE + ph_size,
        PT_LOAD,
        6,
        0x200,
        0x200,
        0x200,
        0x200,
        0x200,
        0x1000,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE + ph_size * 2,
        PT_DYNAMIC,
        6,
        0x220,
        0x220,
        0x220,
        0x50,
        0x50,
        0x8,
    )
    struct.pack_into(ELF64_DYNAMIC_ENTRY_FORMAT, image, 0x220, DT_STRTAB, 0x280)
    struct.pack_into(ELF64_DYNAMIC_ENTRY_FORMAT, image, 0x230, DT_STRSZ, 0x20)
    struct.pack_into(ELF64_DYNAMIC_ENTRY_FORMAT, image, 0x240, DT_SYMTAB, 0x2A0)
    struct.pack_into(ELF64_DYNAMIC_ENTRY_FORMAT, image, 0x250, DT_SYMENT, 0x18)
    struct.pack_into(ELF64_DYNAMIC_ENTRY_FORMAT, image, 0x260, DT_NULL, 0)
    image[0x280:0x280 + 0x20] = b"\x00foo\x00bar\x00baz\x00qux\x00memcpy\x00"
    image[0x2A0:0x2A0 + 0x30] = bytes(0x30)
    return bytes(image)


def make_segmented_plt_elf64_image() -> bytes:
    image = bytearray(0x1000)
    e_ident = b"\x7fELF" + bytes([2, 1, 1, 0]) + bytes(8)
    ph_size = struct.calcsize(ELF64_PROGRAM_HEADER_FORMAT)
    struct.pack_into(
        ELF64_HEADER_FORMAT,
        image,
        0,
        e_ident,
        3,
        0xB7,
        1,
        0,
        ELF64_HEADER_SIZE,
        0,
        0,
        ELF64_HEADER_SIZE,
        ph_size,
        4,
        0,
        0,
        0,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE,
        PT_LOAD,
        5,
        0x0,
        0x0,
        0x0,
        0x500,
        0x500,
        0x1000,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE + ph_size,
        PT_LOAD,
        6,
        0x500,
        0x500,
        0x500,
        0x200,
        0x200,
        0x1000,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE + ph_size * 2,
        PT_DYNAMIC,
        6,
        0x520,
        0x520,
        0x520,
        0x50,
        0x50,
        0x8,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE + ph_size * 3,
        PT_LOAD,
        5,
        0x900,
        0x900,
        0x900,
        0x300,
        0x300,
        0x1000,
    )
    struct.pack_into(ELF64_DYNAMIC_ENTRY_FORMAT, image, 0x520, DT_STRTAB, 0x580)
    struct.pack_into(ELF64_DYNAMIC_ENTRY_FORMAT, image, 0x530, DT_STRSZ, 0x20)
    struct.pack_into(ELF64_DYNAMIC_ENTRY_FORMAT, image, 0x540, DT_SYMTAB, 0x5A0)
    struct.pack_into(ELF64_DYNAMIC_ENTRY_FORMAT, image, 0x550, DT_SYMENT, 0x18)
    struct.pack_into(ELF64_DYNAMIC_ENTRY_FORMAT, image, 0x560, DT_NULL, 0)
    image[0x580:0x580 + 0x20] = b"\x00alpha\x00beta\x00gamma\x00plt\x00"
    image[0x5A0:0x5A0 + 0x30] = bytes(0x30)
    return bytes(image)


def make_sofix_style_elf64_image() -> bytes:
    image = bytearray(0x1400)
    e_ident = b"\x7fELF" + bytes([2, 1, 1, 0]) + bytes(8)
    ph_size = struct.calcsize(ELF64_PROGRAM_HEADER_FORMAT)
    struct.pack_into(
        ELF64_HEADER_FORMAT,
        image,
        0,
        e_ident,
        3,
        0xB7,
        1,
        0,
        ELF64_HEADER_SIZE,
        0,
        0,
        ELF64_HEADER_SIZE,
        ph_size,
        3,
        0,
        0,
        0,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE,
        PT_LOAD,
        5,
        0x0,
        0x0,
        0x0,
        0x700,
        0x700,
        0x1000,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE + ph_size,
        PT_LOAD,
        6,
        0x700,
        0x700,
        0x700,
        0x500,
        0x500,
        0x1000,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE + ph_size * 2,
        PT_DYNAMIC,
        6,
        0x900,
        0x900,
        0x900,
        0x100,
        0x100,
        0x8,
    )
    entries = [
        (DT_HASH, 0x100),
        (DT_SYMTAB, 0x200),
        (DT_STRTAB, 0x300),
        (DT_STRSZ, 0x40),
        (DT_SYMENT, 0x18),
        (DT_RELA, 0x400),
        (DT_RELASZ, 0x60),
        (DT_JMPREL, 0x500),
        (DT_PLTRELSZ, 0x30),
        (DT_PLTREL, DT_RELA),
        (DT_INIT_ARRAY, 0x980),
        (DT_INIT_ARRAYSZ, 0x10),
        (DT_FINI_ARRAY, 0x990),
        (DT_FINI_ARRAYSZ, 0x8),
        (DT_NULL, 0),
    ]
    offset = 0x900
    for tag, value in entries:
        image[offset:offset + 0x10] = struct.pack("<QQ", tag, value)
        offset += 0x10
    image[0x300:0x340] = b"\x00foo\x00bar\x00baz\x00qux\x00rela\x00plt\x00dyn\x00".ljust(0x40, b"\x00")
    image[0x200:0x260] = bytes(0x60)
    image[0x400:0x460] = bytes(0x60)
    image[0x500:0x530] = bytes(0x30)
    return bytes(image)


def make_sofix_style_tail_exec_elf64_image() -> bytes:
    image = bytearray(make_sofix_style_elf64_image())
    ph_size = struct.calcsize(ELF64_PROGRAM_HEADER_FORMAT)
    struct.pack_into(
        ELF64_HEADER_FORMAT,
        image,
        0,
        b"\x7fELF" + bytes([2, 1, 1, 0]) + bytes(8),
        3,
        0xB7,
        1,
        0,
        ELF64_HEADER_SIZE,
        0,
        0,
        ELF64_HEADER_SIZE,
        ph_size,
        4,
        0,
        0,
        0,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE + ph_size * 3,
        PT_LOAD,
        5,
        0xC80,
        0xC80,
        0xC80,
        0x180,
        0x180,
        0x1000,
    )
    return bytes(image)


def make_sysv_hash_elf64_image() -> bytes:
    image = bytearray(0x500)
    e_ident = b"\x7fELF" + bytes([2, 1, 1, 0]) + bytes(8)
    ph_size = struct.calcsize(ELF64_PROGRAM_HEADER_FORMAT)
    struct.pack_into(
        ELF64_HEADER_FORMAT,
        image,
        0,
        e_ident,
        3,
        0xB7,
        1,
        0,
        ELF64_HEADER_SIZE,
        0,
        0,
        ELF64_HEADER_SIZE,
        ph_size,
        2,
        0,
        0,
        0,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE,
        PT_LOAD,
        5,
        0x0,
        0x0,
        0x0,
        0x400,
        0x400,
        0x1000,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE + ph_size,
        PT_DYNAMIC,
        6,
        0x300,
        0x300,
        0x300,
        0x60,
        0x60,
        0x8,
    )
    entries = [
        (DT_HASH, 0x100),
        (DT_SYMTAB, 0x200),
        (DT_STRTAB, 0x280),
        (DT_STRSZ, 0x20),
        (DT_SYMENT, 0x18),
        (DT_NULL, 0),
    ]
    offset = 0x300
    for tag, value in entries:
        struct.pack_into(ELF64_DYNAMIC_ENTRY_FORMAT, image, offset, tag, value)
        offset += 0x10
    # nbucket=1, nchain=3, bucket[0]=1, chain=[0, 2, 0]
    image[0x100:0x118] = struct.pack("<LLLLLL", 1, 3, 1, 0, 2, 0)
    image[0x200:0x248] = bytes(0x48)
    image[0x280:0x2A0] = b"\x00foo\x00bar\x00baz\x00".ljust(0x20, b"\x00")
    return bytes(image)


def make_gnu_hash_elf64_image() -> bytes:
    image = bytearray(0x500)
    e_ident = b"\x7fELF" + bytes([2, 1, 1, 0]) + bytes(8)
    ph_size = struct.calcsize(ELF64_PROGRAM_HEADER_FORMAT)
    struct.pack_into(
        ELF64_HEADER_FORMAT,
        image,
        0,
        e_ident,
        3,
        0xB7,
        1,
        0,
        ELF64_HEADER_SIZE,
        0,
        0,
        ELF64_HEADER_SIZE,
        ph_size,
        2,
        0,
        0,
        0,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE,
        PT_LOAD,
        5,
        0x0,
        0x0,
        0x0,
        0x400,
        0x400,
        0x1000,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE + ph_size,
        PT_DYNAMIC,
        6,
        0x300,
        0x300,
        0x300,
        0x60,
        0x60,
        0x8,
    )
    entries = [
        (DT_GNU_HASH, 0x100),
        (DT_SYMTAB, 0x180),
        (DT_STRTAB, 0x200),
        (DT_STRSZ, 0x20),
        (DT_SYMENT, 0x18),
        (DT_NULL, 0),
    ]
    offset = 0x300
    for tag, value in entries:
        struct.pack_into(ELF64_DYNAMIC_ENTRY_FORMAT, image, offset, tag, value)
        offset += 0x10
    # nbuckets=1, symoffset=0, bloom_size=1, bloom_shift=0, bloom[0], bucket[0]=0, chains[0..2]
    image[0x100:0x128] = (
        struct.pack("<LLLL", 1, 0, 1, 0)
        + struct.pack("<Q", 1)
        + struct.pack("<L", 0)
        + struct.pack("<LLL", 0x10, 0x20, 0x31)
    )
    image[0x180:0x1C8] = bytes(0x48)
    image[0x200:0x220] = b"\x00gnu0\x00gnu1\x00gnu2\x00".ljust(0x20, b"\x00")
    return bytes(image)


def make_malformed_sysv_hash_elf64_image() -> bytes:
    image = bytearray(make_sysv_hash_elf64_image())
    image[0x100:0x108] = struct.pack("<LL", 1, 0x100)
    return bytes(image)


def make_versym_hash_elf64_image() -> bytes:
    image = bytearray(0x600)
    e_ident = b"\x7fELF" + bytes([2, 1, 1, 0]) + bytes(8)
    ph_size = struct.calcsize(ELF64_PROGRAM_HEADER_FORMAT)
    struct.pack_into(
        ELF64_HEADER_FORMAT,
        image,
        0,
        e_ident,
        3,
        0xB7,
        1,
        0,
        ELF64_HEADER_SIZE,
        0,
        0,
        ELF64_HEADER_SIZE,
        ph_size,
        2,
        0,
        0,
        0,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE,
        PT_LOAD,
        5,
        0x0,
        0x0,
        0x0,
        0x500,
        0x500,
        0x1000,
    )
    struct.pack_into(
        ELF64_PROGRAM_HEADER_FORMAT,
        image,
        ELF64_HEADER_SIZE + ph_size,
        PT_DYNAMIC,
        6,
        0x380,
        0x380,
        0x380,
        0x80,
        0x80,
        0x8,
    )
    entries = [
        (DT_HASH, 0x100),
        (DT_SYMTAB, 0x200),
        (DT_STRTAB, 0x280),
        (DT_STRSZ, 0x20),
        (DT_SYMENT, 0x18),
        (DT_VERSYM, 0x300),
        (DT_VERNEED, 0x360),
        (DT_VERNEEDNUM, 1),
        (DT_NULL, 0),
    ]
    offset = 0x380
    for tag, value in entries:
        struct.pack_into(ELF64_DYNAMIC_ENTRY_FORMAT, image, offset, tag, value)
        offset += 0x10
    image[0x100:0x118] = struct.pack("<LLLLLL", 1, 3, 1, 0, 2, 0)
    image[0x200:0x248] = bytes(0x48)
    image[0x280:0x2A0] = b"\x00v0\x00v1\x00v2\x00".ljust(0x20, b"\x00")
    image[0x300:0x306] = b"\x01\x00\x02\x00\x03\x00"
    image[0x360:0x370] = struct.pack("<HHLLL", 1, 1, 1, 0x10, 0)
    image[0x370:0x380] = struct.pack("<LHHLL", 0x1234, 0, 1, 2, 0)
    return bytes(image)


def extract_section_names(image: bytes):
    header = parse_elf64_header(image)
    section_headers = parse_elf64_section_headers(image, header)
    shstrtab = section_headers[header.e_shstrndx]
    blob = image[shstrtab.sh_offset:shstrtab.sh_offset + shstrtab.sh_size]
    names = []
    for entry in section_headers:
        start = entry.sh_name
        end = blob.find(b"\x00", start)
        if 0 <= start < len(blob) and end != -1:
            names.append(blob[start:end].decode("ascii", "ignore"))
        else:
            names.append("")
    return names


def extract_named_sections(image: bytes):
    header = parse_elf64_header(image)
    section_headers = parse_elf64_section_headers(image, header)
    shstrtab = section_headers[header.e_shstrndx]
    blob = image[shstrtab.sh_offset:shstrtab.sh_offset + shstrtab.sh_size]
    result = {}
    for entry in section_headers:
        start = entry.sh_name
        end = blob.find(b"\x00", start)
        if 0 <= start < len(blob) and end != -1:
            name = blob[start:end].decode("ascii", "ignore")
        else:
            name = ""
        result[name] = entry
    return result


class SoFixTests(unittest.TestCase):
    def test_parse_elf64_header(self) -> None:
        image = make_minimal_elf64_image()

        header = parse_elf64_header(image)

        self.assertEqual(header.e_phnum, 1)
        self.assertEqual(header.e_phoff, ELF64_HEADER_SIZE)

    def test_rebuild_loaded_elf_image_rewrites_pt_load(self) -> None:
        image = make_minimal_elf64_image()

        repaired = rebuild_loaded_elf_image(image)
        header = parse_elf64_header(repaired.data)
        program_headers = parse_elf64_program_headers(repaired.data, header)

        self.assertEqual(repaired.modified_program_headers, 1)
        self.assertEqual(program_headers[0].p_offset, 0)
        self.assertEqual(program_headers[0].p_filesz, 0x40)

    def test_rebuild_loaded_elf_image_rebuilds_dynamic_sections(self) -> None:
        image = make_dynamic_elf64_image()

        repaired = rebuild_loaded_elf_image(image)
        header = parse_elf64_header(repaired.data)
        section_headers = parse_elf64_section_headers(repaired.data, header)

        self.assertGreater(header.e_shoff, len(image))
        self.assertGreaterEqual(header.e_shnum, 4)
        self.assertEqual(header.e_shentsize, ELF64_SECTION_HEADER_SIZE)

        section_types = [entry.sh_type for entry in section_headers]
        self.assertIn(SHT_DYNAMIC, section_types)
        self.assertIn(SHT_DYNSYM, section_types)
        self.assertIn(SHT_STRTAB, section_types)

    def test_rebuild_loaded_elf_image_normalizes_dynamic_offsets_before_rebuild(self) -> None:
        image = make_loaded_dynamic_offset_elf64_image()

        repaired = rebuild_loaded_elf_image(image)
        header = parse_elf64_header(repaired.data)
        program_headers = parse_elf64_program_headers(repaired.data, header)
        section_headers = parse_elf64_section_headers(repaired.data, header)

        dynamic_header = program_headers[1]
        self.assertEqual(dynamic_header.p_offset, 0x120)
        section_types = [entry.sh_type for entry in section_headers]
        self.assertIn(SHT_DYNAMIC, section_types)
        self.assertIn(SHT_DYNSYM, section_types)
        self.assertIn(SHT_STRTAB, section_types)

    def test_rebuild_loaded_elf_image_recovers_dynamic_data_from_base_so(self) -> None:
        image = make_broken_dynamic_elf64_image()
        base_image = make_dynamic_elf64_image()

        repaired = rebuild_loaded_elf_image(image, base_so_data=base_image)
        header = parse_elf64_header(repaired.data)
        program_headers = parse_elf64_program_headers(repaired.data, header)
        section_types = [entry.sh_type for entry in parse_elf64_section_headers(repaired.data, header)]

        dynamic_header = program_headers[1]
        self.assertGreaterEqual(dynamic_header.p_offset, len(image))
        self.assertEqual(dynamic_header.p_filesz, 0x50)
        self.assertIn(SHT_DYNAMIC, section_types)
        self.assertIn(SHT_DYNSYM, section_types)
        self.assertIn(SHT_STRTAB, section_types)

    def test_rebuild_loaded_elf_image_expands_load_ranges_for_real_dump_layout(self) -> None:
        image = make_gapped_load_elf64_image()

        repaired = rebuild_loaded_elf_image(image, base_address=0x70000000)
        header = parse_elf64_header(repaired.data)
        program_headers = parse_elf64_program_headers(repaired.data, header)

        self.assertEqual(program_headers[0].p_offset, 0x0)
        self.assertEqual(program_headers[0].p_filesz, 0x220)
        self.assertEqual(program_headers[0].p_memsz, 0x220)
        self.assertEqual(program_headers[1].p_offset, 0x220)
        self.assertEqual(program_headers[1].p_filesz, len(image) - 0x220)
        self.assertEqual(program_headers[1].p_memsz, len(image) - 0x220)
        self.assertEqual(program_headers[2].p_offset, 0x1A0)
        self.assertEqual(program_headers[2].p_paddr, 0x1A0)

    def test_rebuild_loaded_elf_image_synthesizes_segment_sections(self) -> None:
        image = make_segmented_dynamic_elf64_image()

        repaired = rebuild_loaded_elf_image(image)
        names = extract_section_names(repaired.data)

        self.assertIn(".text", names)
        self.assertIn(".data", names)

    def test_rebuild_loaded_elf_image_labels_small_secondary_exec_segment_as_plt(self) -> None:
        image = make_segmented_plt_elf64_image()

        repaired = rebuild_loaded_elf_image(image)
        names = extract_section_names(repaired.data)

        self.assertIn(".text", names)
        self.assertIn(".plt", names)

    def test_rebuild_loaded_elf_image_synthesizes_sofix_style_sections(self) -> None:
        image = make_sofix_style_elf64_image()

        repaired = rebuild_loaded_elf_image(image)
        names = extract_section_names(repaired.data)

        self.assertIn(".rela.plt", names)
        self.assertIn(".plt", names)
        self.assertIn(".text&ARM.extab", names)
        self.assertIn(".data", names)
        self.assertNotIn(".data.1", names)

    def test_rebuild_loaded_elf_image_uses_sofix_style_plt_size_heuristic(self) -> None:
        image = make_sofix_style_elf64_image()

        repaired = rebuild_loaded_elf_image(image)
        sections = extract_named_sections(repaired.data)

        self.assertEqual(sections[".rela.plt"].sh_size, 0x48)
        self.assertEqual(sections[".plt"].sh_size, 0x38)
        self.assertEqual(sections[".text&ARM.extab"].sh_offset, 0x580)

    def test_rebuild_loaded_elf_image_extends_data_to_max_load_end(self) -> None:
        image = make_sofix_style_tail_exec_elf64_image()

        repaired = rebuild_loaded_elf_image(image)
        sections = extract_named_sections(repaired.data)

        self.assertEqual(sections[".data"].sh_size, 0x600)

    def test_rebuild_loaded_elf_image_rejects_non_elf(self) -> None:
        with self.assertRaises(ValueError):
            rebuild_loaded_elf_image(b"not-an-elf")

    def test_rebuild_loaded_elf_image_uses_sysv_hash_to_size_hash_and_dynsym(self) -> None:
        image = make_sysv_hash_elf64_image()

        repaired = rebuild_loaded_elf_image(image)
        sections = extract_named_sections(repaired.data)

        self.assertEqual(sections[".hash"].sh_size, 0x18)
        self.assertEqual(sections[".dynsym"].sh_size, 0x48)

    def test_rebuild_loaded_elf_image_uses_gnu_hash_to_size_hash_and_dynsym(self) -> None:
        image = make_gnu_hash_elf64_image()

        repaired = rebuild_loaded_elf_image(image)
        sections = extract_named_sections(repaired.data)

        self.assertIn(".gnu.hash", sections)
        self.assertEqual(sections[".gnu.hash"].sh_size, 0x28)
        self.assertEqual(sections[".dynsym"].sh_size, 0x48)

    def test_rebuild_loaded_elf_image_falls_back_when_sysv_hash_is_malformed(self) -> None:
        image = make_malformed_sysv_hash_elf64_image()

        repaired = rebuild_loaded_elf_image(image)
        sections = extract_named_sections(repaired.data)

        self.assertEqual(sections[".hash"].sh_size, 0x100)
        self.assertTrue(any("sysv hash parse failed" in warning for warning in repaired.warnings))

    def test_rebuild_loaded_elf_image_sizes_gnu_version_from_dynsym_count(self) -> None:
        image = make_versym_hash_elf64_image()

        repaired = rebuild_loaded_elf_image(image)
        sections = extract_named_sections(repaired.data)

        self.assertEqual(sections[".dynsym"].sh_size, 0x48)
        self.assertEqual(sections[".gnu.version"].sh_size, 0x6)

    def test_rebuild_loaded_elf_image_sizes_gnu_version_r_from_verneed_chain(self) -> None:
        image = make_versym_hash_elf64_image()

        repaired = rebuild_loaded_elf_image(image)
        sections = extract_named_sections(repaired.data)

        self.assertEqual(sections[".gnu.version_r"].sh_size, 0x20)


if __name__ == "__main__":
    unittest.main()
