"""Audit every write to the cartridge mapper in the linked ROMs: `python tools/mapper_audit.py MAP [MAP ...]`.

The map of a link (build/linked-mbc5.map, build/linked-mbc3.map) lists every module that went in: the game's own
objects (whose SDCC assembly is kept beside them, build/obj*/x.asm) and the GBDK runtime and library members
(crt0.o, gb.lib, sm83.lib). For each one this finds the stores whose target is 0x0000-0x7FFF:

  - the game's assembly: `ld (nn), a` and stores through hl/bc/de after the register was loaded with a constant or a
    mapper register symbol (_rRAMG 0x0000, _rROMB0 0x2000, _rROMB1 0x3000, _rRAMB 0x4000), tracked per basic block;
  - the library objects: their ASxxxx relocatable code, relocated fields collapsed to their width, decoded linearly,
    the same patterns (the library is hand-written assembly with its data in separate areas).

Each write is classified by the register it hits:

  0x0000-0x1FFF  RAM/RTC enable                 allowed in both builds
  0x2000-0x2FFF  ROM bank (low 8 bits / 7 bits)  allowed in both builds
  0x3000-0x3FFF  MBC5 ROM bank bit 8             forbidden: on MBC3 it is still the ROM bank register
  0x4000-0x5FFF  RAM bank / RTC register select  allowed in both builds
  0x6000-0x7FFF  MBC3 RTC latch                  allowed only in the MBC3 build, only from src/clock.c

Prints the table; exit status 1 if any write is forbidden. `--self-test` checks the scanners find planted writes.
"""

from __future__ import annotations

import re
import sys
import os
import json
import hashlib
import tempfile
from functools import lru_cache
from pathlib import Path

GBDK = Path.home() / ".cache/gbdk-4.5.0"
@lru_cache(maxsize=4)
def artifact_files(name: str) -> dict:
    data=json.loads(Path(name).read_text())
    if data.get('format')!=1 or not isinstance(data.get('files'),dict):
        raise ValueError('invalid linked-artifact manifest')
    return data['files']

def linked_file(path: Path) -> Path:
    """An explicit hash manifest relocates copied links, never guesses a SDK."""
    name=os.environ.get('EGRESS_MAPPER_MANIFEST')
    if not name:return path
    files=artifact_files(name)
    if str(path) not in files:raise ValueError('unrecorded linked input: '+str(path))
    row=files[str(path)];rel=Path(row['path'])
    if rel.is_absolute() or '..' in rel.parts:raise ValueError('linked input leaves its artifact snapshot')
    local=Path(name).resolve().parent/rel
    if hashlib.sha256(local.read_bytes()).hexdigest()!=row['sha256']:
        raise ValueError('linked input hash mismatch: '+str(path))
    return local

def relocation_self_test() -> None:
    previous=os.environ.get('EGRESS_MAPPER_MANIFEST')
    try:
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);sample=root/'sample.o';sample.write_bytes(b'exact linked bytes')
            manifest=root/'manifest.json';origin=Path('/build-host/gbdk/lib/gb/crt0.o')
            manifest.write_text(json.dumps({'format':1,'files':{str(origin):{'path':'sample.o','sha256':hashlib.sha256(sample.read_bytes()).hexdigest()}}}))
            os.environ['EGRESS_MAPPER_MANIFEST']=str(manifest)
            assert linked_file(origin).read_bytes()==b'exact linked bytes'
            sample.write_bytes(b'wrong linked bytes')
            try:linked_file(origin)
            except ValueError:pass
            else:raise AssertionError('changed linked input accepted')
            try:linked_file(Path('/unknown/library.o'))
            except ValueError:pass
            else:raise AssertionError('unrecorded linked input accepted')
    finally:
        if previous is None:os.environ.pop('EGRESS_MAPPER_MANIFEST',None)
        else:os.environ['EGRESS_MAPPER_MANIFEST']=previous
        artifact_files.cache_clear()
SYMS = {"_rRAMG": 0x0000, "_rROMB0": 0x2000, "_rROMB1": 0x3000, "_rRAMB": 0x4000}
NAMES = [(0x2000, "RAM enable"), (0x3000, "ROM bank"), (0x4000, "ROM bank bit 8"), (0x6000, "RAM bank / RTC select"),
         (0x8000, "RTC latch")]


def region(a: int) -> str:
    return next(n for top, n in NAMES if a < top)


# --- the game's own assembly ---
IMM = re.compile(r"^\s*ld\s+(hl|bc|de)\s*,\s*#(0x[0-9a-fA-F]+|\d+|_\w+)\s*$")
DIRECT = re.compile(r"^\s*ld\s+\(#?(0x[0-9a-fA-F]+|_\w+)\)\s*,\s*a\s*$")
STORE = re.compile(r"^\s*ld\s+\((hl|bc|de)[+-]?\)\s*,")
HALVES = {"hl": ("hl", "h", "l"), "bc": ("bc", "b", "c"), "de": ("de", "d", "e")}


def value(tok: str) -> int | None:
    if tok in SYMS:
        return SYMS[tok]
    if tok.startswith("_"):
        return None
    return int(tok, 16) if tok.startswith("0x") else int(tok)


def scan_asm(path: Path) -> list[tuple[str, int, str]]:
    out = []
    regs: dict[str, int | None] = {}
    for n, raw in enumerate(path.read_text(encoding="latin-1").splitlines(), 1):
        line = raw.split(";")[0].rstrip()
        if not line.strip() or line.lstrip().startswith("."):
            continue
        if re.match(r"^\s*[\w$.]+::?", line) or re.match(r"^\s*(call|ret|reti|rst|pop)\b", line):
            regs = {}  # a label (control can arrive from elsewhere), a call or a pop: nothing is known
            continue
        m = DIRECT.match(line)
        if m:
            v = value(m[1])
            if v is not None and v < 0x8000:
                out.append((f"{path}:{n}", v, line.strip()))
            continue
        m = IMM.match(line)
        if m:
            regs[m[1]] = value(m[2])
            continue
        m = STORE.match(line)
        if m:
            v = regs.get(m[1])
            if v is not None and v < 0x8000:
                out.append((f"{path}:{n}", v, line.strip()))
        if "(hl+)" in line or "(hl-)" in line or re.match(r"^\s*ldhl\b", line):
            regs["hl"] = None
        ops = line.split(None, 1)
        dest = ops[1].split(",")[0].strip() if len(ops) > 1 else ""
        for r, names in HALVES.items():
            if dest in names:
                regs[r] = None
    return out


# --- library members: ASxxxx relocatable objects ---
LEN2 = {0x06, 0x0E, 0x16, 0x1E, 0x26, 0x2E, 0x36, 0x3E, 0x18, 0x20, 0x28, 0x30, 0x38, 0xC6, 0xCE, 0xD6, 0xDE, 0xE6,
        0xEE, 0xF6, 0xFE, 0xE0, 0xF0, 0xE8, 0xF8, 0xCB, 0x10}
LEN3 = {0x01, 0x11, 0x21, 0x31, 0x08, 0xC2, 0xC3, 0xCA, 0xD2, 0xDA, 0xC4, 0xCC, 0xCD, 0xD4, 0xDC, 0xEA, 0xFA}


def ar_members(lib: Path) -> dict[str, bytes]:
    data = lib.read_bytes()
    assert data[:8] == b"!<arch>\n"
    p, longn, out = 8, b"", {}
    while p < len(data):
        h = data[p:p + 60]
        name, size = h[:16].decode().strip(), int(h[48:58])
        body = data[p + 60:p + 60 + size]
        p += 60 + size + (size & 1)
        if name == "//":
            longn = body
            continue
        if name == "/":
            continue
        if name.startswith("/"):
            o = int(name[1:])
            name = longn[o:longn.index(b"/", o)].decode()
        out[name.rstrip("/")] = body
    return out


def rel_code(text: str) -> dict[str, list[tuple[int, object]]]:
    """area name -> [(offset, byte or ('sym', name) / ('area',))] with relocated fields collapsed to their width"""
    syms: list[str] = []
    areas: list[str] = []
    out: dict[str, dict[int, object]] = {}
    lines = text.splitlines()
    for i, ln in enumerate(lines):
        f = ln.split()
        if not f:
            continue
        if f[0] == "S":
            syms.append(f[1])
        elif f[0] == "A":
            areas.append(f[1])
        elif f[0] == "T" and i + 1 < len(lines) and lines[i + 1].startswith("R"):
            t = [int(x, 16) for x in f[1:]]
            r = [int(x, 16) for x in lines[i + 1].split()[1:]]
            area = areas[r[2] | r[3] << 8]
            rel = {}
            for k in range(4, len(r), 4):
                mode, off, idx = r[k], r[k + 1], r[k + 2] | r[k + 3] << 8
                rel[off] = (mode, syms[idx] if mode & 0x02 else "area")
            addr = t[0] | t[1] << 8 | t[2] << 16 | t[3] << 24
            mem = out.setdefault(area, {})
            j = 4
            while j < len(t):
                if j in rel:
                    mode, what = rel[j]
                    width = 1 if mode & 0x01 else 2
                    for w in range(width):
                        mem[addr + w] = ("sym", what, w)
                    addr += width
                    j += 4
                else:
                    mem[addr] = t[j]
                    addr += 1
                    j += 1
    return {a: sorted(m.items()) for a, m in out.items()}


def operand16(cells: list, i: int) -> int | None:
    lo, hi = cells[i], cells[i + 1]
    if isinstance(lo, tuple):
        return SYMS.get(lo[1]) if lo[1] in SYMS else None
    if isinstance(hi, tuple):
        return None
    return lo | hi << 8


def scan_rel(name: str, text: str) -> list[tuple[str, int, str]]:
    out = []
    for area, cells in rel_code(text).items():
        if not cells:
            continue
        mem = dict(cells)
        start, end = cells[0][0], cells[-1][0] + 1
        flat = [mem.get(a, 0) for a in range(start, end)]
        i, hl = 0, None
        while i < len(flat):
            op = flat[i]
            if isinstance(op, tuple):
                i += 1
                continue
            n = 3 if op in LEN3 else 2 if op in LEN2 else 1
            if i + n > len(flat):
                break
            if op == 0xEA:
                v = operand16(flat, i + 1)
                if v is not None and v < 0x8000:
                    out.append((f"{name} {area}+0x{start + i:04x}", v, f"ld (0x{v:04x}), a"))
            elif op == 0x21:
                hl = operand16(flat, i + 1)
                i += n
                continue
            elif op in (0x77, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x36, 0x22, 0x32) and hl is not None and hl < 0x8000:
                out.append((f"{name} {area}+0x{start + i:04x}", hl, f"ld (hl), ... with hl = 0x{hl:04x}"))
            if op in (0x21, 0x23, 0x2B, 0x09, 0x19, 0x29, 0x39, 0xE1, 0xF8, 0x22, 0x32, 0x2A, 0x3A) or 0x60 <= op <= 0x6F \
                    or op in (0x24, 0x25, 0x2C, 0x2D, 0x26, 0x2E, 0xCD, 0xC9):
                hl = None
            i += n
    return out


def modules(map_path: Path) -> tuple[list[Path], list[tuple[Path, str]]]:
    own, libs = [], []
    text = map_path.read_text(encoding="latin-1")
    files = text[text.index("Files Linked"):]
    lib = None
    for ln in files.splitlines()[1:]:
        s = ln.strip()
        if not s:
            continue
        m = re.match(r"^(\S+\.(?:rel|o))\s+\[", s)
        if m and not s.startswith("["):
            p = Path(m[1])
            if p.name == "crt0.o":
                libs.append((p, ""))
            else:
                own.append(p.with_suffix(".asm"))
            continue
        if s.endswith(".lib"):
            lib = Path(s)
            continue
        m = re.match(r"^\[\s*(\S+)\s*\]$", s)
        if m and lib is not None:
            libs.append((lib, m[1]))
            lib = None
    return own, libs


def audit(map_path: Path) -> int:
    mbc3 = "mbc3" in map_path.name
    own, libs = modules(linked_file(map_path))
    rows = []
    for asm in own:
        rows += [(w, v, s, asm.stem) for w, v, s in scan_asm(linked_file(asm))]
    cache: dict[Path, dict[str, bytes]] = {}
    for lib, member in libs:
        lib=linked_file(lib)
        if member:
            body = cache.setdefault(lib, ar_members(lib))[member]
            label = f"{lib.name}:{member}"
        else:
            body = lib.read_bytes()
            label = lib.name
        rows += [(w, v, s, member or lib.name) for w, v, s in scan_rel(label, body.decode("latin-1"))]
    bad = 0
    print(f"{map_path}: {'MBC3' if mbc3 else 'MBC5'} build, {len(own)} game modules, {len(libs)} runtime/library modules")
    for where, v, what, mod in rows:
        verdict = "ok"
        if 0x3000 <= v < 0x4000:
            verdict = "FORBIDDEN (MBC5 high ROM bit; the ROM bank register on MBC3)"
        elif 0x6000 <= v < 0x8000 and not (mbc3 and mod == "clock"):
            verdict = "FORBIDDEN (RTC latch outside the MBC3 clock)"
        bad += verdict != "ok"
        print(f"  0x{v:04x} {region(v):22} {what:42} {where}  {verdict}")
    print(f"  {len(rows)} mapper writes, {bad} forbidden")
    return bad


def self_test() -> int:
    relocation_self_test()
    """the scanners must see the writes they are for: an MBC5 high-bit write, a latch, and the library trampolines"""
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        asm = Path(d) / "x.asm"
        asm.write_text("_f::\n\tld\thl, #_rROMB1\n\tld\t(hl), #0x00\n\tld\ta, #1\n\tld\t(#0x6000),a\n"
                       "\tld\thl, #0x0008\n\tadd\thl, sp\n\tld\t(hl), a\n\tret\n")
        got = sorted(v for _, v, _ in scan_asm(asm))
    lib = ar_members(GBDK / "lib/gb/gb.lib")
    far = [v for _, v, _ in scan_rel("far_ptr.o", lib["far_ptr.o"].decode("latin-1"))]
    ok = got == [0x3000, 0x6000] and far == [0x2000, 0x2000]
    print(f"self-test: asm {[hex(v) for v in got]}, far_ptr.o {[hex(v) for v in far]}: {'ok' if ok else 'FAIL'}")
    return 0 if ok else 1


def main() -> int:
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    bad = sum(audit(Path(p)) for p in sys.argv[1:])
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
