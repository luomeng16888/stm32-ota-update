import re, struct, os, shutil

BASE = r"C:\Users\Administrator\Desktop\ai_model"

# 每个版本的输出量化参数 (从tflite分析得到)
OUTPUT_PARAMS = {
    'v1': {'scale': 0.0026141035, 'zp': 127},
    'v2': {'scale': 0.0035798375, 'zp': 127},
    'v3': {'scale': 0.0037703458, 'zp': 127},
}

def extract(c_file, out_bin, version):
    with open(c_file, "r", encoding="utf-8", errors="ignore") as f:
        c = f.read()
    print(f"  输入: {c_file} ({len(c)} bytes)")
    vals = re.findall(r'0x([0-9a-fA-F]{8,16})U', c)
    if not vals:
        print(f"  ERROR: 没找到 u64 数据")
        return False

    weights = bytearray()
    for v in vals:
        weights.extend(struct.pack('<Q', int(v, 16)))

    # 写入: header(8字节) + 权重
    params = OUTPUT_PARAMS[version]
    header = struct.pack('<fi', params['scale'], params['zp'])

    os.makedirs(os.path.dirname(out_bin), exist_ok=True)
    with open(out_bin, "wb") as f:
        f.write(header)
        f.write(weights)

    print(f"  OK: header(8B) + {len(vals)} u64({len(weights)}B) = {len(header)+len(weights)} bytes -> {out_bin}")
    print(f"       output_scale={params['scale']:.10f}, zp={params['zp']}")
    return True

def main():
    out = os.path.join(BASE, "sdcard")
    os.makedirs(f"{out}/OTA/data", exist_ok=True)
    os.makedirs(f"{out}/OTA/model", exist_ok=True)

    for ver in ['v1', 'v2', 'v3']:
        print("=" * 50)
        print(f"{ver.upper()} 权重")
        print("=" * 50)
        params_file = os.path.join(BASE, f"{ver}_params.c", "network_data_params.c")
        bin_file = f"{out}/OTA/model/{ver}/model.bin"
        extract(params_file, bin_file, ver)

    # 复制数据集
    print("=" * 50)
    print("数据集")
    print("=" * 50)
    ecg = os.path.join(BASE, "ecg.bin")
    if os.path.exists(ecg):
        shutil.copy2(ecg, f"{out}/OTA/data/ecg.bin")
        print(f"  OK: ecg.bin ({os.path.getsize(ecg)} bytes)")

    # SD卡初始内容（V1）
    print("=" * 50)
    print("SD卡初始模型")
    print("=" * 50)
    v1_bin = f"{out}/OTA/model/v1/model.bin"
    if os.path.exists(v1_bin):
        shutil.copy2(v1_bin, f"{out}/OTA/model/model.bin")
        print(f"  OK: model.bin ({os.path.getsize(v1_bin)} bytes)")

    print()
    print("=" * 50)
    print("输出文件:")
    print("=" * 50)
    for root, dirs, files in os.walk(out):
        for f in files:
            fp = os.path.join(root, f)
            rel = os.path.relpath(fp, out)
            print(f"  {rel}  ({os.path.getsize(fp)} bytes)")

if __name__ == "__main__":
    main()
