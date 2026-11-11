import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from check_candidate_elf import (
    Section,LoadSegment,verify,verify_flash_loads,read_load_segments,
    SIGNATURE,SIGNATURE_ADDR)
import struct

def s(name,addr,size,flags=2,kind=1,offset=0):
    return Section(name,addr,size,flags,kind,offset)

raw=b"_"*24+SIGNATURE+b"!"*16
good=[s(".text",0x08006000,16),s(".mySegment",SIGNATURE_ADDR,16,offset=24),
      s(".data",0x20000000,8),s(".bss",0x20000008,32,kind=8)]
assert verify(good,raw)==(SIGNATURE_ADDR+16,0x20000028)
bad=[
    [s(".text",0x08005fff,17),*good[1:]],
    [s(".text",0x08033ff0,32),*good[1:]],
    [*good[:2],s(".data",0x20000ff0,48),good[3]],
    [good[0],s(".mySegment",SIGNATURE_ADDR+4,16,offset=24),*good[2:]],
    [s(".text",0x08006000,32),s(".extra",0x08006010,8),*good[1:]],
]
for sections in bad:
    try: verify(sections,raw)
    except ValueError: continue
    raise AssertionError("accepted invalid flash/RAM layout")
try: verify(good,raw[:20])
except ValueError: pass
else: raise AssertionError("accepted missing signature")

# Initialized RAM has a flash *load* address that section VMA alone misses.
segments=[
    LoadSegment(0x08006000,0x08006000,16,16,0),
    LoadSegment(SIGNATURE_ADDR,SIGNATURE_ADDR,16,16,24),
    LoadSegment(0x20000000,0x08009000,8,8,0),
]
assert verify_flash_loads(good,segments)==SIGNATURE_ADDR+16
unsafe=[
    [*segments[:2],LoadSegment(0x20000000,0x08033FFC,8,8,0)],
    [*segments[:2],LoadSegment(0x20000000,SIGNATURE_ADDR+8,8,8,0)],
    [*segments[:2],LoadSegment(0x20000000,0x08005000,8,8,0)],
    segments[:2],  # missing .data PT_LOAD
]
for item in unsafe:
    try: verify_flash_loads(good,item)
    except ValueError: continue
    raise AssertionError("unsafe initialized-RAM flash load accepted")

fake=bytearray(128)
fake[:6]=b"\x7fELF\x01\x01"
struct.pack_into("<I",fake,28,52)  # ELF program-header offset
struct.pack_into("<HH",fake,42,32,1)  # 1 ELF32 program header
struct.pack_into("<IIIIIIII",fake,52,1,96,0x20000000,
                 0x08009000,8,8,6,4)
parsed=read_load_segments(bytes(fake))
assert len(parsed)==1 and parsed[0].paddr==0x08009000
try: read_load_segments(bytes(fake[:80]))
except ValueError: pass
else: raise AssertionError("truncated initialized-data segment was accepted")
print("PASS: ELF section/VMA, program-header LMA, fixed RAM, load collisions, signature rejection")
