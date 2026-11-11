import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from check_candidate_elf import Section,verify,SIGNATURE,SIGNATURE_ADDR

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
print("PASS: ELF region, fixed RAM, collision and signature rejection")
