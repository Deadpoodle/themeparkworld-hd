"""PE analysis script for Theme Park World TP.exe - Tier 0 reconnaissance."""
import pefile
import sys
import os

exe_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "testrun", "TP.exe")
pe = pefile.PE(exe_path)

print("=" * 70)
print("PE ANALYSIS: " + os.path.basename(exe_path))
print("=" * 70)

# Basic info
print(f"\nMachine: {'x86 (32-bit)' if pe.FILE_HEADER.Machine == 0x14c else 'x64 (64-bit)' if pe.FILE_HEADER.Machine == 0x8664 else hex(pe.FILE_HEADER.Machine)}")
print(f"Number of sections: {pe.FILE_HEADER.NumberOfSections}")
print(f"Timestamp: {pe.FILE_HEADER.TimeDateStamp} (0x{pe.FILE_HEADER.TimeDateStamp:08X})")
import datetime
try:
    ts = datetime.datetime.utcfromtimestamp(pe.FILE_HEADER.TimeDateStamp)
    print(f"Timestamp date: {ts.strftime('%Y-%m-%d %H:%M:%S')} UTC")
except: pass
print(f"Characteristics: 0x{pe.FILE_HEADER.Characteristics:04X}")

# Optional header
oh = pe.OPTIONAL_HEADER
print(f"\nLinker version: {oh.MajorLinkerVersion}.{oh.MinorLinkerVersion}")
print(f"Image base: 0x{oh.ImageBase:08X}")
print(f"Entry point: 0x{oh.AddressOfEntryPoint:08X} (RVA)")
print(f"Section alignment: 0x{oh.SectionAlignment:X}")
print(f"File alignment: 0x{oh.FileAlignment:X}")
print(f"OS version: {oh.MajorOperatingSystemVersion}.{oh.MinorOperatingSystemVersion}")
print(f"Subsystem version: {oh.MajorSubsystemVersion}.{oh.MinorSubsystemVersion}")
print(f"Subsystem: {oh.Subsystem} ({'GUI' if oh.Subsystem == 2 else 'Console' if oh.Subsystem == 3 else '?'})")
print(f"DLL Characteristics: 0x{oh.DllCharacteristics:04X}")
large_aware = bool(pe.FILE_HEADER.Characteristics & 0x20)
print(f"Large Address Aware: {large_aware}")

# Sections
print("\n--- SECTIONS ---")
for s in pe.sections:
    name = s.Name.decode('ascii', errors='replace').rstrip('\x00')
    print(f"  {name:8s}  VA=0x{s.VirtualAddress:08X}  VSize=0x{s.Misc_VirtualSize:08X}  RawSize=0x{s.SizeOfRawData:08X}  Chars=0x{s.Characteristics:08X}")

# Imports
print("\n--- IMPORTS ---")
if hasattr(pe, 'DIRECTORY_ENTRY_IMPORT'):
    for entry in pe.DIRECTORY_ENTRY_IMPORT:
        dll_name = entry.dll.decode('ascii', errors='replace')
        imports = []
        for imp in entry.imports:
            if imp.name:
                imports.append(imp.name.decode('ascii', errors='replace'))
            else:
                imports.append(f"ord#{imp.ordinal}")
        print(f"\n  {dll_name} ({len(imports)} imports):")
        for name in sorted(imports):
            print(f"    {name}")

# Exports
print("\n--- EXPORTS ---")
if hasattr(pe, 'DIRECTORY_ENTRY_EXPORT'):
    for exp in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        name = exp.name.decode('ascii', errors='replace') if exp.name else f"ord#{exp.ordinal}"
        print(f"  {name} @ 0x{exp.address:08X}")
else:
    print("  (none)")

# Check for Rich header (identifies MSVC version)
print("\n--- RICH HEADER ---")
if hasattr(pe, 'RICH_HEADER') and pe.RICH_HEADER:
    print("  Present")
    # Try to decode entries
    try:
        entries = pe.RICH_HEADER.values
        for i in range(0, len(entries), 2):
            comp_id = entries[i]
            count = entries[i+1]
            print(f"  CompID: 0x{comp_id:08X}  Count: {count}")
    except: pass
else:
    print("  Not present (likely old MSVC 4.x/5.x)")

pe.close()
