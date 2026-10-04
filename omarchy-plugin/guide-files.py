#!/usr/bin/env python3
"""Generic capture paths and read-only metrics. No Omakade dependency."""
import configparser
import datetime
import json
import os
from pathlib import Path
import re
import sys

home = Path.home()
config = Path(os.environ.get('XDG_CONFIG_HOME', home / '.config'))
state = Path(os.environ.get('XDG_STATE_HOME', home / '.local/state')) / 'omarchy/guide'
state.mkdir(parents=True, exist_ok=True)
settings_path = state / 'settings.json'
if not settings_path.exists():
    try:
        with settings_path.open('x') as stream:
            stream.write('{}\n')
    except FileExistsError:
        pass

def xdg(name, default):
    try:
        content = (config / 'user-dirs.dirs').read_text()
        match = re.search(r'^XDG_' + name + r'_DIR="([^"]*)"', content, re.M)
        if match:
            return match[1].replace('$HOME', str(home))
    except OSError:
        pass
    return str(home / default)

settings = configparser.ConfigParser(interpolation=None)
settings.read(config / 'omasnap/omasnap.conf')
shots = Path(os.environ.get('OMASNAP_SCREENSHOT_DIR') or os.environ.get('OMARCHY_SCREENSHOT_DIR') or settings.get('output', 'directory', fallback='').strip() or str(Path(xdg('PICTURES', 'Pictures')) / 'Screenshots')).expanduser()
videos = Path(os.environ.get('OMARCHY_SCREENRECORD_DIR') or xdg('VIDEOS', 'Videos'))
for directory in (shots, videos):
    directory.mkdir(parents=True, exist_ok=True)

def screenshot():
    now = datetime.datetime.now()
    name = settings.get('output', 'filename', fallback='screenshot-{date}_{time}-{app}')
    for joined in ('-{app}', '_{app}', ' {app}', '{app}-', '{app}_', '{app} ', '{app}'):
        name = name.replace(joined, '')
    name = name.replace('{date}', now.strftime('%Y-%m-%d')).replace('{time}', now.strftime('%H-%M-%S')).replace('/', '-').lstrip('. -_').strip()
    if name.lower().endswith('.png'):
        name = name[:-4]
    name = name or now.strftime('screenshot-%Y-%m-%d_%H-%M-%S')
    path = shots / (name + '.png')
    suffix = 2
    while path.exists():
        path = shots / (name + '-' + str(suffix) + '.png')
        suffix += 1
    return str(path)

def scan():
    files = []
    for folder in (shots, videos):
        files += [f for f in folder.iterdir() if f.is_file() and f.suffix.lower() in ('.png', '.jpg', '.mp4', '.mkv')]
    files.sort(key=lambda f: f.stat().st_mtime, reverse=True)
    recent = []
    for f in files[:12]:
        shot = f.suffix.lower() in ('.png', '.jpg')
        recent.append(dict(path=str(f), thumb=f.as_uri() if shot else '', kind='Screenshot' if shot else 'Clip', age=datetime.datetime.fromtimestamp(f.stat().st_mtime).strftime('%H:%M')))
    pads = []
    seen = set()
    for event in Path('/sys/class/input').glob('event*'):
        try:
            device = event / 'device'
            chunks = (device / 'capabilities/key').read_text().split()
            bits = sum(int(word, 16) << (64 * i) for i, word in enumerate(reversed(chunks)))
            if not any(bits & (1 << code) for code in (0x130, 0x120)):
                continue
            identity = str(device.resolve())
            if identity in seen:
                continue
            seen.add(identity)
            name = (device / 'name').read_text().strip()
            family = 'playstation' if re.search('dual|sony|playstation', name, re.I) else 'nintendo' if re.search('nintendo|switch|joy-con', name, re.I) else 'deck' if re.search('steam', name, re.I) else 'xbox' if re.search('xbox|x-box|xinput|microsoft', name, re.I) else 'generic'
            pads.append(dict(name=name, id=identity, family=family, identifiable=False))
        except OSError:
            pass
    return dict(shots=str(shots), videos=str(videos), recent=recent, pads=pads, settingsPath=str(state / 'settings.json'))

def stats():
    ticks = list(map(int, Path('/proc/stat').read_text().splitlines()[0].split()[1:]))
    idle, total = ticks[3] + ticks[4], sum(ticks[:8])
    sample = state / 'cpu.json'
    cpu = None
    try:
        old_idle, old_total = json.loads(sample.read_text())
        if total > old_total:
            cpu = max(0, min(1, 1 - (idle - old_idle) / (total - old_total)))
    except (OSError, ValueError):
        pass
    sample.write_text(json.dumps([idle, total]))
    memory = {}
    for line in Path('/proc/meminfo').read_text().splitlines():
        key, value = line.split(':', 1)
        memory[key] = int(value.strip().split()[0])
    used = memory['MemTotal'] - memory['MemAvailable']
    result = [dict(label='RAM', value=f'{used / 1048576:.1f} / {memory["MemTotal"] / 1048576:.1f} GB', progress=used / memory['MemTotal'])]
    if cpu is not None:
        result.insert(0, dict(label='CPU', value=f'{cpu * 100:.0f}%', progress=cpu))
    for sensor in Path('/sys/class/hwmon').glob('hwmon*'):
        try:
            if (sensor / 'name').read_text().strip() in ('coretemp', 'k10temp'):
                value = int((sensor / 'temp1_input').read_text()) / 1000
                result.append(dict(label='CPU temperature', value=f'{value:.0f}°C', progress=0))
        except OSError:
            pass
    # Expose only driver-provided counters. Missing sensors remain absent.
    for device in Path('/sys/class/drm').glob('card[0-9]*/device'):
        for field, label, unit, divisor in [('gpu_busy_percent', 'GPU', '%', 1), ('mem_info_vram_used', 'VRAM', ' GB', 1073741824)]:
            try:
                value = int((device / field).read_text()) / divisor
                result.append(dict(label=label, value=f'{value:.0f}{unit}' if unit == '%' else f'{value:.1f}{unit}', progress=value / 100 if unit == '%' else 0))
            except OSError:
                pass
        for hwmon in (device / 'hwmon').glob('hwmon*'):
            for field, label, divisor, unit in [('temp1_input', 'GPU temperature', 1000, '°C'), ('power1_average', 'GPU power', 1000000, ' W')]:
                try:
                    value = int((hwmon / field).read_text()) / divisor
                    result.append(dict(label=label, value=f'{value:.0f}{unit}', progress=0))
                except OSError:
                    pass
    return result

if sys.argv[1] == 'screenshot':
    print(screenshot())
elif sys.argv[1] == 'stats':
    print(json.dumps(stats()))
else:
    print(json.dumps(scan()))
