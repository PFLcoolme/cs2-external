#!/usr/bin/env python3
"""CS2 (Linux) read-only offset generator.

Scans the mapped ELF segments of libclient.so through /proc/<pid>/mem, resolves
the three global offsets from code signatures, resolves the netfield offsets by
walking the live SchemaSystem (libschemasystem.so), verifies every value against
the running process, and writes a flat config/offsets.json.

Read-only: the process is opened with O_RDONLY and nothing is ever written to
the game or its files.

Usage:
    python3 cs2_linux_offsets.py           # generate + verify + write
    python3 cs2_linux_offsets.py scan      # dump raw signature hits only
"""
import json
import os
import re
import struct
import sys
import time

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_PATH = os.path.join(PROJECT, "config", "offsets.json")

# ----------------------------------------------------------------------------
# signatures (Linux builds, from blkcatcc/cs2-dumper config.json family)
#   ${'}      rip-relative disp32, next instruction at disp+4
#   ${'a,b}   rip-relative disp32 at offset a, next instruction at offset b
# ----------------------------------------------------------------------------
SIG_GAME_ENTITY_SYSTEM = "48893d${'} e9${} 55"
SIG_VIEW_MATRIX = "c683??000001 4c8d05${'}"
SIG_LOCAL_PAWN_STRUCT = ("488d15${} 488d35${} 488d3d${'} e8${} 31d2 31f6 41b8${} "
                         "488d3d${} b902000000")

SCHEMA_MODULE = "libschemasystem.so"
SCHEMA_INTERFACE = "SchemaSystem_001"
SCHEMA_SCOPE_NAMES = ["libclient.so", "client"]

# SchemaSystem layout on Linux (measured): type scopes UtlVector at +0x1F0,
# registration count at +0x280 (a2x Windows layout uses +0x190).
SCH_TYPE_SCOPES = 0x1F0
SCH_REG_COUNT = 0x280
SCOPE_NAME = 0x08
SCOPE_CLASS_BINDINGS = 0x560
BIND_NAME = 0x08
BIND_FIELD_COUNT = 0x24
BIND_FIELDS = 0x30
FIELD_SIZE = 0x20
FIELD_NAME = 0x00
FIELD_OFFSET = 0x10


# ----------------------------------------------------------------------------
# process / memory
# ----------------------------------------------------------------------------
def find_pid(name="cs2"):
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            with open(f"/proc/{entry}/comm", "rb") as f:
                if f.read().strip() == name.encode():
                    return int(entry)
        except OSError:
            pass
    raise SystemExit("cs2 process not found")


class Proc:
    def __init__(self, pid):
        self.pid = pid
        self.fd = os.open(f"/proc/{pid}/mem", os.O_RDONLY)
        self.maps = self._read_maps()

    def _read_maps(self):
        out = []
        with open(f"/proc/{self.pid}/maps", "r") as f:
            for line in f:
                parts = line.split(None, 5)
                lo, hi = parts[0].split("-")
                out.append(dict(start=int(lo, 16), end=int(hi, 16),
                                perms=parts[1], offset=int(parts[2], 16),
                                path=parts[5].rstrip("\n") if len(parts) >= 6 else ""))
        return out

    def read(self, addr, size):
        if addr is None or addr <= 0:
            return b""
        try:
            return os.pread(self.fd, size, addr)
        except (OSError, OverflowError, ValueError):
            return b""

    def u64(self, addr):
        b = self.read(addr, 8)
        return struct.unpack("<Q", b)[0] if len(b) == 8 else None

    def u32(self, addr):
        b = self.read(addr, 4)
        return struct.unpack("<I", b)[0] if len(b) == 4 else None

    def i32(self, addr):
        b = self.read(addr, 4)
        return struct.unpack("<i", b)[0] if len(b) == 4 else None

    def i16(self, addr):
        b = self.read(addr, 2)
        return struct.unpack("<h", b)[0] if len(b) == 2 else None

    def u8(self, addr):
        b = self.read(addr, 1)
        return b[0] if b else None

    def f32(self, addr):
        b = self.read(addr, 4)
        return struct.unpack("<f", b)[0] if len(b) == 4 else None

    def cstr(self, addr, limit=96):
        b = self.read(addr, limit)
        if not b:
            return ""
        end = b.find(b"\0")
        try:
            return b[:end if end >= 0 else limit].decode("utf-8", "replace")
        except Exception:
            return ""

    def readable(self, addr, size=8):
        return any(m["start"] <= addr and addr + size <= m["end"] and m["perms"][0] == "r"
                   for m in self.maps)


def is_user_ptr(v):
    return v is not None and 0x1000000000 <= v < 0x800000000000


class Module:
    def __init__(self, proc, suffix):
        self.name = suffix
        segs = [m for m in proc.maps if m["path"].endswith(suffix)]
        if not segs:
            raise SystemExit(f"module {suffix} not mapped")
        self.base = min(s["start"] for s in segs)
        self.end = max(s["end"] for s in segs)
        self.segments = sorted(segs, key=lambda m: m["start"])

    def rva(self, addr):
        return addr - self.base

    def addr(self, rva):
        return self.base + rva


def writable_ranges(proc, mod, span=0x6000000):
    """Writable mappings of the module, including its anonymous BSS tail."""
    out = []
    for m in proc.maps:
        if not (mod.base <= m["start"] < mod.base + span and "w" in m["perms"]):
            continue
        if m["path"].endswith(mod.name) or m["path"] == "":
            out.append((m["start"], m["end"]))
    return out


# ----------------------------------------------------------------------------
# pelite pattern subset
# ----------------------------------------------------------------------------
class Pattern:
    FIXED = re.compile(rb"[0-9a-fA-F]{2}")

    def __init__(self, text):
        self.text = text
        rx = b""
        self.captures = []          # (group index, kind, next_offset)
        i = 0
        group = 0
        while i < len(text):
            c = text[i]
            if c in " \t":
                i += 1
                continue
            m = re.match(r"\$\{'(\d+),(\d+)\}", text[i:])
            if m:
                group += 1
                rx += b"(" + b"." * 4 + b")"
                self.captures.append((group, "rip", int(m.group(2))))
                i += m.end()
                continue
            if text.startswith("${'}", i):
                group += 1
                rx += b"(" + b"." * 4 + b")"
                self.captures.append((group, "rip_next4", 0))
                i += 4
                continue
            if text.startswith("${}", i):
                rx += b"." * 4
                i += 3
                continue
            if c == "[":
                j = text.index("]", i)
                rx += b"." * int(text[i + 1:j])
                i = j + 1
                continue
            if c in "?.":
                rx += b"."
                i += 1
                continue
            pair = text[i:i + 2]
            if not self.FIXED.fullmatch(pair.encode()):
                raise SystemExit(f"bad pattern token at {text[i:i + 8]!r} in {text!r}")
            rx += re.escape(bytes([int(pair, 16)]))
            i += 2
        self.regex = re.compile(rx, re.DOTALL)

    def scan(self, blob, blob_base):
        for m in self.regex.finditer(blob):
            hit = {"match": blob_base + m.start()}
            for gid, kind, nxt in self.captures:
                pos = m.start(gid)
                if kind == "rip_next4":
                    nxt = pos + 4
                disp = struct.unpack("<i", m.group(gid))[0]
                hit["rip"] = blob_base + nxt + disp
            yield hit


def scan_code(proc, mod, pattern_text):
    pat = Pattern(pattern_text)
    hits = []
    for seg in mod.segments:
        if "x" not in seg["perms"]:
            continue
        data = proc.read(seg["start"], seg["end"] - seg["start"])
        for hit in pat.scan(data, seg["start"]):
            hits.append(hit)
    return hits


def resolve_single(proc, mod, name, pattern_text):
    hits = scan_code(proc, mod, pattern_text)
    print(f"[sig] {name}: hits={len(hits)}")
    for h in hits[:8]:
        print(f"      match_rva=0x{mod.rva(h['match']):x} target={h['rip']:#x} "
              f"target_rva=0x{mod.rva(h['rip']):x}")
    if len(hits) != 1:
        raise SystemExit(f"signature {name} is not unique ({len(hits)} hits)")
    return hits[0]["rip"]


# ----------------------------------------------------------------------------
# ELF dynsym (CreateInterface export)
# ----------------------------------------------------------------------------
def elf_symbol_rva(path, symbol):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 2:
        raise SystemExit(f"{path}: not ELF64")
    e_shoff = struct.unpack_from("<Q", data, 0x28)[0]
    e_shentsize = struct.unpack_from("<H", data, 0x3A)[0]
    e_shnum = struct.unpack_from("<H", data, 0x3C)[0]
    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        sections.append(dict(
            name=struct.unpack_from("<I", data, off)[0],
            type=struct.unpack_from("<I", data, off + 4)[0],
            offset=struct.unpack_from("<Q", data, off + 0x18)[0],
            size=struct.unpack_from("<Q", data, off + 0x20)[0],
            link=struct.unpack_from("<I", data, off + 0x28)[0],
            entsize=struct.unpack_from("<Q", data, off + 0x38)[0],
        ))
    for sec in sections:
        if sec["type"] not in (2, 11) or not sec["entsize"]:
            continue
        strtab = sections[sec["link"]]
        for i in range(sec["size"] // sec["entsize"]):
            off = sec["offset"] + i * sec["entsize"]
            st_name = struct.unpack_from("<I", data, off)[0]
            st_value = struct.unpack_from("<Q", data, off + 8)[0]
            start = strtab["offset"] + st_name
            end = data.find(b"\0", start)
            if data[start:end].decode("latin1") == symbol:
                return st_value
    raise SystemExit(f"symbol {symbol} not found in {path}")


# ----------------------------------------------------------------------------
# schema system walk
# ----------------------------------------------------------------------------
def find_schema_system(proc, so_path):
    ss_mod = Module(proc, SCHEMA_MODULE)
    create_rva = elf_symbol_rva(so_path, "CreateInterface")
    head_ref = None
    code = proc.read(ss_mod.addr(create_rva), 0x60)
    for i in range(len(code) - 7):
        if code[i] == 0x48 and code[i + 1] == 0x8B and (code[i + 2] & 0xC7) == 0x05:
            disp = struct.unpack_from("<i", code, i + 3)[0]
            head_ref = ss_mod.addr(create_rva + i + 7 + disp)
            break
    if not head_ref:
        raise SystemExit("CreateInterface: interface list reference not found")
    node = proc.u64(head_ref)
    want = SCHEMA_INTERFACE.encode()
    while is_user_ptr(node):
        fn = proc.u64(node)
        name_addr = proc.u64(node + 8)
        nxt = proc.u64(node + 0x10)
        if name_addr and proc.cstr(name_addr, 128).encode() == want:
            body = proc.read(fn, 0x30)
            for i in range(len(body) - 7):
                if body[i] == 0x48 and body[i + 1] == 0x8D and (body[i + 2] & 0xC7) == 0x05:
                    disp = struct.unpack_from("<i", body, i + 3)[0]
                    inst = fn + i + 7 + disp
                    regs = proc.i32(inst + SCH_REG_COUNT)
                    scopes = proc.i32(inst + SCH_TYPE_SCOPES)
                    # the instance lives in the module's BSS, past the last
                    # file-backed mapping, so bound-check on its fields only
                    if regs and 0 < regs < (1 << 20) and scopes and 0 < scopes <= 64:
                        return inst, ss_mod
        if nxt == node:
            break
        node = nxt
    raise SystemExit(f"interface {SCHEMA_INTERFACE} not found")


def valid_binding(proc, bind):
    """A SchemaClassBinding: name ptr @0x08, field count @0x24, fields @0x30."""
    if not is_user_ptr(bind):
        return None
    name_ptr = proc.u64(bind + BIND_NAME)
    if not is_user_ptr(name_ptr):
        return None
    name = proc.cstr(name_ptr, 64)
    if not (3 <= len(name) <= 64):
        return None
    if not all(c.isalnum() or c in "_:<>," for c in name):
        return None
    count = proc.i16(bind + BIND_FIELD_COUNT)
    if count is None or not (0 <= count <= 3000):
        return None
    if not is_user_ptr(proc.u64(bind + BIND_FIELDS)):
        return None
    return name


def hash_pool_bindings(proc, hash_addr):
    """Walk the UtlMemoryPool blobs of a UtlTsHash.

    Blob: next @0x00, num_bytes @0x08, data @0x10.  Element: block_size 0x20,
    key @0x00, next @0x08, data (binding ptr) @0x10.
    """
    out = set()
    blob = proc.u64(hash_addr + 0x48)
    guard = 0
    while is_user_ptr(blob) and guard < 64:
        num_bytes = proc.i32(blob + 0x08) or 0
        for k in range(min(num_bytes // 0x20, 4096)):
            bind = proc.u64(blob + 0x10 + k * 0x20 + 0x10)
            if bind and valid_binding(proc, bind):
                out.add(bind)
        nxt = proc.u64(blob)
        guard += 1
        if nxt == blob:
            break
        blob = nxt
    return out


def ts_hash_elements(proc, addr, cap=40000):
    out = []
    blob = proc.u64(addr + 0x20)
    guard = 0
    while is_user_ptr(blob) and guard < cap:
        nxt = proc.u64(blob)
        data = proc.u64(blob + 0x10)
        if is_user_ptr(data):
            out.append(data)
        guard += 1
        if nxt == blob:
            break
        blob = nxt
    for b in range(256):
        bucket = addr + 0x60 + b * 0x18
        for field in (0x08, 0x10):
            node = proc.u64(bucket + field)
            guard = 0
            while is_user_ptr(node) and guard < 64:
                nxt = proc.u64(node + 8)
                data = proc.u64(node + 0x10)
                if is_user_ptr(data):
                    out.append(data)
                guard += 1
                if nxt == node:
                    break
                node = nxt
    seen = set()
    uniq = []
    for p in out:
        if p not in seen:
            seen.add(p)
            uniq.append(p)
    return uniq


def read_schema_classes(proc, schema, ss_mod, wanted):
    scopes_count = proc.i32(schema + SCH_TYPE_SCOPES)
    scopes_data = proc.u64(schema + SCH_TYPE_SCOPES + 8)
    print(f"[schema] instance=0x{schema:x} (rva=0x{ss_mod.rva(schema):x}) "
          f"registrations={proc.i32(schema + SCH_REG_COUNT)} scopes={scopes_count}")
    fields = {}
    total = 0
    for si in range(max(0, min(scopes_count, 64))):
        scope = proc.u64(scopes_data + 8 * si)
        if not is_user_ptr(scope):
            continue
        name = proc.cstr(scope + SCOPE_NAME, 256)
        if name not in SCHEMA_SCOPE_NAMES:
            continue
        hash_addr = scope + SCOPE_CLASS_BINDINGS
        allocated = proc.i32(hash_addr + 0x0C)
        bindings = hash_pool_bindings(proc, hash_addr)
        bindings |= set(ts_hash_elements(proc, hash_addr))
        bindings = {b for b in bindings if valid_binding(proc, b)}
        total = len(bindings)
        print(f"[schema] scope[{si}]={name!r} allocated={allocated} "
              f"bindings_resolved={total}")
        for bind in bindings:
            cls = valid_binding(proc, bind)
            if cls not in wanted:
                continue
            count = proc.i16(bind + BIND_FIELD_COUNT) or 0
            arr = proc.u64(bind + BIND_FIELDS)
            for fi in range(min(count, 4096)):
                fptr = arr + fi * FIELD_SIZE
                fname = proc.cstr(proc.u64(fptr + FIELD_NAME), 128)
                off = proc.i32(fptr + FIELD_OFFSET)
                if fname and off is not None:
                    fields.setdefault(cls, {}).setdefault(fname, off)
    return fields, total


# ----------------------------------------------------------------------------
# entity system
# ----------------------------------------------------------------------------
def entity(proc, system, index, stride, chunk_base=0x10, ptr_stride=8, shift=9, mask=0x1FF):
    chunk = proc.u64(system + chunk_base + ptr_stride * (index >> shift))
    if not is_user_ptr(chunk):
        return None
    return proc.u64(chunk + stride * (index & mask))


def main():
    pid = find_pid()
    proc = Proc(pid)
    client = Module(proc, "libclient.so")
    print(f"[proc] pid={pid} libclient.so base=0x{client.base:x} "
          f"span=0x{client.end - client.base:x}")

    if len(sys.argv) > 1 and sys.argv[1] == "scan":
        for name, sig in (("dwEntityList", SIG_GAME_ENTITY_SYSTEM),
                          ("dwViewMatrix", SIG_VIEW_MATRIX),
                          ("localPawnStruct", SIG_LOCAL_PAWN_STRUCT)):
            hits = scan_code(proc, client, sig)
            print(f"[scan] {name}: {len(hits)} hits")
            for h in hits[:12]:
                print(f"       match=0x{h['match']:x} rva=0x{client.rva(h['match']):x} "
                      f"target=0x{h['rip']:x} rva=0x{client.rva(h['rip']):x}")
        return

    # --- globals -----------------------------------------------------------
    rva_entity_list = client.rva(resolve_single(
        proc, client, "dwEntityList", SIG_GAME_ENTITY_SYSTEM))
    rva_view_matrix = client.rva(resolve_single(
        proc, client, "dwViewMatrix", SIG_VIEW_MATRIX))
    rva_pawn_struct = client.rva(resolve_single(
        proc, client, "localPawnStruct", SIG_LOCAL_PAWN_STRUCT))

    # --- schema ------------------------------------------------------------
    so_path = None
    ss_mod = Module(proc, SCHEMA_MODULE)
    for m in proc.maps:
        if m["path"].endswith(SCHEMA_MODULE) and m["offset"] == 0:
            so_path = m["path"]
            break
    if not so_path:
        raise SystemExit("cannot locate libschemasystem.so on disk")
    schema, ss_mod = find_schema_system(proc, so_path)
    wanted = {"C_BaseEntity", "CGameSceneNode", "CSkeletonInstance",
              "C_CSPlayerPawn", "C_CSPlayerPawnBase", "EntitySpottedState_t",
              "CCSPlayerController", "CBasePlayerController", "C_BasePlayerPawn",
              "CCSPlayer_AimPunchServices"}
    fields, class_total = read_schema_classes(proc, schema, ss_mod, wanted)

    def need(cls, name):
        off = fields.get(cls, {}).get(name)
        if off is None:
            raise SystemExit(f"schema field {cls}.{name} not resolved")
        print(f"[field] {cls}.{name} = {off} ({off:#x})")
        return off

    def need_any(classes, name):
        """按继承链依次查找：字段由哪个类声明随版本而定（如 m_ArmorValue 在
        C_CSPlayerPawnBase），命中即返回，全部未命中才报错。"""
        for cls in classes:
            off = fields.get(cls, {}).get(name)
            if off is not None:
                print(f"[field] {cls}.{name} = {off} ({off:#x})")
                return off
        raise SystemExit(f"schema field {name} not resolved in {classes}")

    m_iHealth = need("C_BaseEntity", "m_iHealth")
    m_ArmorValue = need_any(["C_CSPlayerPawn", "C_CSPlayerPawnBase"], "m_ArmorValue")
    m_iTeamNum = need("C_BaseEntity", "m_iTeamNum")
    m_lifeState = need("C_BaseEntity", "m_lifeState")
    m_pGameSceneNode = need("C_BaseEntity", "m_pGameSceneNode")
    m_vecAbsOrigin = need("CGameSceneNode", "m_vecAbsOrigin")
    # CSkeletonInstance.m_modelState 是 CModelState 结构体起点（本机 0x140），
    # 而骨骼数组指针在 scene + 0x1C0（实测校验 16/16 通过），即结构体内 +0x80。
    # 两者语义不同，直接写进 offsets.json 会让骨骼读错指针，故只打印不输出。
    m_modelState = need("CSkeletonInstance", "m_modelState")
    print(f"[field] m_modelState = {m_modelState} ({m_modelState:#x}) "
          f"struct base; bone array pointer = scene + 0x1c0 (measured, kept in config)")
    m_iIDEntIndex = need("C_CSPlayerPawn", "m_iIDEntIndex")
    m_entitySpottedState = need("C_CSPlayerPawn", "m_entitySpottedState")
    m_bSpotted_local = need("EntitySpottedState_t", "m_bSpotted")
    m_bSpotted = m_entitySpottedState + m_bSpotted_local
    print(f"[field] m_bSpotted = {m_bSpotted} ({m_bSpotted:#x}) "
          f"= m_entitySpottedState + m_bSpotted")
    def maybe_any(classes, name):
        """可选字段：命中返回偏移，未命中返回 None 且不终止生成。
        后坐力抵消依赖这两个字段，缺失时功能在客户端侧自动关闭。"""
        for cls in classes:
            off = fields.get(cls, {}).get(name)
            if off is not None:
                print(f"[field] {cls}.{name} = {off} ({off:#x})")
                return off
        print(f"[field] {name} not present in schema (recoil disabled)")
        return None

    # 后坐力角度需要两级跳：C_CSPlayerPawn.m_pAimPunchServices 是指针，
    # 指向 CCSPlayer_AimPunchServices；后坐基准角是其中的 m_predictableBaseAngle
    # （QAngle，12 字节；紧随其后的 m_predictableBaseAngleVel 可反证宽度）。
    # 本版本 schema 没有 C_CSPlayerPawn.m_aimPunchAngle / m_aimPunchCache。
    m_pAimPunchServices = maybe_any(["C_CSPlayerPawn", "C_CSPlayerPawnBase"],
                                    "m_pAimPunchServices")
    m_aimPunchAngle = maybe_any(["CCSPlayer_AimPunchServices"], "m_predictableBaseAngle")
    m_bIsLocalPlayerController = need("CBasePlayerController", "m_bIsLocalPlayerController")
    m_hPlayerPawn = need("CCSPlayerController", "m_hPlayerPawn")
    m_hController = need("C_BasePlayerPawn", "m_hController")

    # --- entity list -------------------------------------------------------
    system = proc.u64(client.addr(rva_entity_list))
    if not is_user_ptr(system):
        raise SystemExit("dwEntityList did not yield a valid CGameEntitySystem")
    print(f"[entity] dwEntityList rva=0x{rva_entity_list:x} "
          f"CGameEntitySystem=0x{system:x} vtable=0x{proc.u64(system) or 0:x}")

    STRIDE = 0x70
    chunks = []
    for i in range(0, 8):
        c = proc.u64(system + 0x10 + 8 * i)
        chunks.append(c)
    print(f"[entity] chunk[0..7]=" + " ".join(f"{c and hex(c) or 'null'}" for c in chunks))
    samples = 0
    for idx in range(1, 2048):
        ent = entity(proc, system, idx, STRIDE)
        if not is_user_ptr(ent):
            continue
        vt = proc.u64(ent)
        team = proc.i32(ent + m_iTeamNum)
        hp = proc.i32(ent + m_iHealth)
        life = proc.u8(ent + m_lifeState)
        print(f"[entity] idx={idx:<5} ent=0x{ent:x} vtable=0x{(vt or 0):x} "
              f"team={team} health={hp} lifeState={life}")
        samples += 1
        if samples >= 5:
            break
    if samples == 0:
        print("[entity] no live entity slot found (menu/loading?)")

    # stride sanity: 0x78 must not produce a pointer-looking value
    bad = entity(proc, system, 2, 0x78)
    good = entity(proc, system, 2, STRIDE)
    print(f"[entity] stride check: 0x70 -> {good and hex(good) or 'null'}; "
          f"0x78 -> {bad and hex(bad) or 'null'} "
          f"(0x78 value_is_ptr={bool(is_user_ptr(bad))})")

    # --- local player: the controller entity flagged by the engine ----------
    local_ctrl = None
    scanned = 0
    for idx in range(1, 2048):
        e = entity(proc, system, idx, STRIDE)
        if not is_user_ptr(e):
            continue
        scanned += 1
        vt = proc.u64(e)
        if not (client.base <= (vt or 0) < client.base + 0x5000000):
            continue
        if proc.u8(e + m_bIsLocalPlayerController) != 1:
            continue
        handle = proc.u32(e + m_hPlayerPawn)
        if handle is None:
            continue
        pidx = handle & 0x7FFF
        p = entity(proc, system, pidx, STRIDE)
        if is_user_ptr(p):
            local_ctrl = (idx, e, handle, pidx, p)
            break
    if local_ctrl is None:
        raise SystemExit("no controller with m_bIsLocalPlayerController=1 (not in a match)")
    ctrl_idx, controller, handle, pawn_index, pawn = local_ctrl
    print(f"[pawn] local controller idx={ctrl_idx} addr=0x{controller:x} "
          f"m_hPlayerPawn=0x{handle:x} index={pawn_index} pawn=0x{pawn:x} "
          f"(entity slots scanned={scanned})")

    # --- dwLocalPlayerPawn: where the module stores that pointer -----------
    # The global lives in the module's BSS, which is an anonymous mapping that
    # starts where the last file-backed mapping ends.  Scan every writable
    # module (or module-adjacent anonymous) range for the exact pointer value.
    needle = struct.pack("<Q", pawn)
    hits = []
    for attempt in range(3):
        hits = []
        for start, end in writable_ranges(proc, client):
            blob = proc.read(start, end - start)
            pos = blob.find(needle)
            while pos >= 0:
                hits.append(start + pos)
                pos = blob.find(needle, pos + 1)
        hits = sorted(set(hits))
        if hits:
            break
        time.sleep(0.5)
    if not hits:
        raise SystemExit("local pawn pointer not found in module writable range")
    for h in hits:
        print(f"[pawn] pawn pointer at rva=0x{client.rva(h):x}")
    in_struct = [h for h in hits
                 if client.addr(rva_pawn_struct) <= h < client.addr(rva_pawn_struct) + 0x400]
    target = in_struct[0] if len(in_struct) == 1 else (hits[0] if len(hits) == 1 else None)
    if target is None:
        raise SystemExit("local pawn pointer is ambiguous")
    rva_local_pawn = client.rva(target)
    delta = target - client.addr(rva_pawn_struct)
    verified = proc.u64(client.addr(rva_local_pawn))
    print(f"[pawn] localpawn struct rva=0x{rva_pawn_struct:x} + 0x{delta:x} -> "
          f"dwLocalPlayerPawn=0x{rva_local_pawn:x} value=0x{(verified or 0):x} "
          f"match={verified == pawn}")
    back = proc.u32(pawn + m_hController)
    print(f"[pawn] pawn.m_hController=0x{back:x} index={back & 0x7FFF} "
          f"back_to_local_controller={(back & 0x7FFF) == ctrl_idx}")

    # --- view matrix -------------------------------------------------------
    vm_addr = client.addr(rva_view_matrix)
    first = [proc.f32(vm_addr + 4 * i) for i in range(16)]
    print("[view] dwViewMatrix rva=0x%x sample1=%s" % (rva_view_matrix,
          " ".join(f"{v:.5f}" for v in first)))
    time.sleep(0.2)
    second = [proc.f32(vm_addr + 4 * i) for i in range(16)]
    print("[view] sample2=%s" % " ".join(f"{v:.5f}" for v in second))
    print(f"[view] changing={first != second}")

    # --- pawn verification -------------------------------------------------
    scene = proc.u64(pawn + m_pGameSceneNode)
    origin = None
    if is_user_ptr(scene):
        origin = tuple(proc.f32(scene + m_vecAbsOrigin + 4 * i) for i in range(3))
    health = proc.i32(pawn + m_iHealth)
    armor = proc.i32(pawn + m_ArmorValue)
    team = proc.i32(pawn + m_iTeamNum)
    life = proc.u8(pawn + m_lifeState)
    spotted = proc.u8(pawn + m_bSpotted)
    print(f"[verify] localPawn health={health} armor={armor} team={team} lifeState={life} "
          f"spotted={spotted} sceneNode={scene and hex(scene) or 'null'} origin={origin}")
    origin_ok = (origin is not None and all(v is not None and abs(v) < 100000 for v in origin)
                 and any(abs(v) > 0.001 for v in origin))

    # --- output ------------------------------------------------------------
    result = {
        "dwEntityList": rva_entity_list,
        "dwLocalPlayerPawn": rva_local_pawn,
        "dwViewMatrix": rva_view_matrix,
        "m_iHealth": m_iHealth,
        "m_ArmorValue": m_ArmorValue,
        "m_iTeamNum": m_iTeamNum,
        "m_lifeState": m_lifeState,
        "m_pGameSceneNode": m_pGameSceneNode,
        "m_vecAbsOrigin": m_vecAbsOrigin,
        "m_iIDEntIndex": m_iIDEntIndex,
        "m_bSpotted": m_bSpotted,
        "m_aimPunchAngle": m_aimPunchAngle or 0,
        "m_pAimPunchServices": m_pAimPunchServices or 0,
    }
    with open(OUT_PATH, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
        f.write("\n")
    print(f"[out] wrote {OUT_PATH}")
    for k, v in result.items():
        print(f"       {k:<20} = {v} (0x{v:x})")
    print(f"[check] origin_plausible={origin_ok} entity_stride=0x{STRIDE:x} "
          f"classes_in_libclient={class_total}")


if __name__ == "__main__":
    main()
