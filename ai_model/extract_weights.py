"""
提取 network_data.c 中的权重数组 - 支持多种格式
"""
import re
import os
import struct

def find_and_extract_weights(c_file, out_bin):
    """查找并提取权重数组"""

    with open(c_file, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()

    # 查找权重数组定义
    in_array = False
    array_content = []
    array_name = None

    for i, line in enumerate(lines):
        # 查找数组定义开始
        if 's_network_weights_array_u64' in line and '=' in line and '{' in line:
            in_array = True
            array_name = 's_network_weights_array_u64'
            print(f"找到数组定义在第 {i+1} 行: {line.strip()}")

        # 如果在数组中，收集内容
        if in_array:
            array_content.append(line.strip())

            # 检查数组结束
            if '}' in line and ';' in line:
                in_array = False
                break

    if not array_content:
        print("未找到 s_network_weights_array_u64 数组，尝试其他方法...")

        # 方法2：搜索所有数组定义
        content = ''.join(lines)

        # 查找所有可能的数组定义
        patterns = [
            r's_network_weights_array_u64\s*$$[^$$]*\]\s*=\s*\{[^}]+\}',
            r'static\s+const\s+uint8_t\s+s_network_weights_array_u64\s*$$[^$$]*\]\s*=\s*\{[^}]+\}',
            r'uint8_t\s+s_network_weights_array_u64\s*$$[^$$]*\]\s*=\s*\{[^}]+\}',
        ]

        for pattern in patterns:
            matches = re.findall(pattern, content, re.DOTALL)
            if matches:
                print(f"使用模式 '{pattern}' 找到 {len(matches)} 个匹配")
                array_content = [matches[0]]
                break

        if not array_content:
            print("仍然未找到数组，尝试提取所有可能的数值...")

            # 提取所有十六进制数值
            hex_pattern = r'0x([0-9a-fA-F]{2})'
            hex_values = re.findall(hex_pattern, content)

            if hex_values:
                print(f"找到 {len(hex_values)} 个十六进制值")
                # 过滤掉可能的元数据（如数组大小）
                # 通常权重值在0-255之间
                filtered = []
                for h in hex_values:
                    val = int(h, 16)
                    if 0 <= val <= 255:  # 假设是uint8
                        filtered.append(val)

                if filtered:
                    data = bytes(filtered)
                    print(f"提取到 {len(filtered)} 个有效值，共 {len(data)} 字节")

                    with open(out_bin, "wb") as f:
                        f.write(data)

                    return len(data)

            return 0

    # 处理数组内容
    array_text = ' '.join(array_content)

    # 提取十六进制值
    hex_pattern = r'0x([0-9a-fA-F]{2})'
    hex_values = re.findall(hex_pattern, array_text)

    if hex_values:
        data = bytes([int(h, 16) for h in hex_values])
        print(f"提取到 {len(hex_values)} 个十六进制值，共 {len(data)} 字节")

        # 检查大小是否合理
        if len(data) > 1000:
            print(f"✓ 提取大小合理: {len(data)} 字节")
        else:
            print(f"⚠ 提取大小较小: {len(data)} 字节，可能不完整")
    else:
        # 提取十进制值
        int_pattern = r'(\d+)'
        int_values = re.findall(int_pattern, array_text)

        if int_values:
            # 过滤掉太大的值
            filtered_ints = [int(v) for v in int_values if 0 <= int(v) <= 255]
            data = bytes(filtered_ints)
            print(f"提取到 {len(filtered_ints)} 个十进制值，共 {len(data)} 字节")
        else:
            print("未找到有效的数值")
            return 0

    # 保存文件
    with open(out_bin, "wb") as f:
        f.write(data)

    print(f"保存到: {out_bin}")
    return len(data)

def extract_entire_file(c_file, out_bin):
    """提取整个C文件中的所有字节数据"""
    with open(c_file, "rb") as f:
        content = f.read()

    # 提取所有0x开头的十六进制值
    hex_pattern = rb'0x([0-9a-fA-F]{2})'
    import re
    hex_values = re.findall(hex_pattern, content)

    if hex_values:
        data = bytes([int(h, 16) for h in hex_values])
        print(f"从整个文件提取到 {len(hex_values)} 个十六进制值，共 {len(data)} 字节")

        with open(out_bin, "wb") as f:
            f.write(data)

        return len(data)

    return 0

if __name__ == "__main__":
    # 创建目录
    os.makedirs("sdcard/model/v1", exist_ok=True)

    c_file = "network_data.c"
    if not os.path.exists(c_file):
        print(f"找不到 {c_file}")
        exit(1)

    print("=" * 50)
    print("尝试提取权重数组...")
    print("=" * 50)

    # 方法1：查找特定数组
    size1 = find_and_extract_weights(c_file, "sdcard/model/v1/model.bin")

    if size1 == 0:
        print("\n方法1失败，尝试方法2：提取整个文件...")
        print("=" * 50)

        # 方法2：提取整个文件
        size2 = extract_entire_file(c_file, "sdcard/model/v1/model.bin")

        if size2 > 0:
            print(f"✓ 方法2成功: 提取了 {size2} 字节")
        else:
            print("✗ 两种方法都失败")

    # 检查文件
    bin_file = "sdcard/model/v1/model.bin"
    if os.path.exists(bin_file):
        file_size = os.path.getsize(bin_file)
        print(f"\n最终文件大小: {file_size} 字节")

        # 显示前16字节
        with open(bin_file, "rb") as f:
            first_bytes = f.read(16)
            print(f"前16字节 (十六进制): {first_bytes.hex()}")

        if file_size > 1000:
            print("✓ 文件大小合理，可以用于STM32")
        else:
            print("⚠ 文件大小较小，可能需要检查提取是否完整")
