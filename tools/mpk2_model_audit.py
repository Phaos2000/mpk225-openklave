#!/usr/bin/env python3
"""Read-only model evidence from the official MPK249/261 v1.02 ZIP.

No MIDI ports, updater execution, USB access, flashing, or firmware output.
Supports the SHA-256-pinned 2023 v1.02 .syx payload only.
"""
import hashlib
import json
import pathlib
import struct
import sys
import zipfile

KNOWN_SHA256 = '1b3af40f844e040854375f28df0ff587a80ebcbff47714f0eba548d1abe6510b'
BASE = 0x08000000


def parse_syx(syx):
    if hashlib.sha256(syx).hexdigest() != KNOWN_SHA256:
        raise ValueError('Firmware checksum differs from audited v1.02; cannot interpret model offsets')
    if not (syx.startswith(bytes.fromhex('f0 47 00 24 70')) and syx.endswith(b'\xf7')):
        raise ValueError('Invalid Akai SysEx image framing')
    flash = {}
    upper = 0
    record_count = 0
    for line in syx[5:-1].decode('ascii').splitlines():
        if not line.startswith(':'):
            raise ValueError('Non-Intel-HEX line')
        rec = bytes.fromhex(line[1:])
        if len(rec) != rec[0] + 5 or sum(rec) % 256:
            raise ValueError('Bad Intel HEX record/checksum')
        n, offset, typ = rec[0], int.from_bytes(rec[1:3], 'big'), rec[3]
        data = rec[4:4+n]
        record_count += 1
        if typ == 4:
            if n != 2: raise ValueError('Invalid address-extension length')
            upper = int.from_bytes(data, 'big') << 16
        elif typ == 0:
            addr = upper + offset
            if addr < BASE or addr + n > 0x08040000: raise ValueError('Data outside MCU flash')
            for i, b in enumerate(data):
                if addr+i in flash and flash[addr+i] != b: raise ValueError('Overlapping conflicting data')
                flash[addr+i] = b
        elif typ not in (1, 5):
            raise ValueError('Unexpected Intel HEX record type')
    return flash, record_count


def get(flash, addr, length):
    try:
        return bytes(flash[a] for a in range(addr, addr + length))
    except KeyError as e:
        raise ValueError(f'Missing firmware byte at {e.args[0]:#x}') from e


def u32(flash, addr):
    return struct.unpack('<I', get(flash, addr, 4))[0]


def audit(path):
    with zipfile.ZipFile(path) as z:
        names = [n for n in z.namelist() if not n.startswith('__MACOSX/')]
        matches = [n for n in names if n.endswith('/MPK249_MPK261_FW_v1_02.syx')]
        if len(matches) != 1: raise ValueError('Expected one official firmware .syx')
        syx = z.read(matches[0])
    flash, record_count = parse_syx(syx)
    # Unique opcode spans interpreted by offline Thumb-2 disassembly.
    # The SHA-256 is pinned; never infer this mapping for another image.
    for address, sig in {
        0x0800D3A2: '10 b5 00 24 00 f0 53 fa',
        0x0800D4E2: '10 b5 19 24 00 f0 b3 f9',
        0x0800DC3C: '10 b5 00 24 ff f7 06 fe',
        0x08018AC8: '10 b5 f5 f7 a3 f8',
    }.items():
        if get(flash, address, len(bytes.fromhex(sig))) != bytes.fromhex(sig):
            raise ValueError(f'Unexpected opcode at {address:#x}')
    table = 0x08028A08
    bank_a = [u32(flash, table+i*4) for i in range(25)]
    bank_b = [u32(flash, table+100+i*4) for i in range(25)]
    for actual, first in [(bank_a, 0x0801F30E), (bank_b, 0x08028AD0)]:
        if actual != [first+i*1546 for i in range(25)]:
            raise ValueError('Unexpected factory-preset pointer table')
    product_names = {}
    for label, address in [('MPK225', 0x0801E4DE), ('MPK249', 0x0801E4EC), ('MPK261', 0x0801E4FA)]:
        b = get(flash, address, 14)
        if b[:2] != bytes([14, 3]) or b[2:].decode('utf-16le') != label:
            raise ValueError('Unexpected USB product string descriptor')
        product_names[label] = f'0x{address:08X}'
    return {
        'scope': 'MPK249/261 v1.02 shared application; not a physical MPK225 memory dump',
        'syx_sha256': KNOWN_SHA256,
        'intel_hex_records_validated': record_count,
        'model_code_function': '0x0800D850',
        'model_id_function': '0x0800D3A2',
        'key_count_function': '0x0800D4E2',
        'usb_product_string_function': '0x0800DC3C',
        'usb_device_pid_write_function': '0x08018AC8',
        'variants': [{'code':1,'name':'MPK225','id':'0x23','keys':25,'factory_bank':'B'},
                     {'code':2,'name':'MPK249','id':'0x24','keys':49,'factory_bank':'A'},
                     {'code':3,'name':'MPK261','id':'0x25','keys':61,'factory_bank':'A'},
                     {'code':4,'name':'MPK249 variant','id':'0x24','keys':49,'factory_bank':'not initialized by 0x08008A2C'}],
        'usb_product_strings_at': product_names,
        'factory_record_bytes': 1546,
        'factory_bank_records': 25,
        'factory_bank_a_start': f'0x{bank_a[0]:08X}',
        'factory_bank_b_start': f'0x{bank_b[0]:08X}',
    }


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit('Usage: python mpk2_model_audit.py MPK249_&_MPK261_PC_1.02_Update.zip')
    try:
        print(json.dumps(audit(pathlib.Path(sys.argv[1])), indent=2))
    except (OSError, ValueError, zipfile.BadZipFile) as exc:
        sys.exit(f'Audit failed: {exc}')
