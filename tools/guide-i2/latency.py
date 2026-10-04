#!/usr/bin/env python3
"""Run inside omabox with --guide-input-test; measure receive-to-focus acknowledgments."""
import json
import socket
import subprocess
import time

state = json.loads(subprocess.check_output(['omarchy-shell', 'shell', 'call', 'omakade.guide', 'state', '']))
assert state['opened'] and state['token']
connection = socket.socket(socket.AF_UNIX)
connection.connect(state['socket'])
reader = connection.makefile('rb')
for index in range(120):
    code = 545 if index % 2 == 0 else 544
    for value in (1, 0):
        connection.sendall((json.dumps(dict(version=1, token=state['token'], action='inject', value=dict(type=1, code=code, value=value))) + '\n').encode())
        assert json.loads(reader.readline())['ok']
    time.sleep(.08)
print('120 presses delivered through the native translator at 80 ms intervals.')
