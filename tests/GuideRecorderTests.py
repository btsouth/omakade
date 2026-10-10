#!/usr/bin/env python3
"""The guide's clip recorder and a screen recording started from Omarchy leave each other alone.

Runs omarchy-plugin/guide-files.py against a fake gpu-screen-recorder that writes its -o
file and exits on SIGINT, the way Omarchy's recorder stops it.
"""
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

HELPER = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else None

# Bash, so the process keeps the recorder's name in its command line as the real one does.
# FAKE_ARGV0 sets that name, as Omarchy's launch by bare name would.
FAKE_RECORDER = r"""#!/bin/bash
for ((i = 1; i < $#; i++)); do
  if [ "${!i}" = -o ]; then j=$((i + 1)); printf video > "${!j}"; fi
done
exec -a "${FAKE_ARGV0:-$0}" python3 -c 'import signal, sys, time
signal.signal(signal.SIGINT, lambda *a: sys.exit(0))
while True: time.sleep(0.05)' "$@"
"""


class GuideRecorderTests(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.TemporaryDirectory()
        root = Path(self.root.name)
        self.bin = root / 'bin'
        self.videos = root / 'videos'
        for path in (self.bin, self.videos):
            path.mkdir()
        recorder = self.bin / 'gpu-screen-recorder'
        recorder.write_text(FAKE_RECORDER)
        recorder.chmod(0o755)
        for name in ('omarchy-notification-send', 'omarchy-shell'):
            (self.bin / name).write_text('#!/bin/sh\nexit 0\n')
            (self.bin / name).chmod(0o755)
        self.env = dict(os.environ, PATH=f'{self.bin}:{os.environ["PATH"]}', HOME=str(root),
                        XDG_STATE_HOME=str(root / 'state'), XDG_CONFIG_HOME=str(root / 'config'),
                        OMARCHY_SCREENRECORD_DIR=str(self.videos))
        self.processes = []

    def tearDown(self):
        subprocess.run(['pkill', '-KILL', '-f', str(self.bin / 'gpu-screen-recorder')], check=False)
        for process in self.processes:
            process.kill()
            process.wait()
        self.root.cleanup()

    def helper(self, *arguments):
        return subprocess.run([sys.executable, '-I', str(HELPER), *arguments], env=self.env,
                              capture_output=True, text=True, timeout=20)

    def status(self):
        return json.loads(self.helper('status').stdout)

    def omarchy_recording(self):
        # Omarchy starts its recorder by name, and finds and stops it with "^gpu-screen-recorder".
        process = subprocess.Popen(['gpu-screen-recorder', '-w', 'TEST-1', '-o', str(self.videos / 'omarchy.mp4')],
                                   env=dict(self.env, FAKE_ARGV0='gpu-screen-recorder'))
        self.processes.append(process)
        deadline = time.monotonic() + 5
        while not (self.videos / 'omarchy.mp4').exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        return process

    def omarchy_pids(self):
        found = subprocess.run(['pgrep', '-f', '^gpu-screen-recorder'], capture_output=True, text=True).stdout
        return {int(pid) for pid in found.split()}

    def test_omarchy_recording_is_not_the_guides(self):
        omarchy = self.omarchy_recording()
        self.assertNotIn('recording', self.status())
        # The guide records its own clip next to it.
        self.assertEqual(self.helper('record', 'TEST-1').returncode, 0)
        clip = self.status()['recording']['pid']
        self.assertNotEqual(clip, omarchy.pid)
        # Omarchy's pattern sees only its own recorder, so its stop leaves the clip alone.
        self.assertEqual(self.omarchy_pids(), {omarchy.pid})
        # The guide stops only its clip, never Omarchy's recording.
        self.assertNotEqual(self.helper('record-stop', str(omarchy.pid)).returncode, 0)
        self.assertIsNone(omarchy.poll())
        self.assertEqual(self.helper('record-stop', str(clip)).returncode, 0)
        self.assertNotIn('recording', self.status())
        self.assertIsNone(omarchy.poll())
        clips = [path for path in self.videos.iterdir() if path.name.startswith('screenrecording-')]
        self.assertEqual(len(clips), 1)


if __name__ == '__main__':
    unittest.main()
