"""A tiny RFB (VNC) server for testing Telescreen's VNC client without a real VNC server.

  python Tools/vnc_test_server.py [port] [password] [encoding] [static]

Listens on 127.0.0.1:port (default 5905), speaks RFB 3.8 with security None (no password given) or VNC
Authentication, and sends a moving test pattern with the chosen encoding (raw, hextile or zrle; default zrle), a
DesktopSize change after 3 seconds and a Cursor shape when the client asks for those. Prints the pointer and key
events it receives. One client at a time. "static" keeps the pattern still and the size fixed (for comparing a
decoded frame with pattern(320, 200, 0)).
"""
import socket
import struct
import sys
import time
import zlib

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 5905
PASSWORD = sys.argv[2] if len(sys.argv) > 2 else ''
ENCODING = sys.argv[3] if len(sys.argv) > 3 else 'zrle'
ENC = {'raw': 0, 'hextile': 5, 'zrle': 16}[ENCODING]
STATIC = len(sys.argv) > 4 and sys.argv[4] == 'static'


def recv_exact(sock, n):
    data = b''
    while len(data) < n:
        part = sock.recv(n - len(data))
        if not part:
            raise EOFError
        data += part
    return data


def des_key(password):
    key = bytearray(8)
    for i, ch in enumerate(password.encode('latin-1')[:8]):
        key[i] = int('{:08b}'.format(ch)[::-1], 2)
    return bytes(key)


def des_ecb_encrypt(key, data):
    # DES through Windows CNG (bcrypt), so the test needs nothing installed.
    import ctypes
    from ctypes import wintypes
    bcrypt = ctypes.WinDLL('bcrypt')
    alg = wintypes.HANDLE()
    assert bcrypt.BCryptOpenAlgorithmProvider(ctypes.byref(alg), 'DES', None, 0) == 0
    mode = ctypes.create_unicode_buffer('ChainingModeECB')
    assert bcrypt.BCryptSetProperty(alg, 'ChainingMode', mode, ctypes.sizeof(mode), 0) == 0
    hkey = wintypes.HANDLE()
    assert bcrypt.BCryptGenerateSymmetricKey(alg, ctypes.byref(hkey), None, 0, key, len(key), 0) == 0
    out = ctypes.create_string_buffer(len(data))
    done = wintypes.ULONG()
    assert bcrypt.BCryptEncrypt(hkey, data, len(data), None, None, 0, out, len(data), ctypes.byref(done), 0) == 0
    bcrypt.BCryptDestroyKey(hkey)
    bcrypt.BCryptCloseAlgorithmProvider(alg, 0)
    return out.raw


def pattern(width, height, t):
    """BGRX pixels: a diagonal gradient with a moving white bar."""
    bar = int(t * 120) % width
    rows = []
    for y in range(height):
        row = bytearray(width * 4)
        for x in range(width):
            if bar <= x < bar + 12:
                b = g = r = 255
            else:
                b, g, r = (x * 255 // width), (y * 255 // height), ((x + y) * 255 // (width + height))
            row[x * 4:x * 4 + 4] = bytes((b, g, r, 0))
        rows.append(bytes(row))
    return rows


def encode_raw(rows, x, y, w, h):
    return b''.join(rows[y + j][x * 4:(x + w) * 4] for j in range(h))


def encode_hextile(rows, x, y, w, h):
    out = bytearray()
    for ty in range(y, y + h, 16):
        for tx in range(x, x + w, 16):
            tw, th = min(16, x + w - tx), min(16, y + h - ty)
            out.append(1)  # raw tile
            out += encode_raw(rows, tx, ty, tw, th)
    return bytes(out)


class ZrleEncoder:
    def __init__(self):
        self.z = zlib.compressobj()

    def encode(self, rows, x, y, w, h):
        raw = bytearray()
        for ty in range(y, y + h, 64):
            for tx in range(x, x + w, 64):
                tw, th = min(64, x + w - tx), min(64, y + h - ty)
                pixels = [rows[ty + j][(tx + i) * 4:(tx + i) * 4 + 3] for j in range(th) for i in range(tw)]
                distinct = list(dict.fromkeys(pixels))
                if len(distinct) == 1:
                    raw.append(1)
                    raw += distinct[0]
                elif len(distinct) <= 16:
                    raw.append(len(distinct))
                    for p in distinct:
                        raw += p
                    bits = 1 if len(distinct) == 2 else 2 if len(distinct) <= 4 else 4
                    index = {p: i for i, p in enumerate(distinct)}
                    for j in range(th):
                        acc, nbits, line = 0, 0, bytearray()
                        for i in range(tw):
                            acc = (acc << bits) | index[pixels[j * tw + i]]
                            nbits += bits
                            if nbits == 8:
                                line.append(acc)
                                acc, nbits = 0, 0
                        if nbits:
                            line.append(acc << (8 - nbits))
                        raw += line
                else:
                    # plain RLE
                    raw.append(128)
                    i = 0
                    while i < len(pixels):
                        run = 1
                        while i + run < len(pixels) and pixels[i + run] == pixels[i]:
                            run += 1
                        raw += pixels[i]
                        n = run - 1
                        while n >= 255:
                            raw.append(255)
                            n -= 255
                        raw.append(n)
                        i += run
        data = self.z.compress(bytes(raw)) + self.z.flush(zlib.Z_SYNC_FLUSH)
        return struct.pack('>I', len(data)) + data


def serve(conn):
    conn.sendall(b'RFB 003.008\n')
    version = recv_exact(conn, 12)
    print('client version', version)
    if PASSWORD:
        conn.sendall(bytes([1, 2]))
        assert recv_exact(conn, 1)[0] == 2
        challenge = bytes(range(16))
        conn.sendall(challenge)
        response = recv_exact(conn, 16)
        expected = des_ecb_encrypt(des_key(PASSWORD), challenge)
        ok = response == expected
        print('password', 'accepted' if ok else 'REFUSED')
        if not ok:
            reason = b'wrong password'
            conn.sendall(struct.pack('>II', 1, len(reason)) + reason)
            return
        conn.sendall(struct.pack('>I', 0))
    else:
        conn.sendall(bytes([1, 1]))
        assert recv_exact(conn, 1)[0] == 1
        conn.sendall(struct.pack('>I', 0))
    recv_exact(conn, 1)  # ClientInit
    width, height = 320, 200
    name = b'Telescreen test server'
    pixfmt = struct.pack('>BBBBHHHBBBxxx', 32, 24, 0, 1, 255, 255, 255, 16, 8, 0)
    conn.sendall(struct.pack('>HH', width, height) + pixfmt + struct.pack('>I', len(name)) + name)
    encodings, start, resized, cursor_sent = [], time.time(), False, False
    zrle = ZrleEncoder()
    conn.settimeout(0.05)
    frames = 0
    while True:
        try:
            kind = recv_exact(conn, 1)[0]
        except socket.timeout:
            continue
        if kind == 0:
            recv_exact(conn, 19)
        elif kind == 2:
            count = struct.unpack('>xH', recv_exact(conn, 3))[0]
            encodings = list(struct.unpack('>%di' % count, recv_exact(conn, 4 * count)))
            print('client encodings', encodings)
        elif kind == 3:
            incremental, x, y, w, h = struct.unpack('>BHHHH', recv_exact(conn, 9))
            time.sleep(1 / 30)
            t = 0.0 if STATIC else time.time() - start
            rects = []
            if not resized and not STATIC and t > 3 and -223 in encodings:
                width, height, resized = 400, 240, True
                rects.append((0, 0, width, height, -223, b''))
                print('desktop resized to', width, height)
            if not cursor_sent and not STATIC and -239 in encodings:
                cw, ch = 8, 8
                pixels = bytes((0, 0, 255, 0)) * (cw * ch)
                mask = bytes([0xFF] * ch)
                rects.append((2, 2, cw, ch, -239, pixels + mask))
                cursor_sent = True
            rows = pattern(width, height, t)
            body = {0: encode_raw, 5: encode_hextile}.get(ENC, zrle.encode)(rows, 0, 0, width, height)
            rects.append((0, 0, width, height, ENC, body))
            out = struct.pack('>BxH', 0, len(rects))
            for (rx, ry, rw, rh, enc, data) in rects:
                out += struct.pack('>HHHHi', rx, ry, rw, rh, enc) + data
            conn.sendall(out)
            frames += 1
            if frames % 30 == 0:
                print('sent', frames, 'updates')
        elif kind == 4:
            down, keysym = struct.unpack('>BxxI', recv_exact(conn, 7))
            print('key', hex(keysym), 'down' if down else 'up')
        elif kind == 5:
            mask, x, y = struct.unpack('>BHH', recv_exact(conn, 5))
            print('pointer', x, y, 'buttons', mask)
        elif kind == 6:
            length = struct.unpack('>xxxI', recv_exact(conn, 7))[0]
            recv_exact(conn, length)
        elif kind == 255:
            recv_exact(conn, 1)
            op = struct.unpack('>H', recv_exact(conn, 2))[0]
            if op == 2:
                recv_exact(conn, 6)
            print('qemu audio op', op)
        else:
            print('unknown client message', kind)
            return


def main():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('127.0.0.1', PORT))
    server.listen(1)
    print('listening on 127.0.0.1:%d (%s, %s)' % (PORT, 'password' if PASSWORD else 'no password', ENCODING))
    while True:
        conn, addr = server.accept()
        print('client', addr)
        try:
            serve(conn)
        except (EOFError, ConnectionError) as e:
            print('client gone', e)
        finally:
            conn.close()


if __name__ == '__main__':
    main()
