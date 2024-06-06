import subprocess
import os
import sys
import glob

def write_out(name: str, data: dict) -> None:
    with open(name, 'w') as file:
        for (k, v) in data.items():
            file.write(k + ":\n")
            file.write(f"  fullpath: {v['fullpath']}\n")
            file.write(f"  name: {v['name']}\n")
            file.write(f"  symbols:\n")
            for sym in v['symbols']:
                file.write(f"  - {sym}\n")

def obj() -> None:
    dirs = os.environ['LIB'].split(os.pathsep)
    sym_map = dict()
    for f in dirs:
        if not f:
            continue
        target = f + os.path.sep + "*.obj"
        libs = glob.glob(target, recursive=False)
        target = f + os.path.sep + "*.o"
        libs += glob.glob(target, recursive=False)
        exclude = {'@comp.id', '@feat.00', '@vol.md', '.pdata', '.data', '.xdata', '.chks64', '.drectve', '.bss'}
        for lib in libs:
            print(lib)
            proc = subprocess.run(['dumpbin', '/SYMBOLS', lib], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if proc.stderr:
                print("{lib}: {proc.stderr.decode()[:100]}")
            data = proc.stdout.replace(b'\n\r', b'\n').replace(b'\r', b'').decode().split('\n')
            name = os.path.split(lib)[1]
            base = name
            if base in sym_map:
                print(f"Duplicate: {lib}, {sym_map[base]['fullpath']}")
                base = lib
            if base in sym_map:
                break
            syms = []
            for (ix, line) in enumerate(data):
                line = line.strip()
                if line == "COFF SYMBOL TABLE":
                    rows = data[ix + 1:]
                    for row in rows:
                        row = row.strip()
                        if not row:
                            break
                        parts = row.split('|')
                        if len(parts) <= 1:
                            continue
                        sym = parts[1].strip().split()[0]
                        if sym.startswith('.text$') or sym.startswith('.debug$') or sym in exclude:
                            continue
                        syms.append(sym)
                    break
            if syms:
                sym_map[base] = {'fullpath': lib, 'name': name, 'symbols': syms}
    write_out('index/symbols_obj.yaml', sym_map)

def lib() -> None:
    dirs = os.environ['LIB'].split(os.pathsep)
    sym_map = dict()
    for f in dirs:
        if not f:
            continue
        target = f + os.path.sep + "*.lib"
        libs = glob.glob(target, recursive=False)
        for lib in libs:
            print(lib)
            proc = subprocess.run(['dumpbin', '/LINKERMEMBER', lib], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if proc.stderr:
                print("{lib}: {proc.stderr.decode()[:100]}")
            data = proc.stdout.replace(b'\n\r', b'\n').replace(b'\r', b'').decode().split('\n')
            name = os.path.split(lib)[1]
            base = name
            if base in sym_map:
                print(f"Duplicate: {lib}, {sym_map[base]['fullpath']}")
                base = lib
            if base in sym_map:
                break
            syms = set()
            for (ix, line) in enumerate(data):
                line = line.strip()
                parts = line.split()
                if len(parts) == 3 and parts[1] == 'public' and parts[2] == 'symbols':
                    rows = data[ix + 2:]
                    for row in rows:
                        row = row.strip()
                        if not row:
                            break
                        parts = row.split()
                        syms.add(parts[-1])
            sym_map[base] = {'fullpath': lib, 'name': name, 'symbols': list(syms)}
    write_out('index/symbols_lib.yaml', sym_map)


def dll() -> None:
    dirs = os.environ['PATH'].split(os.pathsep)
    sym_map = dict()
    for f in dirs:
        if not f:
            continue
        target = f + os.path.sep + "*.dll"
        dlls = glob.glob(target, recursive=False)

        for dll in dlls:
            print(dll)
            proc = subprocess.run(['dumpbin', '/EXPORTS', dll], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if proc.stderr:
                print(f"{dll}: {proc.stderr.decode()}")
            data = proc.stdout.replace(b'\n\r', b'\n').replace(b'\r', b'').decode().split('\n')
            name = os.path.split(dll)[1]
            base = name
            if base in sym_map:
                print(f"Duplicate: {dll}, {sym_map[base]['fullpath']}")
                base = dll
            if base in sym_map:
                break
            for (ix, line) in enumerate(data):
                line = line.strip()
                parts = line.split()
                if len(parts) == 4 and parts[0] == 'ordinal' and parts[1] == 'hint' and parts[2] == 'RVA' and parts[3] == 'name':
                    rows = data[ix + 2:]
                    syms = []
                    for row in rows:
                        row = row.strip()
                        if not row:
                            break
                        if row[-1] == ')':
                            ix = row.find('(')
                            if ix == -1:
                                continue
                            sym = row[:ix].split()[-1].strip()
                        else:
                            sym = row.split()[-1].strip()
                        if sym == '[NONAME]':
                            continue
                        syms.append(sym)
                    if len(syms) > 0:
                        sym_map[base] = {'fullpath': dll, 'name': name, 'symbols': syms}
                    break
    write_out('index/symbols_dll.yaml', sym_map)


def main() -> None:
    did_dll = False
    did_lib = False
    did_obj = False
    if len(sys.argv) > 1:
        for arg in sys.argv[1:]:
            if arg == 'dll' and not did_dll:
                dll()
                did_dll = True
            if arg == 'lib' and not did_lib:
                lib()
                did_lib = True
            if arg == 'obj' and not did_obj:
                obj()
                did_obj = True
    else:
        dll()
        lib()
        obj()


if __name__ == "__main__":
    main()
