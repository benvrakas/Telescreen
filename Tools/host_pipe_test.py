"""Drives TelescreenHost.exe over its pipe like the mod does, to test host mode without the game.

  python Tools/host_pipe_test.py <url-or-file> [seconds]

Starts Helper/out/TelescreenHost.exe, sends Hello, OpenMedia and SyncTarget, and prints every message it gets back.
"""
import os
import struct
import subprocess
import sys
import time

MOD = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(MOD, 'Resources', 'Runtime', 'host', 'TelescreenHost.exe')
FETCHER = os.path.join(MOD, 'Resources', 'Runtime', 'fetcher')
NAMES = {2: 'HelloAck', 7: 'ProbeResult', 8: 'SessionVideo', 9: 'SessionAudio', 10: 'SessionState'}


def message(kind, payload=b''):
    return struct.pack('<IHH', len(payload), kind, 0) + payload


def string(text):
    data = text.encode('utf-8')
    return struct.pack('<I', len(data)) + data


def read_exact(pipe, size):
    data = b''
    while len(data) < size:
        chunk = pipe.read(size - len(data))
        if not chunk:
            raise EOFError
        data += chunk
    return data


def main():
    url = sys.argv[1]
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
    name = rf'\\.\pipe\Telescreen-test-{os.getpid()}'
    proc = subprocess.Popen([HOST, '--pipe', name, '--parent', str(os.getpid()), '--ytdlp', os.path.join(FETCHER, 'yt-dlp.exe'),
                             '--jsruntime', os.path.join(FETCHER, 'qjs.exe')], stderr=subprocess.DEVNULL)
    for _ in range(100):
        try:
            pipe = open(name, 'r+b', buffering=0)
            break
        except OSError:
            time.sleep(0.1)
    else:
        sys.exit('could not connect')
    pipe.write(message(1, struct.pack('<I', 1)))
    local = '://' not in url
    pipe.write(message(3, struct.pack('<I', 7) + string(url) + struct.pack('<BdBII', int(local), 5.0, 0, 1280, 720)))
    pipe.write(message(6, struct.pack('<I', 1) + string(url)))
    start = time.time()
    seen = {}
    while time.time() - start < seconds:
        size, kind, _ = struct.unpack('<IHH', read_exact(pipe, 8))
        payload = read_exact(pipe, size)
        seen[kind] = seen.get(kind, 0) + 1
        if kind == 10:
            session, state = struct.unpack_from('<IB', payload)
            position, duration = struct.unpack_from('<dd', payload, 5)
            if seen[kind] <= 3 or seen[kind] % 5 == 0:
                print(f'{time.time() - start:5.1f}s SessionState session={session} state={state} pos={position:.2f} dur={duration:.0f}')
        else:
            print(f'{time.time() - start:5.1f}s {NAMES.get(kind, kind)} ({size} bytes) {payload[:120]!r}')
        pipe.write(message(4, struct.pack('<IdB', 7, 5.0 + time.time() - start, 0)))
    print('message counts:', {NAMES.get(k, k): v for k, v in seen.items()})
    pipe.write(message(11))
    pipe.close()
    proc.wait(timeout=5)
    print('host exit code', proc.returncode)


if __name__ == '__main__':
    main()
