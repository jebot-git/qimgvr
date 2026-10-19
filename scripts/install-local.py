#!/usr/bin/env python3
"""Build all optional features and replace the current user's qimgv launchers."""
import argparse
from datetime import datetime
import os
from pathlib import Path
import shlex
import shutil
import subprocess


def run(*args, **kwargs):
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--skip-build', action='store_true', help='Install an already verified build')
    args = parser.parse_args()
    source = Path(__file__).resolve().parent.parent
    build = source / 'build'
    prefix = Path.home() / '.local/opt/qimgvr'
    if not args.skip_build:
        run('cmake', '-S', source, '-B', build, '-DUSE_QT5=OFF', '-DEXIV2=ON',
            '-DVIDEO_SUPPORT=ON', '-DOPENCV_SUPPORT=ON', '-DKDE_SUPPORT=ON',
            '-DSBS_TESTS=ON', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_INSTALL_LIBDIR=lib',
            f'-DCMAKE_INSTALL_PREFIX={prefix}')
        run('cmake', '--build', build, '--parallel', min(8, os.cpu_count() or 1))
        run('ctest', '--test-dir', build, '--output-on-failure')
    run('cmake', '--install', build)
    executable = prefix / 'bin/qimgvr'
    plugin = prefix / 'lib/qimgvr/player_mpv.so'
    if not executable.is_file() or not plugin.is_file():
        raise RuntimeError('Expected full qimgvr installation and mpv plugin')
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
    features = subprocess.check_output([str(executable), '--build-options'], env=env, text=True)
    for feature in ('USE_MPV', 'USE_EXIV2', 'USE_OPENCV', 'USE_KDE_BLUR'):
        if feature not in features:
            raise RuntimeError(f'Build missing {feature}')
    data = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
    state = Path(os.environ.get('XDG_STATE_HOME', Path.home() / '.local/state'))
    backup = state / 'qimgvr/backups' / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    backup.mkdir(parents=True)
    binaries = Path.home() / '.local/bin'
    applications = data / 'applications'
    binaries.mkdir(parents=True, exist_ok=True)
    applications.mkdir(parents=True, exist_ok=True)
    for command in ('qimgvr', 'qimgv', 'qimgv-sbs'):
        launcher = binaries / command
        if launcher.exists() or launcher.is_symlink():
            shutil.copy2(launcher, backup / command)
        launcher.unlink(missing_ok=True)
        launcher.write_text('#!/bin/sh\nexec ' + shlex.quote(str(executable)) + ' "$@"\n')
        launcher.chmod(0o755)
    desktop = (source / 'qimgv/distrib/qimgvr.desktop').read_text()
    escaped = str(executable).replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$').replace('%', '%%')
    exec_argument = f'"{escaped}"' if any(c.isspace() for c in str(executable)) else escaped
    desktop = desktop.replace('Exec=qimgvr %f', f'Exec={exec_argument} %f')
    desktop = desktop.replace('Icon=qimgv', f'Icon={prefix}/share/icons/hicolor/scalable/apps/qimgv.svg')
    for name in ('qimgvr.desktop', 'qimgv.desktop', 'qimgv-sbs.desktop'):
        target = applications / name
        if target.exists(): shutil.copy2(target, backup / name)
        target.write_text(desktop if name == 'qimgvr.desktop' else desktop + 'NoDisplay=true\n')
    config = Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config'))
    for location, name in ((config / 'mimeapps.list', 'config-mimeapps.list'),
                           (applications / 'mimeapps.list', 'applications-mimeapps.list')):
        if location.exists(): shutil.copy2(location, backup / name)
    if (config / 'qimgv').is_dir(): shutil.copytree(config / 'qimgv', backup / 'settings')
    run('update-desktop-database', applications)
    # Replace existing qimgv associations and register supported still images.
    formats = desktop.split('MimeType=', 1)[1].splitlines()[0].split(';')
    mime_env = dict(os.environ)
    # Fedora installs qtpaths outside PATH; xdg-mime's KDE helper expects it.
    if not shutil.which('qtpaths'):
        qtpaths = Path('/usr/lib64/qt6/bin/qtpaths')
        if qtpaths.exists(): mime_env['PATH'] = str(qtpaths.parent) + os.pathsep + mime_env.get('PATH', '')
    for mime in formats:
        if not mime: continue
        current = subprocess.check_output(['xdg-mime', 'query', 'default', mime], text=True, env=mime_env).strip()
        if mime.startswith('image/') or current in ('qimgv.desktop', 'qimgv-sbs.desktop'):
            run('xdg-mime', 'default', 'qimgvr.desktop', mime, env=mime_env)
    if shutil.which('kbuildsycoca6'):
        run('kbuildsycoca6', '--noincremental')
    print(features, end='')
    print(f'Installed qimgvr and replaced local qimgv commands. Backups: {backup}')
    print('The system RPM can now be removed with: sudo dnf remove --no-autoremove qimgv')


if __name__ == '__main__':
    main()
