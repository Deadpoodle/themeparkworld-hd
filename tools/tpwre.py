"""Tier 0 RE helper for TP.exe: a linear-sweep disassembly cache plus lookups.

  python -P tools/tpwre.py dump [exe]         write re/text.asm (all code sections)
  python -P tools/tpwre.py dis <addr> [n]     disassemble n instructions at addr
  python -P tools/tpwre.py bytes <addr> [n]   hex dump n bytes at addr

The exe defaults to testrun/TP.exe. A linear sweep falls out of step after jump
tables and data in code; use the Ghidra export for xrefs, this for the bytes.
"""
import os
import sys

import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
CODE_SECTIONS = (".text", "cseg")


def load(exe=None):
    return pefile.PE(exe or os.path.join(ROOT, "testrun", "TP.exe"), fast_load=True)


def read(pe, va, n):
    return pe.get_data(va - pe.OPTIONAL_HEADER.ImageBase, n)


def dis(pe, va, count):
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    for i, ins in enumerate(md.disasm(read(pe, va, count * 15), va)):
        if i >= count:
            break
        print(f"{ins.address:08x}  {ins.bytes.hex():<20} {ins.mnemonic} {ins.op_str}")


def dump(pe):
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    base = pe.OPTIONAL_HEADER.ImageBase
    out = os.path.join(ROOT, "re", "text.asm")
    with open(out, "w", encoding="ascii") as f:
        for s in pe.sections:
            name = s.Name.rstrip(b"\0").decode()
            if name not in CODE_SECTIONS:
                continue
            data = s.get_data()
            va = base + s.VirtualAddress
            f.write(f"; section {name} {va:08x}\n")
            off = 0
            while off < len(data):
                ok = False
                for ins in md.disasm(data[off:], va + off):
                    f.write(f"{ins.address:08x}  {ins.bytes.hex():<20} {ins.mnemonic} {ins.op_str}\n")
                    off += ins.size
                    ok = True
                if off < len(data):  # undecodable byte: emit it and resync
                    f.write(f"{va + off:08x}  {data[off]:02x}                   db\n")
                    off += 1
                elif not ok:
                    break
    print(out)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    cmd = argv[1]
    if cmd == "dump":
        dump(load(argv[2] if len(argv) > 2 else None))
    elif cmd == "dis":
        dis(load(), int(argv[2], 16), int(argv[3]) if len(argv) > 3 else 20)
    elif cmd == "bytes":
        va, n = int(argv[2], 16), int(argv[3]) if len(argv) > 3 else 64
        print(read(load(), va, n).hex(" "))
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
