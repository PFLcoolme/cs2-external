#!/usr/bin/env python3
"""纯读探针：显示散布 / 枪口抖动字段的当前值。

用来验证「弹着点收成一点」是否把动态字段压在 0。不写任何内存。
需要 cs2 处于对局中；未持枪 / 观战时武器实体链为 0，属预期。

用法：python3 tools/cs2_probe_accuracy.py [采样次数，默认 3]
"""
import json
import os
import struct
import sys
import time

OFFSETS = os.path.join(os.path.dirname(__file__), "..", "config", "offsets.json")
SAMPLES = int(sys.argv[1]) if len(sys.argv) > 1 else 3


def find_pid():
    for e in os.listdir("/proc"):
        if not e.isdigit():
            continue
        try:
            if open(f"/proc/{e}/comm", "rb").read().strip() == b"cs2":
                return int(e)
        except OSError:
            pass
    return None


def module_base(pid):
    base = None
    for line in open(f"/proc/{pid}/maps"):
        p = line.split(None, 5)
        if len(p) >= 6 and p[5].strip().endswith("libclient.so") and p[1].startswith("r"):
            c = int(p[0].split("-")[0], 16) - int(p[2], 16)
            if base is None or c < base:
                base = c
    return base


class Reader:
    def __init__(self, pid):
        self.fd = os.open(f"/proc/{pid}/mem", os.O_RDONLY)

    def raw(self, a, n):
        try:
            return os.pread(self.fd, n, a)
        except (OSError, OverflowError, ValueError):
            return b""

    def u64(self, a):
        b = self.raw(a, 8)
        return struct.unpack("<Q", b)[0] if len(b) == 8 else 0

    def u32(self, a):
        b = self.raw(a, 4)
        return struct.unpack("<I", b)[0] if len(b) == 4 else 0xFFFFFFFF

    def i32(self, a):
        b = self.raw(a, 4)
        return struct.unpack("<i", b)[0] if len(b) == 4 else None

    def f32(self, a):
        b = self.raw(a, 4)
        return round(struct.unpack("<f", b)[0], 5) if len(b) == 4 else None

    def close(self):
        os.close(self.fd)


def resolve_weapon(cfg, r, base, pawn):
    system = r.u64(base + cfg.get("dwEntityList", 0)) if base else 0
    ws_off = cfg.get("m_pWeaponServices", 0)
    h_off = cfg.get("m_hActiveWeapon", 0)
    if not ws_off or not h_off or not system:
        return 0, 0
    services = r.u64(pawn + ws_off)
    if not services:
        return 0, 0
    handle = r.u32(services + h_off)
    index = handle & 0x7FFF
    if handle == 0xFFFFFFFF or index <= 0:
        return handle, 0
    chunk = r.u64(system + cfg.get("entityListChunkBase", 16) +
                  cfg.get("entityChunkPtrStride", 8) *
                  (index >> cfg.get("entityChunkShift", 9)))
    if not chunk:
        return handle, 0
    weapon = r.u64(chunk + cfg.get("entityChunkStride", 112) *
                   (index & cfg.get("entityChunkMask", 511)))
    return handle, weapon


FLOAT_FIELDS = ("m_flFlinchStack", "m_flInterpolatedInaccuracy",
                "m_fAccuracyPenalty", "m_fAccuracySmoothedForZoom",
                "m_flTurningInaccuracy", "m_flTurningInaccuracyDelta",
                "m_flRecoilIndex", "m_flLastShakeTime")
INT_FIELDS = ("m_iShotsFired", "m_iRecoilIndex")
PAWN_FIELDS = {"m_iShotsFired", "m_flFlinchStack", "m_flInterpolatedInaccuracy"}


def main():
    pid = find_pid()
    if not pid:
        print("cs2 未运行")
        return 2
    cfg = json.load(open(OFFSETS))
    base = module_base(pid)
    if not base:
        print("未找到 libclient.so 基址")
        return 2
    r = Reader(pid)
    pawn = r.u64(base + cfg["dwLocalPlayerPawn"])
    if not pawn:
        print("本地 pawn 为 0：需要先进入对局")
        r.close()
        return 2
    camera = r.u64(pawn + cfg.get("m_pCameraServices", 0)) if cfg.get("m_pCameraServices") else 0
    print(f"pid={pid} base=0x{base:x} pawn=0x{pawn:x} camera=0x{camera:x}")
    print("字段为 0 表示引擎已把它压住；非 0 表示游戏正在重算（客户端预测）。\n")

    for sample in range(SAMPLES):
        handle, weapon = resolve_weapon(cfg, r, base, pawn)
        print(f"--- 采样 {sample}  handle=0x{handle:x} weapon=0x{weapon:x} ---")
        for name in FLOAT_FIELDS + INT_FIELDS:
            off = cfg.get(name, 0)
            if not off:
                continue
            where = "pawn" if name in PAWN_FIELDS else "weapon"
            owner = pawn if where == "pawn" else weapon
            if not owner:
                print(f"  {name:<32} ({where}) 链无效，跳过")
                continue
            value = r.i32(owner + off) if name in INT_FIELDS else r.f32(owner + off)
            print(f"  {name:<32} ({where}) = {value}")
        if camera and cfg.get("m_vClientScopeInaccuracy"):
            print(f"  m_vClientScopeInaccuracy     (camera) = "
                  f"{r.f32(camera + cfg['m_vClientScopeInaccuracy'])}")
        if weapon == 0:
            print("  武器链无效：当前未持枪 / 观战 / 死亡，武器侧字段不参与判定")
        if sample < SAMPLES - 1:
            time.sleep(0.2)
    print("\n静态 VData 的 m_flSpread 偏移已定位但缺指针链，本工具不读取。")
    r.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
