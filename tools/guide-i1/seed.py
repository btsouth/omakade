#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sqlite3

repo = Path(__file__).resolve().parents[2]
home = Path.home()
config = home / '.config/omakade'
config.mkdir(parents=True, exist_ok=True)
(config / 'sessiond-profiles.json').write_text(json.dumps({
    'romExtensions': ['qml'],
    'emulators': [{'name': 'Manual', 'binaries': ['qml'], 'rescanSource': 'Manual'}]
}))
game = str(repo / 'tools/guide-i1/FullscreenGame.qml')
data = home / '.local/share/omakade'
data.mkdir(parents=True, exist_ok=True)
with sqlite3.connect(data / 'library.sqlite3') as database:
    database.execute('CREATE TABLE IF NOT EXISTS manual_games (id TEXT PRIMARY KEY, entry TEXT NOT NULL, favorite INTEGER NOT NULL DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0, active INTEGER NOT NULL DEFAULT 1)')
    entry = {'id': game, 'title': 'Guide I1 live test', 'executable': '/usr/lib/qt6/bin/qml', 'directory': str(repo), 'arguments': [game]}
    database.execute('INSERT OR REPLACE INTO manual_games(id, entry) VALUES(?, ?)', (game, json.dumps(entry)))
    database.execute('CREATE TABLE IF NOT EXISTS artwork_overrides (source TEXT NOT NULL, runner TEXT NOT NULL, app_id TEXT NOT NULL, cover_path TEXT NOT NULL DEFAULT \'\', hero_path TEXT NOT NULL DEFAULT \'\', logo_path TEXT NOT NULL DEFAULT \'\', PRIMARY KEY(source, runner, app_id))')
    database.execute('INSERT OR REPLACE INTO artwork_overrides(source, runner, app_id, cover_path, hero_path) VALUES(?, ?, ?, ?, ?)', ('Manual', '', game, str(repo / 'tools/guide-overlay-preview/cover.jpg'), str(repo / 'tools/guide-overlay-preview/game.jpg')))
print('Private test library and real recorder profile prepared.')
