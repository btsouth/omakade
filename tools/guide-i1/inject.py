#!/usr/bin/env python3
"""Inject raw evdev-shaped events into Omakade's translator in an opted-in box run."""
import json
import socket
import subprocess
import sys

state = json.loads(subprocess.check_output(['omarchy-shell', 'shell', 'call', 'omakade.guide', 'state', '']))
token = state['token']
connection = socket.socket(socket.AF_UNIX)
import os
connection.connect(state['socket'])
for code in map(int, sys.argv[1:]):
    for value in (1, 0):
        message = {'version': 1, 'token': token, 'action': 'inject', 'value': {'type': 1, 'code': code, 'value': value}}
        connection.sendall((json.dumps(message) + '\n').encode())
        assert json.loads(connection.makefile('rb').readline())['ok']
# This test writer is not a UI owner; it closes after native processing has acknowledged
# the actions through the shell state. The native service distinguishes it from the UI.
