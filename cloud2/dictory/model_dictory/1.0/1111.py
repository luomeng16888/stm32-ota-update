import os
import hashlib

file_path = "model.bin"
file_size_mb = 5
file_size_bytes = file_size_mb * 1024 * 1024

print(f"正在生成 {file_size_mb}MB 的模型文件，请稍候...")

# 生成固定内容的伪模型文件（可替换为随机数据）
with open(file_path, "wb") as f:
    # 写入特定格式的数据，此处用简单的递增序列模拟实际固件
    for i in range(0, file_size_bytes, 1024):
        chunk = (i // 1024).to_bytes(4, 'big') + (i).to_bytes(8, 'big') + b'\x00\xFF\x55\xAA' + bytes(1008)
        f.write(chunk[:file_size_bytes - i] if file_size_bytes - i < len(chunk) else chunk)

# 计算 MD5
md5 = hashlib.md5()
with open(file_path, "rb") as f:
    for chunk in iter(lambda: f.read(4096), b""):
        md5.update(chunk)

print(f"生成完成！")
print(f"文件路径: {os.path.abspath(file_path)}")
print(f"文件大小: {os.path.getsize(file_path)} 字节")
print(f"MD5 校验值: {md5.hexdigest()}")