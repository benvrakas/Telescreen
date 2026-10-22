"""Tests the live stream path like the mod runs it, without the game.

  python Tools/stream_relay_test.py [url] [seconds] [loss-percent]

Starts the staged TelescreenHost.exe and opens a browser session, a stream sender for it and a stream receiver over the
pipe. Sender packets are cut into the mod's 768-byte chunks, chunks are dropped at random (the network), and the rest
are reassembled and passed to the receiver with the mod's loss handling: on a lost video packet the receiver is reset
to wait for a keyframe, and a keyframe is asked from the sender right away, then every 0.75 s until one arrives.
"""
import os
import random
import struct
import subprocess
import sys
import tempfile
import time

MOD = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNTIME = os.path.join(MOD, 'Resources', 'Runtime')
HOST = os.path.join(RUNTIME, 'host', 'TelescreenHost.exe')

HELLO, SESSION_VIDEO, SESSION_AUDIO, SESSION_STATE, SHUTDOWN = 1, 8, 9, 10, 11
OPEN_BROWSER, OPEN_SENDER, OPEN_RECEIVER, STREAM_PACKET, STREAM_CONTROL = 12, 17, 18, 19, 20
CONTROL_KEYFRAME, CONTROL_RESET = 0, 2
BROWSER, SENDER, RECEIVER = 1, 2, 3
CHUNK = 768
RETRY = 0.75


def message(kind, payload=b''):
    return struct.pack('<IHH', len(payload), kind, 0) + payload


def string(text):
    data = text.encode('utf-8')
    return struct.pack('<I', len(data)) + data


def read_exact(pipe, size):
    data = b''
    while len(data) < size:
        part = pipe.read(size - len(data))
        if not part:
            raise EOFError
        data += part
    return data


class Relay:
    """The mod's chunking, reassembly and keyframe recovery (UTSScreenPlayer)."""

    def __init__(self, pipe, loss):
        self.pipe = pipe
        self.loss = loss
        self.next_id = [0, 0]
        self.partial = [None, None]
        self.last_id = [None, None]
        self.awaiting = False
        self.last_request = 0.0
        self.stats = dict(packets=0, chunks=0, dropped_chunks=0, delivered=0, lost_packets=0, keyframe_requests=0, keyframes=0)

    def control(self, session, command):
        self.pipe.write(message(STREAM_CONTROL, struct.pack('<IBI', session, command, 0)))

    def request_keyframe(self, force):
        new_loss = not self.awaiting
        if new_loss:
            self.control(RECEIVER, CONTROL_RESET)
            self.awaiting = True
        now = time.time()
        if not force and not new_loss and now - self.last_request < RETRY:
            return
        self.last_request = now
        self.stats['keyframe_requests'] += 1
        self.control(SENDER, CONTROL_KEYFRAME)

    def send(self, kind, flags, pts, codec, data):
        packet_id = self.next_id[kind]
        self.next_id[kind] += 1
        self.stats['packets'] += 1
        count = max(1, (len(data) + CHUNK - 1) // CHUNK)
        for index in range(count):
            self.stats['chunks'] += 1
            if random.random() * 100.0 < self.loss:
                self.stats['dropped_chunks'] += 1
                continue
            self.receive(kind, flags, packet_id, index, count, pts, codec, data[index * CHUNK:(index + 1) * CHUNK])

    def receive(self, kind, flags, packet_id, index, count, pts, codec, part):
        p = self.partial[kind]
        if p and packet_id < p['id']:
            return
        if not p or packet_id != p['id']:
            lost_partial = p is not None and p['received'] < len(p['parts'])
            gap = self.last_id[kind] is not None and packet_id - self.last_id[kind] > 1
            if (lost_partial or gap) and kind == 0:
                self.stats['lost_packets'] += 1
                self.request_keyframe(False)
            p = self.partial[kind] = dict(id=packet_id, flags=flags, pts=pts, codec=codec, parts=[None] * count, received=0)
        if count != len(p['parts']) or p['parts'][index] is not None:
            return
        p['parts'][index] = part
        p['received'] += 1
        if p['received'] < count:
            return
        if kind == 0 and flags & 1:
            self.awaiting = False
            self.stats['keyframes'] += 1
        payload = struct.pack('<IBBII', RECEIVER, kind, p['flags'], p['pts'], p['codec']) + b''.join(p['parts'])
        self.pipe.write(message(STREAM_PACKET, payload))
        self.stats['delivered'] += 1
        self.last_id[kind] = packet_id
        self.partial[kind] = None

    def tick(self):
        if self.awaiting and time.time() - self.last_request >= RETRY:
            self.request_keyframe(False)


def main():
    url = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] else 'https://www.youtube.com/embed/aqz-KE-bpKQ?autoplay=1'
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0
    loss = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
    random.seed(7)
    data_dir = tempfile.mkdtemp(prefix='telescreen-relay-')
    name = rf'\\.\pipe\Telescreen-relay-{os.getpid()}'
    log = os.path.join(data_dir, 'host.log')
    proc = subprocess.Popen([HOST, '--pipe', name, '--parent', str(os.getpid()), '--log', log, '--browserdir', os.path.join(RUNTIME, 'browser'),
                             '--browserdata', data_dir], stderr=subprocess.DEVNULL)
    for _ in range(100):
        try:
            pipe = open(name, 'r+b', buffering=0)
            break
        except OSError:
            time.sleep(0.1)
    else:
        sys.exit('could not connect')
    pipe.write(message(HELLO, struct.pack('<I', 2)))
    pipe.write(message(OPEN_BROWSER, struct.pack('<I', BROWSER) + string(url) + struct.pack('<IIB', 1280, 720, 0) + string('')))
    time.sleep(2.0)
    pipe.write(message(OPEN_RECEIVER, struct.pack('<I', RECEIVER)))
    pipe.write(message(OPEN_SENDER, struct.pack('<IIIIB', SENDER, BROWSER, 30, 2500, 1) + string('')))
    relay = Relay(pipe, loss)
    relay.request_keyframe(True)
    start = time.time()
    receiver_states = []
    sender_states = []
    while time.time() - start < seconds:
        try:
            size, kind, _ = struct.unpack('<IHH', read_exact(pipe, 8))
            payload = read_exact(pipe, size)
        except EOFError:
            print(f'{time.time() - start:5.1f}s pipe closed; host exit code {proc.wait(timeout=10):#x}, log {log}')
            print('relay:', relay.stats)
            sys.exit(1)
        if kind == STREAM_PACKET:
            session, packet_kind, flags, pts, codec = struct.unpack_from('<IBBII', payload)
            if session == SENDER:
                relay.send(packet_kind, flags, pts, codec, payload[14:])
        elif kind == SESSION_STATE:
            session, state = struct.unpack_from('<IB', payload)
            (length,) = struct.unpack_from('<I', payload, 21)
            text = payload[25:25 + length].decode('utf-8', 'replace')
            (receiver_states if session == RECEIVER else sender_states).append((round(time.time() - start, 1), state, text))
        elif kind in (SESSION_VIDEO, SESSION_AUDIO):
            print(f'{time.time() - start:5.1f}s session {struct.unpack_from("<I", payload)[0]} {"video" if kind == SESSION_VIDEO else "audio"}')
        relay.tick()
    pipe.write(message(SHUTDOWN))
    pipe.close()
    proc.wait(timeout=10)
    print('sender:', sender_states[-1] if sender_states else None)
    for line in [s for s in receiver_states if s[2]][-4:]:
        print('receiver:', line)
    print('relay:', relay.stats)
    print('host exit code', proc.returncode, 'log', log)


if __name__ == '__main__':
    main()
