"""Removes Resources/Runtime/ (helper, FFmpeg, yt-dlp, QuickJS) from the server zips RunUAT PackagePlugin produces.

  python Tools/trim_server_packages.py

Dedicated servers never start the helper (UTSHostSubsystem isn't created there), so they don't need the runtime
components. Edits the zips in Saved/ArchivedPlugins/Telescreen/ in place; client zips are untouched.
"""
import os
import sys
import zipfile

ARCHIVE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'Saved', 'ArchivedPlugins', 'Telescreen')

JOBS = [
    ('Telescreen-WindowsServer.zip', ['Resources/Runtime/']),
    ('Telescreen-LinuxServer.zip', ['Resources/Runtime/']),
    ('Telescreen.zip', ['WindowsServer/Resources/Runtime/', 'LinuxServer/Resources/Runtime/']),
]


def trim(path, prefixes):
    if not os.path.isfile(path):
        print(f'skip (not found): {path}')
        return
    before = os.path.getsize(path)
    tmp = path + '.trim'
    dropped = 0
    with zipfile.ZipFile(path) as zin, zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as zout:
        for info in zin.infolist():
            if any(info.filename.startswith(p) for p in prefixes):
                dropped += 1
                continue
            zout.writestr(info, zin.read(info.filename))
    os.replace(tmp, path)
    print(f'{os.path.basename(path)}: dropped {dropped} file(s), {before / 1e6:.0f} MB -> {os.path.getsize(path) / 1e6:.0f} MB')


def main():
    archive_dir = os.path.abspath(ARCHIVE_DIR)
    if not os.path.isdir(archive_dir):
        sys.exit(f'{archive_dir} does not exist; package first')
    for name, prefixes in JOBS:
        trim(os.path.join(archive_dir, name), prefixes)


if __name__ == '__main__':
    main()
