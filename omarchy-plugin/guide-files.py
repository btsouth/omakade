#!/usr/bin/env python3
"""Readings and capture actions for the guide card. No Omakade dependency.

  status                 CPU/GPU load and temperature, running recorder processes
  screenshot OUTPUT      save OUTPUT's current frame where Omasnap saves screenshots
  record OUTPUT          record a clip of OUTPUT with the guide's own recorder
  record-stop PID        stop the clip `record` started
  replay-save PID        save the replay buffer of gpu-screen-recorder PID

Capture actions report through Omarchy notifications, and only after the file exists.
"""
import configparser
import datetime
import fcntl
from contextlib import contextmanager
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

home = Path.home()
config = Path(os.environ.get('XDG_CONFIG_HOME', home / '.config'))
state = Path(os.environ.get('XDG_STATE_HOME', home / '.local/state')) / 'omarchy/guide'
recorder = 'gpu-screen-recorder'


def xdg(name, default):
    try:
        content = (config / 'user-dirs.dirs').read_text()
        match = re.search(r'^XDG_' + name + r'_DIR="([^"]*)"', content, re.M)
        if match:
            return match[1].replace('$HOME', str(home))
    except OSError:
        pass
    return str(home / default)


def notify(headline, description='', image=''):
    command = ['omarchy-notification-send']
    if image:
        command += ['--image', image]
    command += [headline] + ([description] if description else [])
    try:
        subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
    except (OSError, subprocess.SubprocessError):
        pass


def refresh_indicators():
    try:
        subprocess.run(['omarchy-shell', '-q', 'omarchy.indicators', 'refresh'],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=3)
    except (OSError, subprocess.SubprocessError):
        pass


def read(path):
    try:
        return Path(path).read_text()
    except OSError:
        return None


def number(path, divisor=1):
    text = read(path)
    try:
        return round(int(text) / divisor) if text is not None else None
    except ValueError:
        return None


# ------------------------------------------------------------------ readings

def cpu_ticks():
    ticks = list(map(int, Path('/proc/stat').read_text().splitlines()[0].split()[1:]))
    return ticks[3] + ticks[4], sum(ticks[:8])


def cpu_load():
    # Load since the previous poll; a fresh open takes its own short sample.
    sample = state / 'cpu.json'
    idle, total = cpu_ticks()
    previous = None
    try:
        if time.time() - sample.stat().st_mtime < 5:
            previous = json.loads(sample.read_text())
    except (OSError, ValueError):
        pass
    if not previous or total <= previous[1]:
        previous = (idle, total)
        time.sleep(0.15)
        idle, total = cpu_ticks()
    try:
        state.mkdir(parents=True, exist_ok=True)
        sample.write_text(json.dumps([idle, total]))
    except OSError:
        pass
    if total <= previous[1]:
        return None
    return round(100 * max(0, min(1, 1 - (idle - previous[0]) / (total - previous[1]))))


def cpu_temperature():
    for sensor in Path('/sys/class/hwmon').glob('hwmon*'):
        if (read(sensor / 'name') or '').strip() in ('coretemp', 'k10temp', 'zenpower'):
            value = number(sensor / 'temp1_input', 1000)
            if value is not None:
                return value
    return None


def gpu_readings():
    # Only driver-provided counters; a missing sensor stays absent.
    for device in sorted(Path('/sys/class/drm').glob('card[0-9]*/device')):
        busy = number(device / 'gpu_busy_percent')
        if busy is None:
            continue
        temperature = None
        for hwmon in (device / 'hwmon').glob('hwmon*'):
            temperature = number(hwmon / 'temp1_input', 1000)
            if temperature is not None:
                break
        return busy, temperature
    return nvidia_readings()


def nvidia_readings():
    # NVIDIA's driver has no busy counter in sysfs; nvidia-smi reports both.
    tool = shutil.which('nvidia-smi')
    if not tool:
        return None, None
    try:
        text = subprocess.run([tool, '--query-gpu=utilization.gpu,temperature.gpu',
                               '--format=csv,noheader,nounits'],
                              capture_output=True, text=True, timeout=2).stdout
    except (OSError, subprocess.SubprocessError):
        return None, None
    values = []
    for field in (text.splitlines() or [''])[0].split(','):
        try:
            values.append(round(float(field)))
        except ValueError:
            values.append(None)
    return tuple((values + [None, None])[:2])


def recorders():
    boot = None
    for line in (read('/proc/stat') or '').splitlines():
        if line.startswith('btime '):
            boot = int(line.split()[1])
    ticks = os.sysconf('SC_CLK_TCK')
    found = []
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit():
            continue
        try:
            args = (entry / 'cmdline').read_bytes().split(b'\0')
        except OSError:
            continue
        if not args or os.path.basename(args[0].decode(errors='replace')) != recorder:
            continue
        args = [a.decode(errors='replace') for a in args if a]
        stat = read(entry / 'stat') or ''
        fields = stat[stat.rfind(')') + 2:].split()
        started = boot + int(fields[19]) / ticks if boot is not None and len(fields) > 19 else None
        option = lambda name: args[args.index(name) + 1] if name in args[:-1] else None
        found.append(dict(pid=int(entry.name), started=started, replay=option('-r'), output=option('-o')))
    return found


def process_start(pid):
    stat = read('/proc/%d/stat' % pid) or ''
    fields = stat[stat.rfind(')') + 2:].split()
    return int(fields[19]) if len(fields) > 19 else None


# The guide's clip, by process identity. A recording someone started elsewhere, such as
# Omarchy's screen recorder, is never shown as the guide's and never stopped by it.
clip_record = state / 'recording.json'


@contextmanager
def clip_lock():
    state.mkdir(parents=True, exist_ok=True)
    # Keep the lock inode: unlinking it would let waiters lock different files.
    with (state / 'recording.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        yield


def own_clip():
    try:
        with clip_lock():
            return locked_clip()
    except OSError:
        return None


def locked_clip():
    try:
        clip = json.loads(clip_record.read_text())
        pid = int(clip['pid'])
    except (OSError, ValueError, KeyError, TypeError):
        return None
    if own_recorder(pid) and process_start(pid) == clip.get('start'):
        return clip
    clip_record.unlink(missing_ok=True)
    return None


def status():
    busy, gpu_temperature = gpu_readings()
    result = dict(cpu=cpu_load(), cpuTemp=cpu_temperature(), gpu=busy, gpuTemp=gpu_temperature,
                  recording=None, replay=None)
    clip = own_clip()
    for process in recorders():
        if process['replay']:
            try:
                seconds = int(process['replay'])
            except ValueError:
                seconds = None
            result['replay'] = dict(pid=process['pid'], seconds=seconds)
        elif clip and process['pid'] == clip['pid']:
            result['recording'] = dict(pid=process['pid'], started=process['started'])
    return {key: value for key, value in result.items() if value is not None}


# ------------------------------------------------------------------ capture

def screenshot_path():
    settings = configparser.ConfigParser(interpolation=None)
    settings.read(config / 'omasnap/omasnap.conf')
    shots = Path(os.environ.get('OMASNAP_SCREENSHOT_DIR') or os.environ.get('OMARCHY_SCREENSHOT_DIR')
                 or settings.get('output', 'directory', fallback='').strip()
                 or str(Path(xdg('PICTURES', 'Pictures')) / 'Screenshots')).expanduser()
    shots.mkdir(parents=True, exist_ok=True)
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
    return path


def screenshot(output):
    path = screenshot_path()
    command = ['grim'] + (['-o', output] if output else []) + [str(path)]
    try:
        subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10, check=True)
    except (OSError, subprocess.SubprocessError):
        pass
    if path.is_file() and path.stat().st_size:
        notify('Screenshot saved', path.name, str(path))
        print(path)
        return 0
    path.unlink(missing_ok=True)
    notify('Screenshot failed', 'The game frame could not be captured')
    return 1


def videos():
    path = Path(os.environ.get('OMARCHY_SCREENRECORD_DIR') or xdg('VIDEOS', 'Videos')).expanduser()
    path.mkdir(parents=True, exist_ok=True)
    return path


def record(output):
    try:
        with clip_lock():
            return start_clip(output)
    except OSError:
        notify('Recording failed', 'The recording state could not be saved')
        return 1


def terminate_recorder(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def start_clip(output):
    if locked_clip():
        return 0
    path = videos() / ('screenrecording-' + datetime.datetime.now().strftime('%Y-%m-%d_%H-%M-%S-%f') + '.mp4')
    # By full path: Omarchy finds its own recording with `pgrep -f "^gpu-screen-recorder"`,
    # and stops it with the same pattern, so a clip started this way is never taken for it.
    command = [shutil.which(recorder) or recorder, '-w', output, '-k', 'auto', '-f', '60', '-fm', 'cfr', '-fallback-cpu-encoding', 'yes',
               '-a', 'default_output', '-ac', 'aac', '-o', str(path)]
    try:
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    except OSError:
        notify('Recording failed', 'Install gpu-screen-recorder to record clips')
        return 1
    deadline = time.monotonic() + 5
    while process.poll() is None and not path.exists() and time.monotonic() < deadline:
        time.sleep(0.1)
    if process.poll() is not None or not path.exists():
        terminate_recorder(process)
        notify('Recording failed', 'The recorder could not start')
        return 1
    try:
        clip_record.write_text(json.dumps(dict(pid=process.pid, start=process_start(process.pid), path=str(path))))
    except OSError:
        terminate_recorder(process)
        notify('Recording failed', 'The recording state could not be saved')
        return 1
    refresh_indicators()
    return 0


def own_recorder(pid):
    args = (read('/proc/%d/cmdline' % pid) or '').split('\0')
    return args if args and os.path.basename(args[0]) == recorder else None


def record_stop(pid):
    with clip_lock():
        return stop_clip(pid)


def stop_clip(pid):
    clip = locked_clip()
    args = own_recorder(pid) if clip and clip['pid'] == pid else None
    if not args:
        return 1
    path = Path(args[args.index('-o') + 1]) if '-o' in args[:-1] else None
    os.kill(pid, signal.SIGINT)
    deadline = time.monotonic() + 5
    while own_recorder(pid) and process_start(pid) == clip['start'] and time.monotonic() < deadline:
        time.sleep(0.1)
    if own_recorder(pid) and process_start(pid) == clip['start']:
        notify('Recording still stopping', 'Try stopping the clip again')
        return 1
    clip_record.unlink(missing_ok=True)
    refresh_indicators()
    if path and path.is_file() and path.stat().st_size:
        notify('Screen recording saved', path.name)
        return 0
    notify('Recording failed', 'No video was saved')
    return 1


def replay_save(pid):
    args = own_recorder(pid)
    if not args or '-r' not in args:
        return 1
    folder = Path(args[args.index('-o') + 1]) if '-o' in args[:-1] else videos()
    before = {f: f.stat().st_mtime for f in folder.glob('*') if f.is_file()} if folder.is_dir() else {}
    os.kill(pid, signal.SIGUSR1)
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        time.sleep(0.2)
        for f in folder.glob('*') if folder.is_dir() else []:
            try:
                fresh = f.is_file() and f.stat().st_size and f.stat().st_mtime != before.get(f)
            except OSError:
                fresh = False
            if fresh and f.suffix.lower() in ('.mp4', '.mkv', '.webm', '.flv'):
                time.sleep(0.3)
                notify('Replay saved', f.name)
                return 0
    notify('Replay could not be saved', 'The replay buffer did not write a file')
    return 1


command = sys.argv[1] if len(sys.argv) > 1 else 'status'
if command == 'screenshot':
    sys.exit(screenshot(sys.argv[2] if len(sys.argv) > 2 else ''))
elif command == 'record':
    sys.exit(record(sys.argv[2]))
elif command == 'record-stop':
    sys.exit(record_stop(int(sys.argv[2])))
elif command == 'replay-save':
    sys.exit(replay_save(int(sys.argv[2])))
else:
    print(json.dumps(status()))
