#!/usr/bin/env python3
"""外部写内存功能可行性哨兵探针。

回答的问题不是「我们写进去了吗」，而是「游戏会不会把它改回去」。

原理：对每个目标字段读原值 → 写一个与原值不同的哨兵 → 以 1 ms 间隔
高频读回 N 次（跨越多个 64 Hz tick）→ 统计哨兵存活比例 → 写回原值。
存活比例高，说明外部写能保持；存活比例低，说明客户端预测/服务器快照
每 tick 覆盖该字段，外部 pwrite 在服务器权威下不生效。

只写 float，哨兵取「原值 + 小量」，结束后一律恢复，不写指针、不写整数状态。
需要 cs2 正在运行且已进入对局；读 /proc/<pid>/maps，读写 /proc/<pid>/mem。

用法：python3 tools/cs2_probe_features.py [采样次数，默认 96]
"""

import json
import os
import struct
import sys
import time

OFFSETS_PATH = os.path.join(os.path.dirname(__file__), "..", "config", "offsets.json")
SAMPLES = int(sys.argv[1]) if len(sys.argv) > 1 else 96
SAMPLE_INTERVAL = 0.001  # 1 ms，覆盖约 6 个 64 Hz tick


def find_pid(name=b"cs2"):
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            if open(f"/proc/{entry}/comm", "rb").read().strip() == name:
                return int(entry)
        except OSError:
            pass
    return None


def module_base(pid, module="libclient.so"):
    base = None
    for line in open(f"/proc/{pid}/maps"):
        parts = line.split(None, 5)
        if len(parts) < 6 or not parts[5].strip().endswith(module):
            continue
        if not parts[1].startswith("r"):
            continue
        candidate = int(parts[0].split("-")[0], 16) - int(parts[2], 16)
        if base is None or candidate < base:
            base = candidate
    return base


class Proc:
    def __init__(self, pid):
        self.fd = os.open(f"/proc/{pid}/mem", os.O_RDWR)

    def read(self, addr, size):
        try:
            return os.pread(self.fd, size, addr)
        except (OSError, OverflowError, ValueError):
            return b""

    def write(self, addr, data):
        try:
            return os.pwrite(self.fd, data, addr)
        except (OSError, OverflowError, ValueError):
            return 0

    def u64(self, addr):
        raw = self.read(addr, 8)
        return struct.unpack("<Q", raw)[0] if len(raw) == 8 else 0

    def f32(self, addr):
        raw = self.read(addr, 4)
        return struct.unpack("<f", raw)[0] if len(raw) == 4 else None

    def close(self):
        os.close(self.fd)


def probe(label, proc, addr, sentinel_delta=1.0):
    """返回 (verdict, held_ratio)。哨兵只改第一个 float。"""
    if addr == 0:
        return "NO_ADDRESS", 0.0
    original = proc.f32(addr)
    if original is None:
        return "UNREADABLE", 0.0

    sentinel = original + sentinel_delta
    written = proc.write(addr, struct.pack("<f", sentinel))
    if written != 4:
        return "UNWRITABLE", 0.0

    held = 0
    for _ in range(SAMPLES):
        value = proc.f32(addr)
        # 允许 0.01 的浮点误差；游戏自己写回的值通常与哨兵明显不同。
        if value is not None and abs(value - sentinel) < 0.01:
            held += 1
        time.sleep(SAMPLE_INTERVAL)

    proc.write(addr, struct.pack("<f", original))  # 幂等恢复
    ratio = held / SAMPLES
    if ratio >= 0.9:
        verdict = "HELD"
    elif ratio >= 0.2:
        verdict = "PARTIAL"
    else:
        verdict = "OVERWRITTEN"
    return verdict, ratio


def main():
    pid = find_pid()
    if not pid:
        print("cs2 未运行：探针需要游戏进程。先启动游戏再跑本工具。")
        return 2
    base = module_base(pid)
    if not base:
        print("未找到 libclient.so 基址")
        return 2

    cfg = json.load(open(OFFSETS_PATH))
    proc = Proc(pid)
    glp = cfg.get("dwLocalPlayerPawn", 0)
    pawn = proc.u64(base + glp)
    if not pawn:
        print(f"本地 pawn 指针为 0（base=0x{base:x} dwLocalPlayerPawn=0x{glp:x}）："
              "需要先进入对局，停在主菜单时该全局尚未写入。")
        print("若已在对局，说明偏移与当前版本不匹配：重跑 tools/cs2_linux_offsets.py")
        proc.close()
        return 2

    scene = proc.u64(pawn + cfg.get("m_pGameSceneNode", 0))
    punch = proc.u64(pawn + cfg.get("m_pAimPunchServices", 0))
    camera = proc.u64(pawn + cfg.get("m_pCameraServices", 0))
    movement = proc.u64(pawn + cfg.get("m_pMovementServices", 0))
    print(f"pid={pid} base=0x{base:x} pawn=0x{pawn:x} scene=0x{scene:x} "
          f"punch=0x{punch:x} camera=0x{camera:x} movement=0x{movement:x}")
    print(f"采样 {SAMPLES} 次 × {SAMPLE_INTERVAL * 1000:.0f} ms，判定阈值 HELD>=0.9 / PARTIAL>=0.2\n")

    def at(base_addr, key):
        off = cfg.get(key, 0)
        return base_addr + off if base_addr and off else 0

    targets = [
        ("加速", "m_flVelocityModifier", at(pawn, "m_flVelocityModifier"), 0.5),
        ("加速", "m_flMaxspeed", at(movement, "m_flMaxspeed"), 25.0),
        ("真子弹追踪", "m_angEyeAngles", at(pawn, "m_angEyeAngles"), 1.0),
        ("真子弹追踪", "m_angStashedShootAngles", at(pawn, "m_angStashedShootAngles"), 1.0),
        ("飞天", "m_vecVelocity", at(pawn, "m_vecVelocity"), 25.0),
        ("飞天", "m_vecAbsVelocity", at(pawn, "m_vecAbsVelocity"), 25.0),
        ("身体旋转", "m_angRotation", at(scene, "m_angRotation"), 1.0),
        ("身体旋转", "m_angAbsRotation", at(scene, "m_angAbsRotation"), 1.0),
        ("真无后坐力", "m_aimPunchAngle", at(punch, "m_aimPunchAngle"), 1.0),
        ("真无后坐力", "m_aimPunchVel", at(punch, "m_aimPunchVel"), 1.0),
        ("真无后坐力", "m_vecCsViewPunchAngle", at(camera, "m_vecCsViewPunchAngle"), 1.0),
    ]

    results = {}
    for feature, field, addr, delta in targets:
        verdict, ratio = probe("", proc, addr, delta)
        results.setdefault(feature, []).append((field, verdict, ratio))
        print(f"  {feature:<12} {field:<26} {verdict:<13} held={ratio:.2f}")

    print("\n=== 判定 ===")
    for feature, rows in results.items():
        worst = min((r for _, _, r in rows), default=0.0)
        verdict = "HELD" if worst >= 0.9 else ("PARTIAL" if worst >= 0.2 else "OVERWRITTEN")
        if verdict == "HELD":
            note = "外部写能保持，功能有生效前提"
        elif verdict == "PARTIAL":
            note = "部分 tick 被覆盖，效果不稳定"
        else:
            note = "客户端预测每 tick 覆盖，外部 pwrite 在服务器权威下不生效"
        print(f"  {feature:<12} {verdict:<13} {note}")
    print("\n说明：HELD 只证明「写进去不被改回」，不等于服务器认可；"
          "OVERWRITTEN 则证明继续堆写入目标无效，必须换到 CUserCmd 输入路径。")
    proc.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
