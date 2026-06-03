"""
app.py - OTA Server v5.2
========================================
修改清单 (v5.1 → v5.2):
  [FIX]  /api/ota/download 参数兼容: 支持 numeric version (float→str)
  [FIX]  /api/ota/download 支持多种 version 参数名 (version/new_version/ver)
  [FIX]  /api/ota/download 404 时输出完整请求体日志, 便于排查
  [FIX]  /api/ota/download_patch 同步修复
  [IMP]  _normalize_version() 统一处理 version 字段类型转换
========================================
"""

import os
import json
import hashlib
import struct
import time
import sqlite3
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

app.config['SECRET_KEY'] = os.environ.get(
    'OTA_SECRET_KEY', 'dev-key-please-change-in-production')

app.config['SQLALCHEMY_DATABASE_URI'] = 'sqlite:///ota.db'
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False
app.config['UPLOAD_FOLDER'] = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), 'uploads')
app.config['MAX_CONTENT_LENGTH'] = 200 * 1024 * 1024

db = SQLAlchemy(app)

login_manager = LoginManager(app)
login_manager.login_view = 'login'
login_manager.login_message = '请先登录以访问该页面'
login_manager.login_message_category = 'info'

for _d in ('firmware', 'firmware_a', 'firmware_b', 'models', 'patches'):
    os.makedirs(os.path.join(app.config['UPLOAD_FOLDER'], _d), exist_ok=True)

DEBUG_OTA = True


# ============================================================
# Connection: keep-alive 覆盖所有下载路径
# ============================================================

_DOWNLOAD_PREFIXES = (
    '/api/ota/download',
    '/api/ota/chunk',
    '/api/ota/fw',
    '/api/ota/fw_a',
    '/api/ota/fw_b',
)

@app.after_request
def set_connection_header(response):
    if any(request.path.startswith(p) for p in _DOWNLOAD_PREFIXES):
        response.headers['Connection'] = 'keep-alive'
    return response


# ============================================================
# 工具函数
# ============================================================

def utcnow():
    return datetime.now(timezone.utc).replace(tzinfo=None)


def log_event(level, module, message, device_id='', ip_address=''):
    try:
        entry = ServerLog(
            level=level, module=module, message=message,
            device_id=device_id, ip_address=ip_address)
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
# [NEW] version 参数统一归一化
# ============================================================

def _normalize_version(raw):
    """
    将设备发来的 version 统一转为字符串。
    设备可能发送:
      "1.2"        → str OK
       1.2         → float 1.2 → "1.2"
       "1.10"      → str OK
       2           → int → "2"
    """
    if raw is None:
        return ''
    if isinstance(raw, float):
        if raw == int(raw):
            return str(int(raw))
        return str(raw)
    if isinstance(raw, int):
        return str(raw)
    return str(raw).strip()


def version_gt(v1: str, v2: str) -> bool:
    """v1 > v2 返回 True (语义化比较)"""
    try:
        parts1 = [int(x) for x in v1.split('.')]
        parts2 = [int(x) for x in v2.split('.')]
        max_len = max(len(parts1), len(parts2))
        parts1.extend([0] * (max_len - len(parts1)))
        parts2.extend([0] * (max_len - len(parts2)))
        return parts1 > parts2
    except (ValueError, AttributeError):
        return v1 > v2


# ============================================================
# SDIFF01 生成 — merge_gap 合并逻辑
# ============================================================

def generate_simple_diff(old_path, new_path, merge_gap=64):
    with open(old_path, 'rb') as f:
        old_data = f.read()
    with open(new_path, 'rb') as f:
        new_data = f.read()

    old_len = len(old_data)
    new_len = len(new_data)

    raw_changes = []
    i = 0
    n = min(old_len, new_len)
    while i < n:
        if old_data[i] != new_data[i]:
            start = i
            while i < n and old_data[i] != new_data[i]:
                i += 1
            raw_changes.append((start, i))
        else:
            i += 1

    if new_len > old_len:
        raw_changes.append((old_len, new_len))

    if not raw_changes:
        merged = raw_changes
    else:
        merged = [raw_changes[0]]
        for start, end in raw_changes[1:]:
            prev_start, prev_end = merged[-1]
            if start - prev_end <= merge_gap:
                merged[-1] = (prev_start, end)
            else:
                merged.append((start, end))

    result = bytearray()
    result += b'SDIFF01\x00'
    result += struct.pack('<I', new_len)
    result += struct.pack('<I', len(merged))
    for start, end in merged:
        result += struct.pack('<I', start)
        result += struct.pack('<I', end - start)
        result += new_data[start:end]

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
    filename_b = db.Column(db.String(256), default='')
    file_path_b = db.Column(db.String(512), default='')
    file_size_b = db.Column(db.Integer, default=0)
    file_md5_b = db.Column(db.String(32), default='')
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
# Admin Routes
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
    file_a = request.files.get('firmware_a') or request.files.get('firmware')
    file_b = request.files.get('firmware_b')
    version = request.form.get('version', '').strip()
    device_model = request.form.get('device_model', 'STM32F407ZGT6').strip()
    notes = request.form.get('release_notes', '').strip()

    if not file_a or not version:
        flash('需要版本号和分区A固件文件')
        return redirect(url_for('admin_firmware'))

    if Firmware.query.filter_by(version=version).first():
        flash(f'版本 {version} 已存在')
        return redirect(url_for('admin_firmware'))

    filename_a = secure_filename(file_a.filename)
    file_path_a = os.path.join(app.config['UPLOAD_FOLDER'], 'firmware',
                               f'{version}_A_{filename_a}')
    file_a.save(file_path_a)
    file_size_a = os.path.getsize(file_path_a)
    file_md5_a = hashlib.md5(open(file_path_a, 'rb').read()).hexdigest()

    filename_b = ''
    file_path_b = ''
    file_size_b = 0
    file_md5_b = ''

    if file_b and file_b.filename:
        filename_b = secure_filename(file_b.filename)
        file_path_b = os.path.join(app.config['UPLOAD_FOLDER'], 'firmware',
                                   f'{version}_B_{filename_b}')
        file_b.save(file_path_b)
        file_size_b = os.path.getsize(file_path_b)
        file_md5_b = hashlib.md5(open(file_path_b, 'rb').read()).hexdigest()

    fw = Firmware(
        version=version, device_model=device_model,
        filename=filename_a, file_path=file_path_a,
        file_size=file_size_a, file_md5=file_md5_a,
        filename_b=filename_b, file_path_b=file_path_b,
        file_size_b=file_size_b, file_md5_b=file_md5_b,
        release_notes=notes, uploaded_by=current_user.id)

    db.session.add(fw)
    db.session.commit()

    dual_msg = f' + 分区B({file_size_b}B)' if file_path_b else ' (仅分区A)'
    log_event('INFO', 'Firmware',
              f'Uploaded v{version} A:{file_size_a}B{dual_msg}')
    flash(f'固件 v{version} 已上传: 分区A={file_size_a}B{dual_msg}')
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
        if fw.file_path and os.path.exists(fw.file_path):
            os.remove(fw.file_path)
        if fw.file_path_b and os.path.exists(fw.file_path_b):
            os.remove(fw.file_path_b)
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
# 补丁生成 — SDIFF01
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

    old_patch = Patch.query.filter_by(
        patch_type=patch_type, from_version=from_ver,
        to_version=to_ver).first()
    if old_patch:
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

        diff_data = generate_simple_diff(
            from_item.file_path, to_item.file_path, merge_gap=64)

        with open(patch_path, 'wb') as f:
            f.write(diff_data)

        with open(patch_path, 'rb') as f:
            verify = f.read(8)
        if verify != b'SDIFF01\x00':
            raise ValueError(f"Verification failed: {verify}")

    except Exception as e:
        if os.path.exists(patch_path):
            os.remove(patch_path)
        flash(f'补丁生成失败: {e}')
        import traceback
        traceback.print_exc()
        return redirect(url_for('admin_push'))

    change_count = 0
    try:
        with open(patch_path, 'rb') as f:
            f.seek(12)
            change_count = struct.unpack('<I', f.read(4))[0]
    except Exception:
        pass

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
# Dashboard API
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
            'version': f.version,
            'size_a': f.file_size,
            'size_b': f.file_size_b or 0,
            'md5_a': f.file_md5 or '',
            'md5_b': f.file_md5_b or '',
            'dual': bool(f.file_path_b),
            'time': f.created_at.isoformat() if f.created_at else None
        } for f in fws],
        'active': active.version if active else ''})


@app.route('/api/dashboard/upload', methods=['POST'])
@login_required
def dash_upload():
    version = request.form.get('version', '').strip()
    file_a = request.files.get('firmware_a') or request.files.get('firmware')
    file_b = request.files.get('firmware_b')
    if not version or not file_a:
        return jsonify({'ok': False, 'error': '需要版本号和分区A固件文件'})
    if Firmware.query.filter_by(version=version).first():
        return jsonify({'ok': False, 'error': f'版本 {version} 已存在'})

    filename_a = secure_filename(file_a.filename) or f'fw_{version}_a.bin'
    file_path_a = os.path.join(app.config['UPLOAD_FOLDER'], 'firmware',
                               f'{version}_A_{filename_a}')
    file_a.save(file_path_a)
    file_size_a = os.path.getsize(file_path_a)
    file_md5_a = hashlib.md5(open(file_path_a, 'rb').read()).hexdigest()

    filename_b = ''
    file_path_b = ''
    file_size_b = 0
    file_md5_b = ''
    if file_b and file_b.filename:
        filename_b = secure_filename(file_b.filename) or f'fw_{version}_b.bin'
        file_path_b = os.path.join(app.config['UPLOAD_FOLDER'], 'firmware',
                                   f'{version}_B_{filename_b}')
        file_b.save(file_path_b)
        file_size_b = os.path.getsize(file_path_b)
        file_md5_b = hashlib.md5(open(file_path_b, 'rb').read()).hexdigest()

    db.session.add(Firmware(
        version=version,
        filename=filename_a, file_path=file_path_a,
        file_size=file_size_a, file_md5=file_md5_a,
        filename_b=filename_b, file_path_b=file_path_b,
        file_size_b=file_size_b, file_md5_b=file_md5_b,
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
    if fw.file_path and os.path.exists(fw.file_path):
        os.remove(fw.file_path)
    if fw.file_path_b and os.path.exists(fw.file_path_b):
        os.remove(fw.file_path_b)
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
# OTA API — 注册 & 心跳
# ============================================================

@app.route('/api/ota/register', methods=['POST'])
def ota_register():
    try:
        t0 = time.time()
        client_ip = request.remote_addr
        data = request.get_json(force=True)

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
        if data.get('sys_version'):
            device.current_sys_version = data['sys_version']
        if data.get('model_version'):
            device.current_model_version = data['model_version']
        db.session.commit()

        dt = (time.time() - t0) * 1000
        ota_debug(device_id, f'REGISTER ok ({dt:.0f}ms)')
        log_event('INFO', 'OTA', f'Device online: {device_id}',
                  device_id=device_id, ip_address=client_ip)
        return jsonify({
            "code": 200, "message": "OK",
            "wifi_ssid": device.wifi_ssid,
            "wifi_password": device.wifi_password})
    except Exception as e:
        db.session.rollback()
        log_event('ERROR', 'OTA', f'register failed: {e}')
        return jsonify({"code": 500, "message": "Internal error"}), 500


@app.route('/api/ota/heartbeat', methods=['POST'])
def ota_heartbeat():
    try:
        t0 = time.time()
        client_ip = request.remote_addr
        data = request.get_json(force=True)

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
    except Exception as e:
        db.session.rollback()
        log_event('ERROR', 'OTA', f'heartbeat failed: {e}')
        return jsonify({"code": 500, "message": "Internal error"}), 500


# ============================================================
# OTA 固件检查 — 语义化比较
# ============================================================

@app.route('/api/ota/check', methods=['GET'])
def ota_check():
    try:
        t0 = time.time()
        client_ip = request.remote_addr

        device_id = request.args.get('device_id', '')
        sys_version = request.args.get('sys_version', '0.0')
        model_version = request.args.get('model_version', '')

        ota_debug(device_id,
                  f'CHECK: sys_ver={sys_version} model_ver={model_version}',
                  client_ip)

        device = Device.query.filter_by(device_id=device_id).first()
        device_model = device.device_model if device else 'STM32F407ZGT6'

        latest_fw = Firmware.query.filter_by(
            is_active=True, device_model=device_model
        ).order_by(Firmware.created_at.desc()).first()

        if not latest_fw:
            dt = (time.time() - t0) * 1000
            ota_debug(device_id,
                      f'CHECK: no firmware ({dt:.0f}ms)', client_ip)
            return jsonify({"code": 200, "update": False})

        if sys_version and sys_version != 'unknown' and \
           not version_gt(latest_fw.version, sys_version):
            dt = (time.time() - t0) * 1000
            ota_debug(device_id,
                      f'CHECK: up-to-date {sys_version}>={latest_fw.version} '
                      f'({dt:.0f}ms)', client_ip)
            return jsonify({"code": 200, "update": False})

        result = {
            "code": 200,
            "update": True,
            "version": latest_fw.version,
            "md5": latest_fw.file_md5 or "",
            "size": latest_fw.file_size or 0,
        }

        if latest_fw.file_path_b:
            result["url_a"] = f"/api/ota/chunk_a/{latest_fw.version}"
            result["url_b"] = f"/api/ota/chunk_b/{latest_fw.version}"
            result["size_a"] = latest_fw.file_size
            result["size_b"] = latest_fw.file_size_b
            result["md5_a"] = latest_fw.file_md5 or ""
            result["md5_b"] = latest_fw.file_md5_b or ""
            mode = "dual"
        else:
            result["url"] = f"/api/ota/chunk/{latest_fw.version}"
            result["url_a"] = result["url"]
            result["url_b"] = result["url"]
            mode = "single"

        dt = (time.time() - t0) * 1000
        ota_debug(device_id,
                  f'CHECK: {sys_version}->{latest_fw.version} '
                  f'sz={latest_fw.file_size} mode={mode} ({dt:.0f}ms)',
                  client_ip)
        log_event('INFO', 'OTA',
                  f'{device_id} check: {sys_version}->{latest_fw.version} '
                  f'({mode})',
                  device_id=device_id, ip_address=client_ip)
        return jsonify(result)
    except Exception as e:
        db.session.rollback()
        log_event('ERROR', 'OTA', f'check failed: {e}')
        return jsonify({"code": 500, "message": "Internal error"}), 500


@app.route('/api/ota/check_firmware', methods=['POST'])
def ota_check_firmware():
    try:
        t0 = time.time()
        client_ip = request.remote_addr
        data = request.get_json(force=True)

        device_id = data.get('device_id', '')
        current_ver = data.get('current_ver', '0.0')
        ota_debug(device_id, f'CHECK_FW: ver={current_ver}', client_ip)

        device = Device.query.filter_by(device_id=device_id).first()
        if not device:
            return jsonify({"code": 404, "message": "Device not found"})

        latest_fw = Firmware.query.filter_by(
            is_active=True, device_model=device.device_model
        ).order_by(Firmware.created_at.desc()).first()

        if not latest_fw or not version_gt(latest_fw.version, current_ver):
            dt = (time.time() - t0) * 1000
            ota_debug(device_id,
                      f'CHECK_FW: up-to-date ({dt:.0f}ms)')
            return jsonify({"code": 200, "update_available": False})

        result = {
            "code": 200, "update_available": True,
            "new_version": latest_fw.version,
            "file_size": latest_fw.file_size,
            "file_md5": latest_fw.file_md5,
            "release_notes": latest_fw.release_notes or "",
            "download_url": "/api/ota/download"}

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
    except Exception as e:
        db.session.rollback()
        log_event('ERROR', 'OTA', f'check_firmware failed: {e}')
        return jsonify({"code": 500, "message": "Internal error"}), 500


@app.route('/api/ota/check_model', methods=['POST'])
def ota_check_model():
    try:
        t0 = time.time()
        client_ip = request.remote_addr
        data = request.get_json(force=True)

        device_id = data.get('device_id', '')
        current_ver = data.get('current_ver', '0.0')
        ota_debug(device_id, f'CHECK_MD: ver={current_ver}', client_ip)

        device = Device.query.filter_by(device_id=device_id).first()
        if not device:
            return jsonify({"code": 404, "message": "Device not found"})

        latest_model = ModelVersion.query.filter_by(
            is_active=True, device_model=device.device_model
        ).order_by(ModelVersion.created_at.desc()).first()

        if not latest_model or not version_gt(latest_model.version, current_ver):
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
            "download_url": "/api/ota/download",
            "chunk_url": f"/api/ota/chunk_model/{latest_model.version}"}

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
                  f'sz={latest_model.file_size} '
                  f'patch={patch is not None} '
                  f'ps={patch.file_size if patch else 0} ({dt:.0f}ms)',
                  client_ip)
        log_event('INFO', 'OTA',
                  f'{device_id} check model: '
                  f'{current_ver}->{latest_model.version}',
                  device_id=device_id, ip_address=client_ip)
        return jsonify(result)
    except Exception as e:
        db.session.rollback()
        log_event('ERROR', 'OTA', f'check_model failed: {e}')
        return jsonify({"code": 500, "message": "Internal error"}), 500


# ============================================================
# [FIX] OTA 分块下载 — 兼容多种参数名 + version 类型归一化
# ============================================================

@app.route('/api/ota/download', methods=['POST'])
def ota_download():
    try:
        t0 = time.time()
        client_ip = request.remote_addr
        data = request.get_json(force=True)

        dtype = data.get('type', 'firmware')

        # [FIX] 兼容多种 version 参数名
        #   设备可能发送: version, new_version, ver, file_version
        version = _normalize_version(
            data.get('version')
            or data.get('new_version')
            or data.get('ver')
            or data.get('file_version')
            or '')

        offset = int(data.get('offset', 0))
        length = int(data.get('length', 512))

        ota_debug('',
                  f'DOWNLOAD_REQ: type={dtype!r} ver={version!r} '
                  f'off={offset} len={length} body_keys={list(data.keys())}',
                  client_ip)

        if not version:
            log_event('WARNING', 'OTA',
                      f'DOWNLOAD: missing version! body={data}',
                      ip_address=client_ip)
            return jsonify({"code": 400, "message": "Missing version"})

        if dtype == 'firmware':
            item = Firmware.query.filter_by(
                version=version, is_active=True).first()
        else:
            item = ModelVersion.query.filter_by(
                version=version, is_active=True).first()

        if not item or not os.path.exists(item.file_path):
            # [FIX] 404 时输出完整请求体, 便于排查
            log_event('WARNING', 'OTA',
                      f'DOWNLOAD 404: type={dtype!r} ver={version!r} '
                      f'body={json.dumps(data, ensure_ascii=False)}',
                      ip_address=client_ip)
            ota_debug('',
                      f'DOWNLOAD 404: type={dtype!r} ver={version!r} '
                      f'item={item is not None} '
                      f'file_exists={os.path.exists(item.file_path) if item else "N/A"}',
                      client_ip)
            return jsonify({"code": 404, "message": "File not found"})

        file_size = item.file_size

        if offset >= file_size:
            return jsonify({
                "code": 200, "total_size": file_size,
                "chunk_size": 0, "data": "", "eof": True})

        actual_length = min(length, file_size - offset)

        with open(item.file_path, 'rb') as f:
            f.seek(offset)
            chunk = f.read(actual_length)

        dt = (time.time() - t0) * 1000
        ota_debug('',
                  f'DOWNLOAD: type={dtype} v={version} '
                  f'off={offset} sent={len(chunk)}B ({dt:.0f}ms)')

        body = json.dumps({
            "code": 200, "total_size": file_size,
            "chunk_size": len(chunk), "data": chunk.hex(),
            "eof": (offset + len(chunk) >= file_size)
        }, separators=(',', ':'))
        return body, 200, {'Content-Type': 'application/json'}
    except Exception as e:
        log_event('ERROR', 'OTA', f'download failed: {e}')
        return jsonify({"code": 500, "message": str(e)}), 500


@app.route('/api/ota/download_patch', methods=['POST'])
def ota_download_patch():
    try:
        t0 = time.time()
        client_ip = request.remote_addr
        data = request.get_json(force=True)

        dtype = data.get('type', 'firmware')

        # [FIX] 兼容多种参数名
        from_ver = _normalize_version(
            data.get('from_version')
            or data.get('from_ver')
            or data.get('from')
            or '')
        to_ver = _normalize_version(
            data.get('to_version')
            or data.get('to_ver')
            or data.get('version')
            or data.get('new_version')
            or '')

        offset = int(data.get('offset', 0))
        length = int(data.get('length', 512))

        ota_debug('',
                  f'DL_PATCH_REQ: type={dtype!r} '
                  f'from={from_ver!r} to={to_ver!r}',
                  client_ip)

        patch = Patch.query.filter_by(
            patch_type=dtype, from_version=from_ver,
            to_version=to_ver).first()
        if not patch or not os.path.exists(patch.file_path):
            log_event('WARNING', 'OTA',
                      f'DL_PATCH 404: type={dtype!r} '
                      f'from={from_ver!r} to={to_ver!r} body={data}',
                      ip_address=client_ip)
            return jsonify({"code": 404, "message": "Patch not found"})

        file_size = patch.file_size

        if offset >= file_size:
            return jsonify({
                "code": 200, "total_size": file_size,
                "chunk_size": 0, "data": "", "eof": True})

        actual_length = min(length, file_size - offset)

        with open(patch.file_path, 'rb') as f:
            f.seek(offset)
            chunk = f.read(actual_length)

        dt = (time.time() - t0) * 1000
        ota_debug('',
                  f'DL_PATCH: offset={offset} sent={len(chunk)}B '
                  f'total={file_size}B ({dt:.0f}ms)', client_ip)

        body = json.dumps({
            "code": 200, "total_size": file_size,
            "chunk_size": len(chunk), "data": chunk.hex(),
            "eof": (offset + len(chunk) >= file_size)
        }, separators=(',', ':'))
        return body, 200, {'Content-Type': 'application/json'}
    except Exception as e:
        log_event('ERROR', 'OTA', f'download_patch failed: {e}')
        return jsonify({"code": 500, "message": str(e)}), 500


# ============================================================
# OTA 原始二进制下载 (POST JSON)
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
    try:
        client_ip = request.remote_addr
        data = request.get_json(force=True)

        dtype = data.get('type', 'firmware')
        version = _normalize_version(
            data.get('version')
            or data.get('new_version')
            or data.get('ver')
            or '')
        from_version = _normalize_version(
            data.get('from_version') or data.get('from_ver') or '')

        if not version:
            return jsonify({"code": 400, "message": "Missing version"}), 400

        file_path, file_size = _locate_ota_file(dtype, version, from_version)
        if not file_path:
            return jsonify({"code": 404, "message": "File not found"}), 404

        with open(file_path, 'rb') as f:
            file_data = f.read()

        ota_debug('',
                  f'DL_RAW: {dtype} v{version} '
                  f'from={from_version or "full"} '
                  f'size={len(file_data)}', client_ip)

        response = Response(
            file_data, status=200, mimetype='application/octet-stream')
        response.headers['Content-Length'] = str(len(file_data))
        return response
    except Exception as e:
        log_event('ERROR', 'OTA', f'download_raw failed: {e}')
        return jsonify({"code": 500, "message": str(e)}), 500


# ============================================================
# OTA 整包二进制下载 (GET)
# ============================================================

@app.route('/api/ota/fw/<version>', methods=['GET'])
def ota_fw_single(version):
    try:
        client_ip = request.remote_addr
        fw = Firmware.query.filter_by(version=version, is_active=True).first()
        if not fw:
            abort(404)
        fp = fw.file_path
        if not fp or not os.path.exists(fp):
            abort(404)

        ota_debug('', f'DL_FW: v{version} size={fw.file_size}', client_ip)
        log_event('INFO', 'OTA',
                  f'download fw v{version} ({fw.file_size}B)',
                  ip_address=client_ip)

        with open(fp, 'rb') as f:
            data = f.read()
        resp = Response(data, status=200, mimetype='application/octet-stream')
        resp.headers['Content-Length'] = str(len(data))
        resp.headers['X-MD5'] = fw.file_md5 or ''
        return resp
    except Exception as e:
        log_event('ERROR', 'OTA', f'fw download failed: {e}')
        abort(500)


@app.route('/api/ota/fw_a/<version>', methods=['GET'])
def ota_fw_a(version):
    try:
        client_ip = request.remote_addr
        fw = Firmware.query.filter_by(version=version, is_active=True).first()
        if not fw:
            abort(404)
        fp = fw.file_path
        if not fp or not os.path.exists(fp):
            abort(404)

        ota_debug('', f'DL_FW_A: v{version} size={fw.file_size}', client_ip)
        log_event('INFO', 'OTA',
                  f'download fw_A v{version} ({fw.file_size}B)',
                  ip_address=client_ip)

        with open(fp, 'rb') as f:
            data = f.read()
        resp = Response(data, status=200, mimetype='application/octet-stream')
        resp.headers['Content-Length'] = str(len(data))
        resp.headers['X-MD5'] = fw.file_md5 or ''
        return resp
    except Exception as e:
        abort(500)


@app.route('/api/ota/fw_b/<version>', methods=['GET'])
def ota_fw_b(version):
    try:
        client_ip = request.remote_addr
        fw = Firmware.query.filter_by(version=version, is_active=True).first()
        if not fw:
            abort(404)
        fp = fw.file_path_b
        if not fp or not os.path.exists(fp):
            ota_debug('', f'DL_FW_B: v{version} NO FILE_B', client_ip)
            abort(404)

        ota_debug('', f'DL_FW_B: v{version} size={fw.file_size_b}', client_ip)
        log_event('INFO', 'OTA',
                  f'download fw_B v{version} ({fw.file_size_b}B)',
                  ip_address=client_ip)

        with open(fp, 'rb') as f:
            data = f.read()
        resp = Response(data, status=200, mimetype='application/octet-stream')
        resp.headers['Content-Length'] = str(len(data))
        resp.headers['X-MD5'] = fw.file_md5_b or ''
        return resp
    except Exception as e:
        abort(500)


# ============================================================
# 二进制分块下载 — 共用 _make_chunk_response
# ============================================================

def _make_chunk_response(file_path, file_size, file_md5,
                          offset, length, label=''):
    """构造二进制分块响应"""
    if offset >= file_size:
        resp = Response(b'', status=200, mimetype='application/octet-stream')
        resp.headers['Content-Length'] = '0'
        resp.headers['X-Total-Size'] = str(file_size)
        resp.headers['X-MD5'] = file_md5 or ''
        resp.headers['X-Offset'] = str(offset)
        resp.headers['X-EOF'] = 'true'
        return resp

    actual_length = min(length, file_size - offset)
    is_eof = (offset + actual_length >= file_size)

    with open(file_path, 'rb') as f:
        f.seek(offset)
        chunk = f.read(actual_length)

    resp = Response(chunk, status=200, mimetype='application/octet-stream')
    resp.headers['Content-Length'] = str(len(chunk))
    resp.headers['X-Total-Size'] = str(file_size)
    resp.headers['X-MD5'] = file_md5 or ''
    resp.headers['X-Offset'] = str(offset)
    resp.headers['X-EOF'] = 'true' if is_eof else 'false'

    return resp


@app.route('/api/ota/chunk/<version>', methods=['GET'])
def ota_chunk(version):
    try:
        client_ip = request.remote_addr
        offset = int(request.args.get('offset', 0))
        length = int(request.args.get('length', 4096))

        if length > 65536:
            length = 65536
        if offset < 0:
            offset = 0

        fw = Firmware.query.filter_by(version=version, is_active=True).first()
        if not fw or not os.path.exists(fw.file_path):
            abort(404)

        ota_debug('',
                  f'CHUNK: v{version} off={offset} len={length} '
                  f'total={fw.file_size}', client_ip)

        return _make_chunk_response(
            fw.file_path, fw.file_size, fw.file_md5, offset, length)
    except ValueError:
        return jsonify({"code": 400, "message": "Invalid offset/length"}), 400
    except Exception as e:
        log_event('ERROR', 'OTA', f'chunk failed: {e}')
        abort(500)


@app.route('/api/ota/chunk_a/<version>', methods=['GET'])
def ota_chunk_a(version):
    try:
        offset = int(request.args.get('offset', 0))
        length = int(request.args.get('length', 4096))
        if length > 65536: length = 65536
        if offset < 0: offset = 0

        fw = Firmware.query.filter_by(version=version, is_active=True).first()
        if not fw or not os.path.exists(fw.file_path):
            abort(404)

        return _make_chunk_response(
            fw.file_path, fw.file_size, fw.file_md5, offset, length, 'A')
    except Exception:
        abort(500)


@app.route('/api/ota/chunk_b/<version>', methods=['GET'])
def ota_chunk_b(version):
    try:
        offset = int(request.args.get('offset', 0))
        length = int(request.args.get('length', 4096))
        if length > 65536: length = 65536
        if offset < 0: offset = 0

        fw = Firmware.query.filter_by(version=version, is_active=True).first()
        if not fw:
            abort(404)
        fp = fw.file_path_b
        if not fp or not os.path.exists(fp):
            abort(404)

        return _make_chunk_response(
            fp, fw.file_size_b, fw.file_md5_b, offset, length, 'B')
    except Exception:
        abort(500)


# ============================================================
# 模型二进制分块下载
# ============================================================

@app.route('/api/ota/chunk_model/<version>', methods=['GET'])
def ota_chunk_model(version):
    try:
        client_ip = request.remote_addr
        offset = int(request.args.get('offset', 0))
        length = int(request.args.get('length', 4096))

        if length > 65536:
            length = 65536
        if offset < 0:
            offset = 0

        mv = ModelVersion.query.filter_by(
            version=version, is_active=True).first()
        if not mv or not os.path.exists(mv.file_path):
            abort(404)

        ota_debug('',
                  f'CHUNK_MODEL: v{version} off={offset} len={length} '
                  f'total={mv.file_size}', client_ip)
        log_event('INFO', 'OTA',
                  f'chunk model v{version} off={offset} len={length}',
                  ip_address=client_ip)

        return _make_chunk_response(
            mv.file_path, mv.file_size, mv.file_md5, offset, length, 'MODEL')
    except ValueError:
        return jsonify({"code": 400, "message": "Invalid offset/length"}), 400
    except Exception as e:
        log_event('ERROR', 'OTA', f'chunk_model failed: {e}')
        abort(500)


# ============================================================
# 补丁二进制分块下载
# ============================================================

@app.route('/api/ota/chunk_patch/<patch_type>/<from_ver>/<to_ver>',
           methods=['GET'])
def ota_chunk_patch(patch_type, from_ver, to_ver):
    try:
        client_ip = request.remote_addr
        offset = int(request.args.get('offset', 0))
        length = int(request.args.get('length', 4096))

        if length > 65536:
            length = 65536
        if offset < 0:
            offset = 0

        patch = Patch.query.filter_by(
            patch_type=patch_type, from_version=from_ver,
            to_version=to_ver).first()
        if not patch or not os.path.exists(patch.file_path):
            abort(404)

        ota_debug('',
                  f'CHUNK_PATCH: {patch_type} {from_ver}->{to_ver} '
                  f'off={offset} len={length} total={patch.file_size}',
                  client_ip)
        log_event('INFO', 'OTA',
                  f'chunk patch {patch_type} {from_ver}->{to_ver} '
                  f'off={offset} len={length}',
                  ip_address=client_ip)

        return _make_chunk_response(
            patch.file_path, patch.file_size, patch.file_md5,
            offset, length, f'PATCH:{patch_type}')
    except ValueError:
        return jsonify({"code": 400, "message": "Invalid offset/length"}), 400
    except Exception as e:
        log_event('ERROR', 'OTA', f'chunk_patch failed: {e}')
        abort(500)


# ============================================================
# OTA Debug Log
# ============================================================

@app.route('/api/ota/debug_log', methods=['POST'])
def ota_debug_log():
    try:
        client_ip = request.remote_addr
        data = request.get_json(force=True)
        device_id = data.get('device_id', '')
        msg = data.get('msg', '')
        level = data.get('level', 'DEBUG')
        ota_debug(device_id, f'CLIENT: {msg}', client_ip)
        log_event(level, 'OTA-Client', msg,
                  device_id=device_id, ip_address=client_ip)
        return jsonify({"code": 200, "message": "OK"})
    except Exception:
        return jsonify({"code": 400, "message": "Invalid JSON"})


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
# 数据库迁移
# ============================================================

def migrate_db():
    db_uri = app.config['SQLALCHEMY_DATABASE_URI']
    if not db_uri.startswith('sqlite:///'):
        return
    db_path = db_uri.replace('sqlite:///', '')
    if not os.path.isabs(db_path):
        db_path = os.path.join(
            os.path.dirname(os.path.abspath(__file__)), db_path)
    if not os.path.exists(db_path):
        return

    try:
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        cursor.execute("PRAGMA table_info(firmware)")
        columns = [row[1] for row in cursor.fetchall()]

        new_cols = {
            'file_path_b': "VARCHAR(512) DEFAULT ''",
            'file_size_b': "INTEGER DEFAULT 0",
            'file_md5_b':  "VARCHAR(32) DEFAULT ''",
            'filename_b':  "VARCHAR(256) DEFAULT ''",
        }

        added = False
        for col_name, col_def in new_cols.items():
            if col_name not in columns:
                cursor.execute(
                    f"ALTER TABLE firmware ADD COLUMN {col_name} {col_def}")
                added = True

        conn.commit()
        conn.close()

        if added:
            print('[INIT] Migrated firmware table: added partition B columns')
    except Exception as e:
        print(f'[INIT] Migration warning: {e}')


# ============================================================
# Init & Run
# ============================================================

def init_db():
    with app.app_context():
        migrate_db()
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
    print(f'[INIT] Patch format: SDIFF01 (simple diff + merge_gap)')
    print(f'[INIT] Chunk download: binary (GET /api/ota/chunk/<ver>)')
    print(f'[INIT] Dual firmware: supported (chunk_a + chunk_b)')
    print(f'[INIT] Model chunk: supported (GET /api/ota/chunk_model/<ver>)')
    print(f'[INIT] Patch chunk: supported (GET /api/ota/chunk_patch/<type>/<from>/<to>)')


if __name__ == '__main__':
    init_db()
    app.run(host='0.0.0.0', port=8080, debug=False, threaded=True)
