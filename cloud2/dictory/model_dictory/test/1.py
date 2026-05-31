"""
gen_models.py - 生成测试模型文件（差分 10%~100%）

★ 关键：用连续块替换，bsdiff 可高效压缩，补丁远小于全量 ★

用法：python gen_models.py

生成结果（当前目录 models/）：
  model_base.bin     基准 (512KB)
  model_diff10.bin   10% 不同
  ...
  model_diff100.bin  100% 不同
"""

import os
import struct
import hashlib

SIZE = 512 * 1024  # 512KB


def gen_base(size):
    """
    生成基准数据（有结构的重复模式，模拟真实模型权重）
    """
    data = bytearray(size)

    # 头部
    data[0:4] = b'NEUR'
    struct.pack_into('<I', data, 4, 1)
    struct.pack_into('<I', data, 8, size)
    struct.pack_into('<I', data, 12, 1024)
    struct.pack_into('<I', data, 16, 3)

    # 权重（有规律，非纯随机）
    for i in range(64, size):
        data[i] = ((i * 7 + 13) ^ (i >> 3) ^ 0xA5) & 0xFF

    # 校验和
    ck = sum(data[64:]) & 0xFFFFFFFF
    struct.pack_into('<I', data, 20, ck)

    return bytes(data)


def gen_diff(base, pct):
    """
    ★ 连续块修改 ★

    策略：从文件中间开始，修改连续字节（每个字节 +1）
    bsdiff 高效编码：共同前缀 + 差异块 + 共同后缀
    补丁大小 ≈ 差异大小 + 少量开销（远小于全量）
    """
    if pct <= 0:
        return base

    data = bytearray(base)
    size = len(data)
    body_size = size - 64
    num_changes = int(body_size * pct / 100)

    # 中间连续块
    start = 64 + (body_size - num_changes) // 2

    for i in range(num_changes):
        pos = start + i
        if pos < size:
            data[pos] = (data[pos] + 1) & 0xFF

    # 更新差异标记
    struct.pack_into('<I', data, 24, pct)

    # 重新计算校验和
    ck = sum(data[64:]) & 0xFFFFFFFF
    struct.pack_into('<I', data, 20, ck)

    return bytes(data)


def verify_diff(base, variant):
    """验证实际差异百分比"""
    diff_count = 0
    for i in range(64, len(base)):
        if base[i] != variant[i]:
            diff_count += 1
    return diff_count * 100.0 / (len(base) - 64)


def main():
    os.makedirs("models", exist_ok=True)

    print(f"Size: {SIZE // 1024} KB each")
    print(f"Strategy: continuous block (bsdiff-friendly)\n")

    # 基准
    base = gen_base(SIZE)
    md5 = hashlib.md5(base).hexdigest()[:16]
    with open("models/model_base.bin", "wb") as f:
        f.write(base)
    print(f"  model_base.bin     {len(base):>8d} B   0% diff   md5={md5}")

    # 变体
    for pct in range(10, 110, 10):
        data = gen_diff(base, pct)
        name = f"models/model_diff{pct}.bin"
        md5 = hashlib.md5(data).hexdigest()[:16]
        actual = verify_diff(base, data)

        with open(name, "wb") as f:
            f.write(data)

        print(f"  {os.path.basename(name):20s} {len(data):>8d} B  "
              f"{pct:>3}% diff  actual={actual:.1f}%  md5={md5}")

    # 补丁大小预估
    print(f"\n  Expected bsdiff patch sizes:")
    print(f"  {'Pair':30s} {'Full':>8s} {'Patch':>8s} {'Save':>6s}")
    print(f"  {'-'*55}")

    for pct in range(10, 110, 10):
        # 连续块 → 补丁 ≈ 差异大小 × 1.1
        est = int(SIZE * pct / 100 * 1.1)
        save = round((1 - est / SIZE) * 100, 1)
        print(f"  base → diff{pct:<3d}            {SIZE//1024:>5d}KB "
              f"{est//1024:>5d}KB {save:>5.1f}%")

    print(f"\n  Done: 11 files in {os.path.abspath('models')}/")
    print(f"\n  Next:")
    print(f"  1. Upload all to server (admin/model)")
    print(f"  2. Publish versions")
    print(f"  3. Generate patches (admin/push)")
    print(f"  4. Patches will be small (uncompressed, STM32-compatible)")


if __name__ == '__main__':
    main()
