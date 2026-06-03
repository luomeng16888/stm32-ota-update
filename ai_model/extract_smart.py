"""
智能权重提取脚本 - 支持多种提取方式
"""
import re
import os
import sys
import struct

def search_in_directory(directory, pattern):
    """在目录中搜索模式"""
    matches = []
    for root, dirs, files in os.walk(directory):
        for file in files:
            if file.endswith(('.c', '.h')):
                filepath = os.path.join(root, file)
                try:
                    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                        content = f.read()
                        if re.search(pattern, content):
                            matches.append(filepath)
                except:
                    pass
    return matches

def extract_from_tflite(tflite_file, output_bin):
    """从TFLite文件提取权重"""
    try:
        import tensorflow as tf
        import numpy as np
    except ImportError:
        print("需要安装tensorflow: pip install tensorflow")
        return 0

    try:
        # 加载TFLite模型
        interpreter = tf.lite.Interpreter(model_path=tflite_file)
        interpreter.allocate_tensors()

        # 获取所有张量
        tensor_details = interpreter.get_tensor_details()

        weights_data = []

        for detail in tensor_details:
            tensor = interpreter.get_tensor(detail['index'])
            name = detail['name'].lower()

            # 只提取权重相关的张量
            if any(keyword in name for keyword in ['weight', 'kernel', 'bias', 'param']):
                print(f"提取: {detail['name']} - Shape: {tensor.shape} - Dtype: {tensor.dtype}")

                # 展平数组
                flat_tensor = tensor.flatten()

                # 根据数据类型处理
                if tensor.dtype == np.float32:
                    weights_data.extend(flat_tensor.tolist())
                elif tensor.dtype == np.int8:
                    weights_data.extend(flat_tensor.tolist())
                elif tensor.dtype == np.uint8:
                    weights_data.extend(flat_tensor.tolist())
                else:
                    print(f"  跳过未知数据类型: {tensor.dtype}")

        if not weights_data:
            print("未找到权重数据")
            return 0

        # 保存为BIN文件
        with open(output_bin, 'wb') as f:
            # 检查数据类型
            if isinstance(weights_data[0], float):
                # 浮点数权重
                for weight in weights_data:
                    f.write(struct.pack('f', weight))
                print(f"保存了 {len(weights_data)} 个浮点数权重")
            else:
                # 整数权重
                for weight in weights_data:
                    f.write(struct.pack('b', weight))  # int8
                print(f"保存了 {len(weights_data)} 个整数权重")

        return len(weights_data) * 4 if isinstance(weights_data[0], float) else len(weights_data)

    except Exception as e:
        print(f"TFLite提取失败: {e}")
        return 0

def extract_from_c_file(c_file, output_bin):
    """从C文件提取权重"""
    with open(c_file, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()

    # 方法1: 查找数组定义
    patterns = [
        r's_network_weights_array_u64\s*$$[^$$]*\]\s*=\s*\{([^}]+)\}',
        r'static\s+const\s+uint8_t\s+\w*weight\w*\s*$$[^$$]*\]\s*=\s*\{([^}]+)\}',
        r'uint8_t\s+\w*weight\w*\s*$$[^$$]*\]\s*=\s*\{([^}]+)\}',
    ]

    for pattern in patterns:
        matches = re.findall(pattern, content, re.DOTALL)
        if matches:
            print(f"使用模式找到数组: {pattern}")
            array_content = matches[0]

            # 提取数值
            hex_pattern = r'0x([0-9a-fA-F]{2})'
            hex_values = re.findall(hex_pattern, array_content)

            if hex_values:
                data = bytes([int(h, 16) for h in hex_values])
                with open(output_bin, 'wb') as f:
                    f.write(data)
                return len(data)

    # 方法2: 提取所有可能的数值
    hex_pattern = r'0x([0-9a-fA-F]{2})'
    hex_values = re.findall(hex_pattern, content)

    if hex_values:
        # 过滤掉可能的元数据（通常权重值在0-255之间）
        filtered = [int(h, 16) for h in hex_values if 0 <= int(h, 16) <= 255]
        if filtered:
            data = bytes(filtered)
            with open(output_bin, 'wb') as f:
                f.write(data)
            return len(data)

    return 0

if __name__ == "__main__":
    print("=" * 60)
    print("智能权重提取工具")
    print("=" * 60)

    # 创建目录
    os.makedirs("sdcard/model/v1", exist_ok=True)

    # 选项1: 从TFLite提取（推荐）
    print("\n选项1: 从TFLite文件提取（推荐）")
    tflite_files = [
        "model.tflite",
        "v1/model.tflite",
        "output/v1/model.tflite",
        "C:/Users/Administrator/Desktop/model/tran/output/v1/model.tflite"
    ]

    tflite_file = None
    for f in tflite_files:
        if os.path.exists(f):
            tflite_file = f
            break

    if tflite_file:
        print(f"找到TFLite文件: {tflite_file}")
        size = extract_from_tflite(tflite_file, "sdcard/model/v1/model.bin")
        if size > 0:
            print(f"✓ TFLite提取成功: {size} 字节")
        else:
            print("✗ TFLite提取失败，尝试其他方法")
    else:
        print("未找到TFLite文件，尝试从C文件提取")

    # 选项2: 从C文件提取
    if not tflite_file or size == 0:
        print("\n选项2: 从C文件提取")

        # 搜索整个目录
        print("搜索整个目录中的权重定义...")
        matches = search_in_directory(".", r's_network_weights_array_u64')

        if matches:
            print(f"找到包含s_network_weights_array_u64的文件: {matches}")
            for c_file in matches:
                print(f"尝试从 {c_file} 提取...")
                size = extract_from_c_file(c_file, "sdcard/model/v1/model.bin")
                if size > 0:
                    print(f"✓ 从 {c_file} 提取成功: {size} 字节")
                    break
        else:
            print("未找到包含s_network_weights_array_u64的文件")

            # 尝试从network_data.c提取
            if os.path.exists("network_data.c"):
                print("尝试从network_data.c提取...")
                size = extract_from_c_file("network_data.c", "sdcard/model/v1/model.bin")
                if size > 0:
                    print(f"✓ 从network_data.c提取成功: {size} 字节")

    # 检查结果
    bin_file = "sdcard/model/v1/model.bin"
    if os.path.exists(bin_file):
        file_size = os.path.getsize(bin_file)
        print(f"\n最终文件大小: {file_size} 字节")

        # 显示前32字节
        with open(bin_file, "rb") as f:
            first_bytes = f.read(32)
            print(f"前32字节 (十六进制): {first_bytes.hex()}")

            # 尝试解析为浮点数
            if file_size % 4 == 0:
                num_floats = file_size // 4
                floats = struct.unpack(f'{num_floats}f', first_bytes)
                print(f"前8个浮点数: {floats[:8]}")

        if file_size >= 1000:
            print("✓ 文件大小合理，可以用于STM32")
        else:
            print("⚠ 文件大小较小，可能需要检查提取是否完整")

    print("\n" + "=" * 60)
    print("下一步:")
    print("1. 如果提取成功，继续提取V2/V3权重")
    print("2. 如果失败，请提供以下信息:")
    print("   - network_data.c文件的完整路径")
    print("   - 项目中其他.c文件的列表")
    print("   - TFLite文件的完整路径")
    print("=" * 60)
