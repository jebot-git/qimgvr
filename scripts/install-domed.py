#!/usr/bin/env python3
"""Opt in/out of qimgvr as DomeD's image opener without changing other desktops."""
import argparse
import configparser
import json
import os
from pathlib import Path
import shutil
import subprocess

MIMES = ('image/jpeg', 'image/png', 'image/gif', 'image/webp', 'image/bmp', 'image/tiff',
         'image/avif', 'image/heif', 'image/heic', 'image/jxl', 'image/svg+xml', 'image/x-jps')


def configure(config, data, state, executable, disable=False):
    associations = config / 'dome-mimeapps.list'
    desktop = data / 'applications/qimgvr-domed.desktop'
    saved = state / 'qimgvr/domed-integration.json'
    settings = configparser.ConfigParser(interpolation=None)
    settings.optionxform = str
    if associations.exists(): settings.read(associations)
    if not settings.has_section('Default Applications'): settings.add_section('Default Applications')
    defaults = settings['Default Applications']
    if disable:
        if not saved.exists(): return
        previous = json.loads(saved.read_text())
        for mime, value in previous['associations'].items():
            if defaults.get(mime) == 'qimgvr-domed.desktop;':
                if value is None: defaults.pop(mime, None)
                else: defaults[mime] = value
        if previous['desktop'] is None: desktop.unlink(missing_ok=True)
        else: desktop.write_text(previous['desktop'])
        saved.unlink()
    else:
        if not Path(executable).is_file(): raise RuntimeError(f'Install qimgvr first: {executable}')
        if not saved.exists():
            saved.parent.mkdir(parents=True, exist_ok=True)
            with saved.open('w') as output:
                json.dump({'associations': {mime: defaults.get(mime) for mime in MIMES},
                           'desktop': desktop.read_text() if desktop.exists() else None}, output, indent=2)
            saved.chmod(0o600)
        for mime in MIMES: defaults[mime] = 'qimgvr-domed.desktop;'
        argument = str(executable).replace('\\', '\\\\').replace('"', '\\"').replace('%', '%%').replace('`', '\\`').replace('$', '\\$')
        if any(c.isspace() for c in str(executable)): argument = '"' + argument + '"'
        icon = Path(executable).parent.parent / 'share/icons/hicolor/scalable/apps/qimgv.svg'
        icon = str(icon) if icon.is_file() else 'image-x-generic'
        desktop.parent.mkdir(parents=True, exist_ok=True)
        desktop.write_text('[Desktop Entry]\nType=Application\nName=qimgvr (DomeD)\n'
            'Comment=Open images and SBS stereo in DomeD’s XR session\n'
            f'Exec={argument} --domed %f\nIcon={icon}\nTerminal=false\nNoDisplay=true\n'
            'Categories=Graphics;Viewer;\nMimeType=' + ';'.join(MIMES) + ';\n')
    associations.parent.mkdir(parents=True, exist_ok=True)
    with associations.open('w') as output:
        # No spaces around '=': xdg-utils' MIME reader expects this form.
        settings.write(output, space_around_delimiters=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disable', action='store_true', help='Restore prior DomeD image defaults')
    parser.add_argument('--executable', type=Path, default=Path.home() / '.local/opt/qimgvr/bin/qimgvr')
    args = parser.parse_args()
    config = Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config'))
    data = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
    state = Path(os.environ.get('XDG_STATE_HOME', Path.home() / '.local/state'))
    configure(config, data, state, args.executable, args.disable)
    if shutil.which('update-desktop-database'):
        subprocess.run(['update-desktop-database', str(data / 'applications')], check=True)
    print('DomeD image opener ' + ('restored.' if args.disable else 'enabled: qimgvr --domed.'))


if __name__ == '__main__':
    main()
