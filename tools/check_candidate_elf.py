#!/usr/bin/env python3
"""Offline ELF memory-region guard. This is NOT a boot/recovery proof."""
from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path
import struct
import sys

FLASH_START=0x08006000
FLASH_END=0x08034000
RAM_START=0x20000000
STATIC_RAM_END=0x20001000 # first inherited fixed RAM region
SIGNATURE_ADDR=0x08033800
SIGNATURE=b"AD4A_Richard"+bytes([0,1,0,2])

@dataclass(frozen=True)
class Section:
    name: str
    addr: int
    size: int
    flags: int
    kind: int
    offset: int=0

def read_sections(raw: bytes) -> list[Section]:
    if len(raw)<52 or raw[:6]!=b"\x7fELF\x01\x01":
        raise ValueError("expected little-endian ELF32")
    shoff=struct.unpack_from("<I",raw,32)[0]
    entsize,count,names=struct.unpack_from("<HHH",raw,46)
    if entsize<40 or not (0<count<4096) or names>=count:
        raise ValueError("unsupported section table")
    if shoff>len(raw) or count*entsize>len(raw)-shoff:
        raise ValueError("truncated section table")
    rows=[struct.unpack_from("<IIIIIIIIII",raw,shoff+i*entsize)
          for i in range(count)]
    name_row=rows[names]
    off,sz=name_row[4],name_row[5]
    if off>len(raw) or sz>len(raw)-off:
        raise ValueError("invalid name string table")
    strings=raw[off:off+sz]
    def section_name(n: int)->str:
        if n>=len(strings): raise ValueError("bad section name offset")
        end=strings.find(b"\0",n)
        if end<0: end=len(strings)
        return strings[n:end].decode("ascii")
    return [Section(section_name(h[0]),h[3],h[5],h[2],h[1],h[4])
            for h in rows]


@dataclass(frozen=True)
class LoadSegment:
    vaddr: int
    paddr: int
    filesz: int
    memsz: int
    offset: int

def read_load_segments(raw: bytes) -> list[LoadSegment]:
    """ELF32 PT_LOAD mappings. For .data, p_paddr is the flash LMA."""
    if len(raw)<52 or raw[:6]!=b"\x7fELF\x01\x01":
        raise ValueError("expected ELF32 little-endian")
    phoff=struct.unpack_from("<I",raw,28)[0]
    entsize,count=struct.unpack_from("<HH",raw,42)
    if entsize<32 or not (0<count<128):
        raise ValueError("missing or invalid ELF program headers")
    if phoff>len(raw) or count*entsize>len(raw)-phoff:
        raise ValueError("truncated ELF program headers")
    segments=[]
    for i in range(count):
        typ,off,va,pa,filesz,memsz,flags,align=struct.unpack_from(
            "<IIIIIIII",raw,phoff+i*entsize)
        if typ!=1: continue  # PT_LOAD
        if filesz>memsz or off>len(raw) or filesz>len(raw)-off:
            raise ValueError("malformed load segment")
        segments.append(LoadSegment(va,pa,filesz,memsz,off))
    if not segments:
        raise ValueError("no PT_LOAD image to validate")
    return segments

def verify_flash_loads(sections: list[Section],
                       segments: list[LoadSegment]) -> int:
    """Validate *load* addresses, not only VMA addresses.

    Initialized RAM is linked with VMA in SRAM but loaded from flash.
    Without this check an ELF could pass every prior section-VMA check
    while .data's flash bytes overwrite the boot token or preset pages.
    """
    loads=[]
    for s in sections:
        if not (s.flags&2) or not s.size or s.kind==8:
            continue
        owners=[p for p in segments
                if p.vaddr<=s.addr and s.addr+s.size<=p.vaddr+p.memsz
                and p.offset<=s.offset and s.offset+s.size<=p.offset+p.filesz]
        if len(owners)!=1:
            raise ValueError(f"{s.name}: ambiguous or missing flash load segment")
        p=owners[0]
        lma=p.paddr+s.addr-p.vaddr
        if lma<FLASH_START or lma+s.size>FLASH_END:
            raise ValueError(f"{s.name}: load address outside application flash")
        loads.append((lma,lma+s.size,s.name))
    if not loads:
        raise ValueError("no initialized flash load sections")
    loads.sort()
    for a,b in zip(loads,loads[1:]):
        if a[1]>b[0]:
            raise ValueError(f"flash load collision: {a[2]} with {b[2]}")
    return max(x[1] for x in loads)

def verify(sections: list[Section],raw: bytes)->tuple[int,int]:
    allocated=[s for s in sections if s.flags&2 and s.size]
    flash=[]
    ram=[]
    byname={s.name:s for s in sections}
    for s in allocated:
        if s.addr+s.size>0xffffffff:
            raise ValueError(f"{s.name} address overflow")
        if 0x08000000<=s.addr<0x08040000:
            if s.addr<FLASH_START or s.addr+s.size>FLASH_END:
                raise ValueError(f"{s.name} outside application flash")
            flash.append((s.addr,s.addr+s.size,s.name))
        elif RAM_START<=s.addr<0x2000c000:
            if s.addr+s.size>STATIC_RAM_END:
                raise ValueError(f"{s.name} overlaps inherited fixed RAM")
            ram.append((s.addr,s.addr+s.size,s.name))
        else:
            raise ValueError(f"{s.name} unexpectedly allocated at {s.addr:#x}")
        if s.kind!=8 and (s.offset>len(raw) or s.size>len(raw)-s.offset):
            raise ValueError(f"missing bytes for {s.name}")
    for spans in (flash,ram):
        spans.sort()
        for first,second in zip(spans,spans[1:]):
            if first[1]>second[0]:
                raise ValueError(f"sections overlap: {first[2]} / {second[2]}")
    if not any(s.name==".text" for s in allocated):
        raise ValueError("missing allocated .text")
    sig=byname.get(".mySegment")
    if (not sig or sig.addr!=SIGNATURE_ADDR or
        sig.size!=len(SIGNATURE) or sig.kind==8 or
        raw[sig.offset:sig.offset+sig.size]!=SIGNATURE):
        raise ValueError("missing official shared-image token")
    if not ram: raise ValueError("no RAM allocations")
    return max(s[1] for s in flash),max(s[1] for s in ram)

def main() -> None:
    if len(sys.argv)<2:
        raise SystemExit("usage: check_candidate_elf.py ELF [ELF ...]")
    for p in sys.argv[1:]:
        raw=Path(p).read_bytes()
        sections=read_sections(raw)
        a,b=verify(sections,raw)
        load_end=verify_flash_loads(sections,read_load_segments(raw))
        print(f"PASS NOT INSTALLABLE: {p} section flash end {a:#x},"
              f" flash load end {load_end:#x}, RAM end {b:#x}")

if __name__=="__main__":
    main()
