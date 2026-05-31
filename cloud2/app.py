"""
app.py - OTA Server v3.5
补丁格式: SDIFF01 (简单差分, 仅记录变化区域)
传输方式: 透明模式 JSON+hex 分块下载
STM32 端无需 bz2/bsdiff4 库, 无动态内存分配
"""

import os
import json
import hashlib
import struct
import time
from datetime import datetime, timezone
from functools import wraps

from flask import (
    Flask, render_template, request, redirect, url_for,
    flash, jsonify, abort, Response
)
from flask_sqlalchemy import SQLAlchemy
from flask_login import (
    LoginManager, UserMixin, login_user, logout_user,
    login_required, current_user
)
from werkzeug.security import generate_password_hash, check_password_hash
from werkzeug.utils import secure_filename

# ============================================================
# App Configuration
# ============================================================

app = Flask(__name__)
app.config['SECRET_KEY'] = 'ota-secret-key-change-me'
app.config['SQLALCHEMY_DATABASE_URI'] = 'sqlite:///ota.db'
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False
app.config['UPLOAD_FOLDER'] = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), 'uploads'
)
app.config['MAX_CONTENT_LENGTH'] = 200 * 1024 * 1024

db = SQLAlchemy(app)

login_manager = LoginManager(app)
login_manager.login_view = 'login'
login_manager.login_message = '请先登录以访问该页面'
login_manager.login_message_category = 'info'

for _d in ('firmware', 'models', 'patches'):
    os.makedirs(os.path.join(app.config['UPLOAD_FOLDER'], _d), exist_ok=True)

DEBUG_OTA = True


@app.after_request
def set_connection_close(response):
    # OTA 接口保持 keep-alive（透明模式需要）
    if not request.path.startswith('/api/ota/'):
        response.headers['Connection'] = 'close'
    response.headers.pop('Keep-Alive', None)
    return response


def utcnow():
    return datetime.now(timezone.utc).replace(tzinfo=None)


def log_event(level, module, message, device_id='', ip_address=''):
    try:
        entry = ServerLog(
            level=level, module=module, message=message,
            device_id=device_id, ip_address=ip_address
        )
        db.session.add(entry)
        db.session.commit()
    except Exception:
        db.session.rollback()


def ota_debug(device_id, message, ip_address=''):
    ts = datetime.now().strftime('%H:%M:%S.%f')[:-3]
    print(f"[OTA-DBG {ts}] {device_id}: {message}")
    if DEBUG_OTA:
        log_event('DEBUG', 'OTA', f'{device_id} | {message}',
                  device_id=device_id, ip_address=ip_address)


# ============================================================
# SDIFF01 生成函数
# ============================================================

def generate_simple_diff(old_path, new_path):
    """
    SDIFF01 格式:
      "SDIFF01\0"  8B  magic
      new_size     4B  uint32 LE
      count        4B  uint32 LE
      [offset(4B) + len(4B) + data(len B)] * count
    """
    with open(old_path, 'rb') as f:
        old_data = f.read()
    with open(new_path, 'rb') as f:
        new_data = f.read()

    old_len = len(old_data)
    new_len = len(new_data)
    changes = []

    i = 0
    n = min(old_len, new_len)

    while i < n:
        if old_data[i] != new_data[i]:
            start = i
            # 向后扫描连续差异 (限制单块最大 4KB)
            while i < n and old_data[i] != new_data[i] and (i - start) < 4096:
                i += 1
            changes.append((start, new_data[start:i]))
        else:
            i += 1

    # 处理文件大小不同 (新文件更长的部分)
    if new_len > old_len:
        off = old_len
        while off < new_len:
            end = min(off + 4096, new_len)
            changes.append((off, new_data[off:end]))
            off = end

    # 构造二进制补丁
    result = bytearray()
    result += b'SDIFF01\x00'
    result += struct.pack('<I', new_len)
    result += struct.pack('<I', len(changes))
    for offset, data in changes:
        result += struct.pack('<I', offset)
        result += struct.pack('<I', len(data))
        result += data

    return bytes(result)


# ============================================================
# Database Models
# ============================================================

class User(UserMixin, db.Model):
    id = db.Column(db.Integer, primary_key=True)
    username = db.Column(db.String(80), unique=True, nullable=False)
    password_hash = db.Column(db.String(128), nullable=False)
    email = db.Column(db.String(120), default='')
    role = db.Column(db.String(10), default='user')
    created_at = db.Column(db.DateTime, default=utcnow)
    last_login = db.Column(db.DateTime)
    devices = db.relationship('Device', backref='owner', lazy=True)
    feedbacks = db.relationship('Feedback', backref='user', lazy=True)

    def set_password(self, password):
        self.password_hash = generate_password_hash(password)

    def check_password(self, password):
        return check_password_hash(self.password_hash, password)


class Device(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    device_id = db.Column(db.String(64), unique=True, nullable=False)
    device_model = db.Column(db.String(32), default='STM32F407ZGT6')
    user_id = db.Column(db.Integer, db.ForeignKey('user.id'), nullable=True)
    current_sys_version = db.Column(db.String(16), default='1.0.0')
    current_model_version = db.Column(db.String(16), default='0.0')
    wifi_ssid = db.Column(db.String(64), default='')
    wifi_password = db.Column(db.String(128), default='')
    is_online = db.Column(db.Boolean, default=False)
    last_seen = db.Column(db.DateTime)
    ip_address = db.Column(db.String(45), default='')
    created_at = db.Column(db.DateTime, default=utcnow)
    updated_at = db.Column(db.DateTime, default=utcnow, onupdate=utcnow)


class Firmware(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    version = db.Column(db.String(16), unique=True, nullable=False)
    device_model = db.Column(db.String(32), default='STM32F407ZGT6')
    filename = db.Column(db.String(256), nullable=False)
    file_path = db.Column(db.String(512), nullable=False)
    file_size = db.Column(db.Integer, default=0)
    file_md5 = db.Column(db.String(32), default='')
    release_notes = db.Column(db.Text, default='')
    is_active = db.Column(db.Boolean, default=False)
    uploaded_by = db.Column(db.Integer, db.ForeignKey('user.id'))
    created_at = db.Column(db.DateTime, default=utcnow)
    published_at = db.Column(db.DateTime)


class ModelVersion(db.Model):
    __tablename__ = 'model_version'
    id = db.Column(db.Integer, primary_key=True)
    version = db.Column(db.String(16), nullable=False)
    device_model = db.Column(db.String(32), default='STM32F407ZGT6')
    model_name = db.Column(db.String(64), default='')
    filename = db.Column(db.String(256), nullable=False)
    file_path = db.Column(db.String(512), nullable=False)
    file_size = db.Column(db.Integer, default=0)
    file_md5 = db.Column(db.String(32), default='')
    description = db.Column(db.Text, default='')
    is_active = db.Column(db.Boolean, default=False)
    uploaded_by = db.Column(db.Integer, db.ForeignKey('user.id'))
    created_at = db.Column(db.DateTime, default=utcnow)
    published_at = db.Column(db.DateTime)


class Patch(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    patch_type = db.Column(db.String(16), nullable=False)
    from_version = db.Column(db.String(16), nullable=False)
    to_version = db.Column(db.String(16), nullable=False)
    device_model = db.Column(db.String(32), default='')
    filename = db.Column(db.String(256), nullable=False)
    file_path = db.Column(db.String(512), nullable=False)
    file_size = db.Column(db.Integer, default=0)
    file_md5 = db.Column(db.String(32), default='')
    created_at = db.Column(db.DateTime, default=utcnow)


class PushRecord(db.Model):
    __tablename__ = 'push_record'
    id = db.Column(db.Integer, primary_key=True)
    target_device_id = db.Column(db.Integer, db.ForeignKey('device.id'))
    push_type = db.Column(db.String(16), nullable=False)
    version = db.Column(db.String(16), nullable=False)
    update_mode = db.Column(db.String(16), default='full')
    status = db.Column(db.String(16), default='pending')
    progress = db.Column(db.Integer, default=0)
    pushed_by = db.Column(db.Integer, db.ForeignKey('user.id'))
    created_at = db.Column(db.DateTime, default=utcnow)
    completed_at = db.Column(db.DateTime)
    device = db.relationship('Device', backref='push_records')


class Feedback(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    user_id = db.Column(db.Integer, db.ForeignKey('user.id'), nullable=False)
    device_id = db.Column(db.Integer, db.ForeignKey('device.id'), nullable=True)
    subject = db.Column(db.String(200), nullable=False)
    content = db.Column(db.Text, nullable=False)
    status = db.Column(db.String(16), default='unread')
    reply = db.Column(db.Text, default='')
    created_at = db.Column(db.DateTime, default=utcnow)
    replied_at = db.Column(db.DateTime)
    device = db.relationship('Device', backref='feedbacks')


class ServerLog(db.Model):
    __tablename__ = 'server_log'
    id = db.Column(db.Integer, primary_key=True)
    level = db.Column(db.String(10), default='INFO')
    module = db.Column(db.String(50), default='')
    message = db.Column(db.Text, nullable=False)
    device_id = db.Column(db.String(64), default='')
    ip_address = db.Column(db.String(45), default='')
    created_at = db.Column(db.DateTime, default=utcnow)


# ============================================================
# Login Manager
# ============================================================

@login_manager.user_loader
def load_user(user_id):
    return db.session.get(User, int(user_id))


def admin_required(f):
    @wraps(f)
    def decorated(*args, **kwargs):
        if not current_user.is_authenticated:
            return redirect(url_for('login'))
        if current_user.role != 'admin':
            abort(403)
        return f(*args, **kwargs)
    return decorated


# ============================================================
# Auth Routes
# ============================================================

@app.route('/login', methods=['GET', 'POST'])
def login():
    if current_user.is_authenticated:
        return redirect(url_for(
            'admin_dashboard' if current_user.role == 'admin' else 'user_dashboard'))
    if request.method == 'POST':
        username = request.form.get('username', '')
        password = request.form.get('password', '')
        user = User.query.filter_by(username=username).first()
        if user and user.check_password(password):
            login_user(user)
            user.last_login = utcnow()
            db.session.commit()
            log_event('INFO', 'Auth', f'Login: {username}',
                      ip_address=request.remote_addr)
            return redirect(url_for(
                'admin_dashboard' if user.role == 'admin' else 'user_dashboard'))
        flash('用户名或密码错误')
        log_event('WARNING', 'Auth', f'Failed login: {username}',
                  ip_address=request.remote_addr)
    return render_template('login.html')


@app.route('/register', methods=['GET', 'POST'])
def register():
    if current_user.is_authenticated:
        return redirect(url_for('index'))
    if request.method == 'POST':
        username = request.form.get('username', '').strip()
        password = request.form.get('password', '')
        email = request.form.get('email', '').strip()
        if not username or not password:
            flash('请填写用户名和密码')
        elif User.query.filter_by(username=username).first():
            flash('用户名已存在')
        else:
            user = User(username=username, email=email, role='user')
            user.set_password(password)
            db.session.add(user)
            db.session.commit()
            log_event('INFO', 'Auth', f'Registered: {username}')
            flash('注册成功，请登录')
            return redirect(url_for('login'))
    return render_template('register.html')


@app.route('/logout')
@login_required
def logout():
    log_event('INFO', 'Auth', f'Logout: {current_user.username}')
    logout_user()
    flash('已退出登录')
    return redirect(url_for('login'))


@app.route('/')
def index():
    if not current_user.is_authenticated:
        return redirect(url_for('login'))
    return redirect(url_for(
        'admin_dashboard' if current_user.role == 'admin' else 'user_dashboard'))


# ============================================================
# Admin - Dashboard
# ============================================================

@app.route('/admin')
@admin_required
def admin_dashboard():
    return render_template('admin_dashboard.html',
        users=User.query.all(),
        devices=Device.query.all(),
        firmwares=Firmware.query.order_by(Firmware.created_at.desc()).all(),
        models=ModelVersion.query.order_by(ModelVersion.created_at.desc()).all(),
        unread_feedbacks=Feedback.query.filter_by(status='unread').count(),
        online_devices=Device.query.filter_by(is_online=True).count(),
        total_devices=Device.query.count(),
        total_users=User.query.count(),
        recent_logs=ServerLog.query.order_by(
            ServerLog.created_at.desc()).limit(5).all())


@app.route('/admin/users')
@admin_required
def admin_users():
    return render_template('admin_users.html',
        users=User.query.all(),
        devices=Device.query.all(),
        unread_feedbacks=Feedback.query.filter_by(status='unread').count())


@app.route('/admin/firmware')
@admin_required
def admin_firmware():
    return render_template('admin_firmware.html',
        firmwares=Firmware.query.order_by(Firmware.created_at.desc()).all(),
        unread_feedbacks=Feedback.query.filter_by(status='unread').count())


@app.route('/admin/firmware/upload', methods=['POST'])
@admin_required
def admin_firmware_upload():
    file = request.files.get('firmware')
    version = request.form.get('version', '').strip()
    device_model = request.form.get('device_model', 'STM32F407ZGT6').strip()
    notes = request.form.get('release_notes', '').strip()
    if not file or not version:
        flash('需要版本号和文件')
        return redirect(url_for('admin_firmware'))
    if Firmware.query.filter_by(version=version).first():
        flash(f'版本 {version} 已存在')
        return redirect(url_for('admin_firmware'))
    filename = secure_filename(file.filename)
    file_path = os.path.join(app.config['UPLOAD_FOLDER'], 'firmware',
                             f'{version}_{filename}')
    file.save(file_path)
    file_size = os.path.getsize(file_path)
    file_md5 = hashlib.md5(open(file_path, 'rb').read()).hexdigest()
    fw = Firmware(
        version=version, device_model=device_model, filename=filename,
        file_path=file_path, file_size=file_size, file_md5=file_md5,
        release_notes=notes, uploaded_by=current_user.id)
    db.session.add(fw)
    db.session.commit()
    log_event('INFO', 'Firmware', f'Uploaded v{version} ({file_size}B)')
    flash(f'固件 v{version} 已上传 ({file_size} bytes)')
    return redirect(url_for('admin_firmware'))


@app.route('/admin/firmware/publish/<int:fw_id>', methods=['POST'])
@admin_required
def admin_firmware_publish(fw_id):
    fw = db.session.get(Firmware, fw_id)
    if fw:
        fw.is_active = True
        fw.published_at = utcnow()
        db.session.commit()
        log_event('INFO', 'Firmware', f'Published v{fw.version}')
    return redirect(url_for('admin_firmware'))


@app.route('/admin/firmware/unpublish/<int:fw_id>', methods=['POST'])
@admin_required
def admin_firmware_unpublish(fw_id):
    fw = db.session.get(Firmware, fw_id)
    if fw:
        fw.is_active = False
        fw.published_at = None
        db.session.commit()
    return redirect(url_for('admin_firmware'))


@app.route('/admin/firmware/delete/<int:fw_id>', methods=['POST'])
@admin_required
def admin_firmware_delete(fw_id):
    fw = db.session.get(Firmware, fw_id)
    if fw:
        if os.path.exists(fw.file_path):
            os.remove(fw.file_path)
        Patch.query.filter_by(patch_type='firmware').filter(
            (Patch.from_version == fw.version) |
            (Patch.to_version == fw.version)).delete()
        db.session.delete(fw)
        db.session.commit()
    return redirect(url_for('admin_firmware'))


@app.route('/admin/model')
@admin_required
def admin_model():
    return render_template('admin_model.html',
        models=ModelVersion.query.order_by(ModelVersion.created_at.desc()).all(),
        unread_feedbacks=Feedback.query.filter_by(status='unread').count())


@app.route('/admin/model/upload', methods=['POST'])
@admin_required
def admin_model_upload():
    file = request.files.get('model_file')
    version = request.form.get('version', '').strip()
    model_name = request.form.get('model_name', '').strip()
    device_model = request.form.get('device_model', 'STM32F407ZGT6').strip()
    description = request.form.get('description', '').strip()
    if not file or not version:
        flash('需要版本号和文件')
        return redirect(url_for('admin_model'))
    filename = secure_filename(file.filename)
    file_path = os.path.join(app.config['UPLOAD_FOLDER'], 'models',
                             f'{version}_{filename}')
    file.save(file_path)
    file_size = os.path.getsize(file_path)
    file_md5 = hashlib.md5(open(file_path, 'rb').read()).hexdigest()
    mv = ModelVersion(
        version=version, device_model=device_model, model_name=model_name,
        filename=filename, file_path=file_path, file_size=file_size,
        file_md5=file_md5, description=description,
        uploaded_by=current_user.id)
    db.session.add(mv)
    db.session.commit()
    log_event('INFO', 'Model', f'Uploaded v{version} ({file_size}B)')
    flash(f'模型 v{version} 已上传 ({file_size} bytes)')
    return redirect(url_for('admin_model'))


@app.route('/admin/model/publish/<int:model_id>', methods=['POST'])
@admin_required
def admin_model_publish(model_id):
    mv = db.session.get(ModelVersion, model_id)
    if mv:
        mv.is_active = True
        mv.published_at = utcnow()
        db.session.commit()
    return redirect(url_for('admin_model'))


@app.route('/admin/model/unpublish/<int:model_id>', methods=['POST'])
@admin_required
def admin_model_unpublish(model_id):
    mv = db.session.get(ModelVersion, model_id)
    if mv:
        mv.is_active = False
        mv.published_at = None
        db.session.commit()
    return redirect(url_for('admin_model'))


@app.route('/admin/model/delete/<int:model_id>', methods=['POST'])
@admin_required
def admin_model_delete(model_id):
    mv = db.session.get(ModelVersion, model_id)
    if mv:
        if os.path.exists(mv.file_path):
            os.remove(mv.file_path)
        Patch.query.filter_by(patch_type='model').filter(
            (Patch.from_version == mv.version) |
            (Patch.to_version == mv.version)).delete()
        db.session.delete(mv)
        db.session.commit()
    return redirect(url_for('admin_model'))


@app.route('/admin/push')
@admin_required
def admin_push():
    return render_template('admin_push.html',
        devices=Device.query.all(),
        firmwares=Firmware.query.filter_by(is_active=True).all(),
        models=ModelVersion.query.filter_by(is_active=True).all(),
        patches=Patch.query.all(),
        push_history=PushRecord.query.order_by(
            PushRecord.created_at.desc()).limit(50).all(),
        unread_feedbacks=Feedback.query.filter_by(status='unread').count())


@app.route('/admin/push/send', methods=['POST'])
@admin_required
def admin_push_send():
    device_ids = request.form.getlist('device_ids')
    push_type = request.form.get('push_type', 'firmware')
    version = request.form.get('version', '')
    update_mode = request.form.get('update_mode', 'full')
    if not device_ids or not version:
        flash('请选择设备和版本')
        return redirect(url_for('admin_push'))
    count = 0
    for did in device_ids:
        device = db.session.get(Device, int(did))
        if device:
            db.session.add(PushRecord(
                target_device_id=device.id, push_type=push_type,
                version=version, update_mode=update_mode,
                pushed_by=current_user.id))
            count += 1
    db.session.commit()
    flash(f'已推送 {push_type} v{version} 到 {count} 个设备')
    return redirect(url_for('admin_push'))


@app.route('/admin/patch/delete/<int:patch_id>', methods=['POST'])
@admin_required
def admin_patch_delete(patch_id):
    p = db.session.get(Patch, patch_id)
    if p:
        if os.path.exists(p.file_path):
            os.remove(p.file_path)
        db.session.delete(p)
        db.session.commit()
    return redirect(url_for('admin_push'))


# ============================================================
# ★ 补丁生成 - SDIFF01 格式 (简单差分)
# ★★★ 无需 bsdiff4 库, 纯 Python 实现 ★★★
# ============================================================

@app.route('/admin/patch/generate', methods=['POST'])
@admin_required
def admin_patch_generate():
    patch_type = request.form.get('patch_type', 'firmware')
    from_ver = request.form.get('from_version', '')
    to_ver = request.form.get('to_version', '')

    if not from_ver or not to_ver:
        flash('请选择版本')
        return redirect(url_for('admin_push'))

    if patch_type == 'firmware':
        from_item = Firmware.query.filter_by(version=from_ver).first()
        to_item = Firmware.query.filter_by(version=to_ver).first()
    else:
        from_item = ModelVersion.query.filter_by(version=from_ver).first()
        to_item = ModelVersion.query.filter_by(version=to_ver).first()

    if not from_item or not to_item:
        flash('版本未找到')
        return redirect(url_for('admin_push'))

    # 自动删除旧补丁记录和文件
    old_patch = Patch.query.filter_by(
        patch_type=patch_type, from_version=from_ver,
        to_version=to_ver).first()
    if old_patch:
        print(f"[PATCH] Removing old patch (id={old_patch.id}, "
              f"size={old_patch.file_size}B)")
        if os.path.exists(old_patch.file_path):
            os.remove(old_patch.file_path)
        db.session.delete(old_patch)
        db.session.commit()

    patch_dir = os.path.join(app.config['UPLOAD_FOLDER'], 'patches')
    os.makedirs(patch_dir, exist_ok=True)
    patch_filename = f'{patch_type}_{from_ver}_to_{to_ver}.sdiff'
    patch_path = os.path.join(patch_dir, patch_filename)

    try:
        print(f"\n{'='*60}")
        print(f"[PATCH] Generating SDIFF01: {from_ver} -> {to_ver}")
        print(f"[PATCH] Old: {from_item.file_path} ({from_item.file_size}B)")
        print(f"[PATCH] New: {to_item.file_path} ({to_item.file_size}B)")

        # ★ 生成简单差分
        diff_data = generate_simple_diff(from_item.file_path, to_item.file_path)

        with open(patch_path, 'wb') as f:
            f.write(diff_data)

        # 验证魔数
        with open(patch_path, 'rb') as f:
            verify = f.read(8)
        if verify != b'SDIFF01\x00':
            raise ValueError(f"Verification failed: {verify}")

        print(f"[PATCH] Verified: SDIFF01 magic OK")

    except Exception as e:
        if os.path.exists(patch_path):
            os.remove(patch_path)
        flash(f'补丁生成失败: {e}')
        import traceback
        traceback.print_exc()
        return redirect(url_for('admin_push'))

    # 读取变更块数
    change_count = 0
    try:
        with open(patch_path, 'rb') as f:
            f.seek(12)
            change_count = struct.unpack('<I', f.read(4))[0]
    except Exception:
        pass

    # 记录到数据库
    patch_size = os.path.getsize(patch_path)
    patch_md5 = hashlib.md5(open(patch_path, 'rb').read()).hexdigest()

    db.session.add(Patch(
        patch_type=patch_type, from_version=from_ver,
        to_version=to_ver, device_model=to_item.device_model,
        filename=patch_filename, file_path=patch_path,
        file_size=patch_size, file_md5=patch_md5))
    db.session.commit()

    savings = round(
        (1 - patch_size / max(to_item.file_size, 1)) * 100, 1)
    print(f"[PATCH] Final: {patch_size}B, {change_count} changes "
          f"(save {savings}%)")
    print(f"[PATCH] File: {patch_path}")
    print(f"{'='*60}\n")

    log_event('INFO', 'Patch',
              f'{from_ver}->{to_ver} ({patch_size}B, {change_count} changes)')
    flash(f'补丁: {from_ver}->{to_ver} '
          f'({patch_size}B / {change_count}块, 节省{savings}%)')
    return redirect(url_for('admin_push'))


# ============================================================
# Admin - Feedback / Logs
# ============================================================

@app.route('/admin/feedback')
@admin_required
def admin_feedback():
    return render_template('admin_feedback.html',
        feedbacks=Feedback.query.order_by(Feedback.created_at.desc()).all(),
        unread_feedbacks=Feedback.query.filter_by(status='unread').count())


@app.route('/admin/feedback/reply/<int:fb_id>', methods=['POST'])
@admin_required
def admin_feedback_reply(fb_id):
    fb = db.session.get(Feedback, fb_id)
    if fb:
        fb.reply = request.form.get('reply', '')
        fb.status = 'replied'
        fb.replied_at = utcnow()
        db.session.commit()
        flash('回复已发送')
    return redirect(url_for('admin_feedback'))


@app.route('/admin/logs')
@admin_required
def admin_logs():
    level_filter = request.args.get('level', '')
    query = ServerLog.query.order_by(ServerLog.created_at.desc())
    if level_filter:
        query = query.filter_by(level=level_filter)
    return render_template('admin_logs.html',
        logs=query.limit(200).all(),
        level_filter=level_filter,
        unread_feedbacks=Feedback.query.filter_by(status='unread').count())


# ============================================================
# User Routes
# ============================================================

@app.route('/user')
@login_required
def user_dashboard():
    if current_user.role == 'admin':
        return redirect(url_for('admin_dashboard'))
    return render_template('user_dashboard.html',
        devices=Device.query.filter_by(user_id=current_user.id).all(),
        firmwares=Firmware.query.filter_by(is_active=True).order_by(
            Firmware.created_at.desc()).all(),
        models=ModelVersion.query.filter_by(is_active=True).order_by(
            ModelVersion.created_at.desc()).all(),
        latest_firmware=Firmware.query.filter_by(is_active=True).order_by(
            Firmware.created_at.desc()).first(),
        latest_model=ModelVersion.query.filter_by(is_active=True).order_by(
            ModelVersion.created_at.desc()).first())


@app.route('/user/device/add', methods=['GET', 'POST'])
@login_required
def user_device_add():
    if request.method == 'POST':
        device_id = request.form.get('device_id', '').strip()
        device_model = request.form.get('device_model', 'STM32F407ZGT6').strip()
        wifi_ssid = request.form.get('wifi_ssid', '').strip()
        wifi_password = request.form.get('wifi_password', '').strip()
        if not device_id:
            flash('需要设备ID')
        elif Device.query.filter_by(device_id=device_id).first():
            flash('设备已注册')
        else:
            device = Device(
                device_id=device_id, device_model=device_model,
                user_id=current_user.id, wifi_ssid=wifi_ssid,
                wifi_password=wifi_password)
            db.session.add(device)
            db.session.commit()
            log_event('INFO', 'Device',
                      f'{current_user.username} added {device_id}')
            flash(f'设备 {device_id} 已添加')
            return redirect(url_for('user_dashboard'))
    return render_template('user_device_add.html')


@app.route('/user/device/<int:dev_id>/wifi', methods=['GET', 'POST'])
@login_required
def user_device_wifi(dev_id):
    device = db.session.get(Device, dev_id)
    if not device or device.user_id != current_user.id:
        abort(403)
    if request.method == 'POST':
        device.wifi_ssid = request.form.get('wifi_ssid', '').strip()
        device.wifi_password = request.form.get('wifi_password', '').strip()
        db.session.commit()
        log_event('INFO', 'Device', f'WiFi updated: {device.device_id}')
        flash('WiFi 配置已更新')
        return redirect(url_for('user_dashboard'))
    return render_template('user_device_wifi.html', device=device)


@app.route('/user/device/delete/<int:dev_id>', methods=['POST'])
@login_required
def user_device_delete(dev_id):
    device = db.session.get(Device, dev_id)
    if device and device.user_id == current_user.id:
        log_event('INFO', 'Device',
                  f'{current_user.username} deleted {device.device_id}')
        db.session.delete(device)
        db.session.commit()
        flash('设备已删除')
    return redirect(url_for('user_dashboard'))


@app.route('/user/feedback', methods=['GET', 'POST'])
@login_required
def user_feedback():
    if request.method == 'POST':
        subject = request.form.get('subject', '').strip()
        content = request.form.get('content', '').strip()
        device_id = request.form.get('device_id') or None
        if subject and content:
            db.session.add(Feedback(
                user_id=current_user.id, device_id=device_id,
                subject=subject, content=content))
            db.session.commit()
            flash('反馈已提交')
            return redirect(url_for('user_feedback_list'))
    return render_template('user_feedback_new.html',
        devices=Device.query.filter_by(user_id=current_user.id).all())


@app.route('/user/feedback/list')
@login_required
def user_feedback_list():
    return render_template('user_feedback.html',
        feedbacks=Feedback.query.filter_by(user_id=current_user.id)
            .order_by(Feedback.created_at.desc()).all())


# ============================================================
# Dashboard
# ============================================================

@app.route('/dashboard')
@login_required
def dashboard_page():
    return render_template('dashboard.html')


@app.route('/api/dashboard/stats')
@login_required
def dash_stats():
    active_fw = Firmware.query.filter_by(is_active=True).order_by(
        Firmware.created_at.desc()).first()
    return jsonify({
        'total': Device.query.count(),
        'online': Device.query.filter_by(is_online=True).count(),
        'fwCount': Firmware.query.count(),
        'active': active_fw.version if active_fw else '---'})


@app.route('/api/dashboard/devices')
@login_required
def dash_devices():
    if current_user.role == 'admin':
        devices = Device.query.all()
    else:
        devices = Device.query.filter_by(user_id=current_user.id).all()
    return jsonify([{
        'id': d.device_id,
        'fw': d.current_sys_version or '0.0',
        'model': d.device_model or '',
        'partition': d.current_sys_version or '?',
        'online': d.is_online,
        'last_seen': d.last_seen.isoformat() if d.last_seen else None,
        'ip': d.ip_address or ''
    } for d in devices])


@app.route('/api/dashboard/firmware', methods=['GET'])
@login_required
def dash_firmware_list():
    fws = Firmware.query.order_by(Firmware.created_at.desc()).all()
    active = Firmware.query.filter_by(is_active=True).order_by(
        Firmware.created_at.desc()).first()
    return jsonify({
        'list': [{
            'version': f.version, 'size': f.file_size,
            'md5': f.file_md5 or '',
            'time': f.created_at.isoformat() if f.created_at else None
        } for f in fws],
        'active': active.version if active else ''})


@app.route('/api/dashboard/upload', methods=['POST'])
@login_required
def dash_upload():
    version = request.form.get('version', '').strip()
    file = request.files.get('firmware')
    if not version or not file:
        return jsonify({'ok': False, 'error': '需要版本号和文件'})
    if Firmware.query.filter_by(version=version).first():
        return jsonify({'ok': False, 'error': f'版本 {version} 已存在'})
    filename = secure_filename(file.filename) or f'fw_{version}.bin'
    file_path = os.path.join(app.config['UPLOAD_FOLDER'], 'firmware',
                             f'{version}_{filename}')
    file.save(file_path)
    file_size = os.path.getsize(file_path)
    file_md5 = hashlib.md5(open(file_path, 'rb').read()).hexdigest()
    db.session.add(Firmware(
        version=version, filename=filename, file_path=file_path,
        file_size=file_size, file_md5=file_md5,
        uploaded_by=current_user.id))
    db.session.commit()
    return jsonify({'ok': True})


@app.route('/api/dashboard/activate', methods=['POST'])
@login_required
def dash_activate():
    data = request.get_json(force=True)
    fw = Firmware.query.filter_by(version=data.get('version', '')).first()
    if not fw:
        return jsonify({'ok': False, 'error': '版本不存在'})
    fw.is_active = True
    fw.published_at = utcnow()
    db.session.commit()
    return jsonify({'ok': True, 'active': fw.version})


@app.route('/api/dashboard/firmware', methods=['DELETE'])
@login_required
def dash_firmware_delete():
    data = request.get_json(force=True)
    fw = Firmware.query.filter_by(version=data.get('version', '')).first()
    if not fw:
        return jsonify({'ok': False, 'error': '版本不存在'})
    if os.path.exists(fw.file_path):
        os.remove(fw.file_path)
    db.session.delete(fw)
    db.session.commit()
    return jsonify({'ok': True})


@app.route('/api/dashboard/log')
@login_required
def dash_log():
    logs = ServerLog.query.order_by(
        ServerLog.created_at.desc()).limit(100).all()
    result = []
    for l in logs:
        lvl = (l.level or 'INFO').lower()
        if lvl in ('error', 'critical'):
            lvl = 'error'
        elif lvl == 'warning':
            lvl = 'warning'
        else:
            lvl = 'info'
        result.append({
            't': l.created_at.isoformat() if l.created_at else '',
            'type': lvl,
            'msg': l.message or ''})
    return jsonify(result)


# ============================================================
# OTA Device API
# ============================================================

@app.route('/api/ota/register', methods=['POST'])
def ota_register():
    t0 = time.time()
    client_ip = request.remote_addr
    try:
        data = request.get_json(force=True)
    except Exception:
        return jsonify({"code": 400, "message": "Invalid JSON"})

    device_id = data.get('device_id', '')
    ota_debug(device_id,
              f'REGISTER: model={data.get("device_model","?")} '
              f'ip={client_ip}', client_ip)

    if not device_id:
        return jsonify({"code": 400, "message": "Missing device_id"})

    device = Device.query.filter_by(device_id=device_id).first()
    if not device:
        device = Device(
            device_id=device_id,
            device_model=data.get('device_model', 'STM32F407ZGT6'),
            last_seen=utcnow(), ip_address=client_ip)
        db.session.add(device)
    device.is_online = True
    device.last_seen = utcnow()
    device.ip_address = client_ip
    db.session.commit()

    dt = (time.time() - t0) * 1000
    ota_debug(device_id, f'REGISTER ok ({dt:.0f}ms)')
    log_event('INFO', 'OTA', f'Device online: {device_id}',
              device_id=device_id, ip_address=client_ip)
    return jsonify({
        "code": 200, "message": "OK",
        "wifi_ssid": device.wifi_ssid,
        "wifi_password": device.wifi_password})


@app.route('/api/ota/heartbeat', methods=['POST'])
def ota_heartbeat():
    t0 = time.time()
    client_ip = request.remote_addr
    try:
        data = request.get_json(force=True)
    except Exception:
        return jsonify({"code": 400, "message": "Invalid JSON"})

    device_id = data.get('device_id', '')
    device = Device.query.filter_by(device_id=device_id).first()
    if not device:
        return jsonify({"code": 404, "message": "Device not found"})

    device.is_online = True
    device.last_seen = utcnow()
    device.ip_address = client_ip
    if data.get('sys_ver'):
        device.current_sys_version = data['sys_ver']
    if data.get('model_ver'):
        device.current_model_version = data['model_ver']
    pending = PushRecord.query.filter_by(
        target_device_id=device.id, status='pending').first()
    db.session.commit()

    dt = (time.time() - t0) * 1000
    ota_debug(device_id,
              f'HB sys={data.get("sys_ver","?")} '
              f'md={data.get("model_ver","?")} '
              f'push={pending is not None} ({dt:.0f}ms)')
    return jsonify({
        "code": 200, "push_pending": pending is not None,
        "wifi_ssid": device.wifi_ssid,
        "wifi_password": device.wifi_password})


@app.route('/api/ota/check_firmware', methods=['POST'])
def ota_check_firmware():
    t0 = time.time()
    client_ip = request.remote_addr
    try:
        data = request.get_json(force=True)
    except Exception:
        return jsonify({"code": 400, "message": "Invalid JSON"})

    device_id = data.get('device_id', '')
    current_ver = data.get('current_ver', '0.0')
    ota_debug(device_id, f'CHECK_FW: ver={current_ver}', client_ip)

    device = Device.query.filter_by(device_id=device_id).first()
    if not device:
        return jsonify({"code": 404, "message": "Device not found"})

    latest_fw = Firmware.query.filter_by(
        is_active=True, device_model=device.device_model
    ).order_by(Firmware.created_at.desc()).first()

    if not latest_fw or current_ver >= latest_fw.version:
        dt = (time.time() - t0) * 1000
        ota_debug(device_id, f'CHECK_FW: up-to-date ({dt:.0f}ms)')
        return jsonify({"code": 200, "update_available": False})

    result = {
        "code": 200, "update_available": True,
        "new_version": latest_fw.version,
        "file_size": latest_fw.file_size,
        "file_md5": latest_fw.file_md5,
        "release_notes": latest_fw.release_notes or "",
        "download_url": "/api/ota/download_raw"}

    patch = Patch.query.filter_by(
        patch_type='firmware', from_version=current_ver,
        to_version=latest_fw.version).first()
    result["patch_available"] = patch is not None
    if patch:
        result["patch_size"] = patch.file_size
        result["patch_md5"] = patch.file_md5

    dt = (time.time() - t0) * 1000
    ota_debug(device_id,
              f'CHECK_FW: {current_ver}->{latest_fw.version} '
              f'sz={latest_fw.file_size} ({dt:.0f}ms)', client_ip)
    log_event('INFO', 'OTA',
              f'{device_id} check fw: '
              f'{current_ver}->{latest_fw.version}',
              device_id=device_id, ip_address=client_ip)
    return jsonify(result)


@app.route('/api/ota/check_model', methods=['POST'])
def ota_check_model():
    t0 = time.time()
    client_ip = request.remote_addr
    try:
        data = request.get_json(force=True)
    except Exception:
        return jsonify({"code": 400, "message": "Invalid JSON"})

    device_id = data.get('device_id', '')
    current_ver = data.get('current_ver', '0.0')
    ota_debug(device_id, f'CHECK_MD: ver={current_ver}', client_ip)

    device = Device.query.filter_by(device_id=device_id).first()
    if not device:
        return jsonify({"code": 404, "message": "Device not found"})

    latest_model = ModelVersion.query.filter_by(
        is_active=True, device_model=device.device_model
    ).order_by(ModelVersion.created_at.desc()).first()

    if not latest_model or current_ver >= latest_model.version:
        dt = (time.time() - t0) * 1000
        ota_debug(device_id, f'CHECK_MD: up-to-date ({dt:.0f}ms)')
        return jsonify({"code": 200, "update_available": False})

    result = {
        "code": 200, "update_available": True,
        "new_version": latest_model.version,
        "model_name": latest_model.model_name,
        "file_size": latest_model.file_size,
        "file_md5": latest_model.file_md5,
        "description": latest_model.description or "",
        "download_url": "/api/ota/download_raw"}

    patch = Patch.query.filter_by(
        patch_type='model', from_version=current_ver,
        to_version=latest_model.version).first()
    result["patch_available"] = patch is not None
    if patch:
        result["patch_size"] = patch.file_size
        result["patch_md5"] = patch.file_md5

    dt = (time.time() - t0) * 1000
    ota_debug(device_id,
              f'CHECK_MD: {current_ver}->{latest_model.version} '
              f'sz={latest_model.file_size} patch={patch is not None} '
              f'ps={patch.file_size if patch else 0} ({dt:.0f}ms)',
              client_ip)
    log_event('INFO', 'OTA',
              f'{device_id} check model: '
              f'{current_ver}->{latest_model.version}',
              device_id=device_id, ip_address=client_ip)
    return jsonify(result)


@app.route('/api/ota/download', methods=['POST'])
def ota_download():
    """Legacy: JSON hex chunks."""
    t0 = time.time()
    client_ip = request.remote_addr
    try:
        data = request.get_json(force=True)
    except Exception:
        return jsonify({"code": 400, "message": "Invalid JSON"})

    dtype = data.get('type', 'firmware')
    version = data.get('version', '')
    offset = int(data.get('offset', 0))
    length = int(data.get('length', 512))

    if not version:
        return jsonify({"code": 400, "message": "Missing version"})

    if dtype == 'firmware':
        item = Firmware.query.filter_by(
            version=version, is_active=True).first()
    else:
        item = ModelVersion.query.filter_by(
            version=version, is_active=True).first()

    if not item or not os.path.exists(item.file_path):
        return jsonify({"code": 404, "message": "File not found"})

    try:
        with open(item.file_path, 'rb') as f:
            f.seek(offset)
            chunk = f.read(length)
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)})

    dt = (time.time() - t0) * 1000
    ota_debug('', f'DOWNLOAD legacy: sent {len(chunk)}B ({dt:.0f}ms)')

    body = json.dumps({
        "code": 200, "total_size": item.file_size,
        "chunk_size": len(chunk), "data": chunk.hex()
    }, separators=(',', ':'))
    return body, 200, {'Content-Type': 'application/json'}


@app.route('/api/ota/download_patch', methods=['POST'])
def ota_download_patch():
    """JSON hex patch chunks - used by transparent mode on STM32."""
    t0 = time.time()
    client_ip = request.remote_addr
    try:
        data = request.get_json(force=True)
    except Exception:
        return jsonify({"code": 400, "message": "Invalid JSON"})

    dtype = data.get('type', 'firmware')
    from_ver = data.get('from_version', '')
    to_ver = data.get('to_version', '')
    offset = int(data.get('offset', 0))
    length = int(data.get('length', 512))

    patch = Patch.query.filter_by(
        patch_type=dtype, from_version=from_ver,
        to_version=to_ver).first()
    if not patch or not os.path.exists(patch.file_path):
        return jsonify({"code": 404, "message": "Patch not found"})

    try:
        with open(patch.file_path, 'rb') as f:
            f.seek(offset)
            chunk = f.read(length)
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)})

    dt = (time.time() - t0) * 1000
    ota_debug('',
              f'DL_PATCH: offset={offset} sent={len(chunk)}B '
              f'total={patch.file_size}B ({dt:.0f}ms)',
              client_ip)

    body = json.dumps({
        "code": 200, "total_size": patch.file_size,
        "chunk_size": len(chunk), "data": chunk.hex()
    }, separators=(',', ':'))
    return body, 200, {'Content-Type': 'application/json'}


# ============================================================
# OTA Raw Binary Download
# ============================================================

def _locate_ota_file(dtype, version, from_version):
    if from_version:
        patch = Patch.query.filter_by(
            patch_type=dtype, from_version=from_version,
            to_version=version).first()
        if patch and os.path.exists(patch.file_path):
            return patch.file_path, patch.file_size
    elif dtype == 'firmware':
        item = Firmware.query.filter_by(
            version=version, is_active=True).first()
        if item and os.path.exists(item.file_path):
            return item.file_path, item.file_size
    else:
        item = ModelVersion.query.filter_by(
            version=version, is_active=True).first()
        if item and os.path.exists(item.file_path):
            return item.file_path, item.file_size
    return None, 0


@app.route('/api/ota/download_raw', methods=['POST'])
def ota_download_raw():
    t0 = time.time()
    client_ip = request.remote_addr

    try:
        data = request.get_json(force=True)
    except Exception:
        ota_debug('', f'DL_RAW: bad JSON from {client_ip}', client_ip)
        return jsonify({"code": 400, "message": "Invalid JSON"}), 400

    dtype = data.get('type', 'firmware')
    version = data.get('version', '') or data.get('to_version', '')
    from_version = data.get('from_version', '')

    if not version:
        return jsonify({"code": 400, "message": "Missing version"}), 400

    file_path, file_size = _locate_ota_file(dtype, version, from_version)
    if not file_path:
        ota_debug('',
                  f'DL_RAW: NOT FOUND type={dtype} v={version} '
                  f'from={from_version or "-"}', client_ip)
        return jsonify({"code": 404, "message": "File not found"}), 404

    try:
        with open(file_path, 'rb') as f:
            file_data = f.read()
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)}), 500

    dt = (time.time() - t0) * 1000
    ota_debug('',
              f'DL_RAW: {dtype} v{version} '
              f'from={from_version or "full"} '
              f'size={len(file_data)} ({dt:.0f}ms)', client_ip)
    log_event('INFO', 'OTA',
              f'download_raw {dtype} v{version} '
              f'({len(file_data)}B)',
              ip_address=client_ip)

    response = Response(
        file_data, status=200,
        mimetype='application/octet-stream')
    response.headers['Content-Length'] = str(len(file_data))
    return response


@app.route('/api/ota/debug_log', methods=['POST'])
def ota_debug_log():
    client_ip = request.remote_addr
    try:
        data = request.get_json(force=True)
    except Exception:
        return jsonify({"code": 400, "message": "Invalid JSON"})

    device_id = data.get('device_id', '')
    msg = data.get('msg', '')
    level = data.get('level', 'DEBUG')

    ota_debug(device_id, f'CLIENT: {msg}', client_ip)
    log_event(level, 'OTA-Client', msg,
              device_id=device_id, ip_address=client_ip)
    return jsonify({"code": 200, "message": "OK"})


# ============================================================
# Error Handlers
# ============================================================

@app.errorhandler(404)
def not_found(e):
    if request.path.startswith('/api/'):
        return jsonify({"code": 404, "message": "Not found"}), 404
    return '<h2>404</h2>', 404


@app.errorhandler(403)
def forbidden(e):
    if request.path.startswith('/api/'):
        return jsonify({"code": 403, "message": "Forbidden"}), 403
    return '<h2>403</h2>', 403


@app.errorhandler(500)
def server_error(e):
    if request.path.startswith('/api/'):
        return jsonify({"code": 500, "message": "Internal error"}), 500
    return '<h2>500</h2>', 500


# ============================================================
# Init & Run
# ============================================================

def init_db():
    with app.app_context():
        db.create_all()
        if not User.query.filter_by(username='admin').first():
            admin = User(
                username='admin', role='admin',
                email='admin@ota.local')
            admin.set_password('admin123')
            db.session.add(admin)
            db.session.commit()
            print('[INIT] Admin: admin / admin123')
    print(f'[INIT] DEBUG_OTA={DEBUG_OTA}')
    print(f'[INIT] Patch format: SDIFF01 (simple diff)')
    print(f'[INIT] Transmission: transparent mode (JSON+hex)')
    print(f'[INIT] No external dependencies needed')


if __name__ == '__main__':
    init_db()
    app.run(host='0.0.0.0', port=8080, debug=False, threaded=True)
