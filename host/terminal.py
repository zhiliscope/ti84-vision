#!/usr/bin/env python3
"""Real CDC hardware client. No simulated transport or AI response."""
import argparse
import binascii
import struct
import time

MAX_PAYLOAD = 240


def encode(kind, seq, payload=b''):
    if len(payload) > MAX_PAYLOAD:
        raise ValueError('payload exceeds 240 bytes')
    body = struct.pack('<BBHH', 1, kind, seq, len(payload)) + payload
    return b'TV' + body + struct.pack('<H', binascii.crc_hqx(body, 0xffff))


class Parser:
    def __init__(self):
        self.buf = bytearray()
        self.errors = 0

    def feed(self, data):
        self.buf.extend(data)
        out = []
        while len(self.buf) >= 2:
            if self.buf[:2] != b'TV':
                del self.buf[0]
                continue
            if len(self.buf) < 8:
                break
            version, kind, seq, n = struct.unpack_from('<BBHH', self.buf, 2)
            if version != 1 or n > MAX_PAYLOAD:
                self.errors += 1
                del self.buf[0]
                continue
            if len(self.buf) < n + 10:
                break
            crc = struct.unpack_from('<H', self.buf, 8 + n)[0]
            if binascii.crc_hqx(self.buf[2:8+n], 0xffff) != crc:
                self.errors += 1
                del self.buf[0]
                continue
            out.append((kind, seq, bytes(self.buf[8:8+n])))
            del self.buf[:n+10]
        return out


class Terminal:
    def __init__(self, port):
        import serial
        self.io = serial.Serial(port, 115200, timeout=0.05, write_timeout=2)
        self.io.dtr = True
        self.io.rts = True
        self.seq = 0

    def request(self, kind, payload=b'', timeout=5):
        self.seq = (self.seq + 1) & 0xffff
        frame = encode(kind, self.seq, payload)
        parser = Parser()
        until = time.monotonic() + timeout
        off = 0
        while off < len(frame):
            if time.monotonic() >= until:
                raise TimeoutError('CDC write timeout')
            off += self.io.write(frame[off:])
        last_byte = time.monotonic()
        while time.monotonic() < until:
            data = self.io.read(512)
            if parser.buf and time.monotonic() - last_byte > 2:
                parser.buf.clear()
            if data:
                last_byte = time.monotonic()
            for response, seq, payload in parser.feed(data):
                if seq != self.seq:
                    continue
                if response == 255:
                    raise RuntimeError(payload.decode('ascii', 'replace'))
                if response == kind | 0x80:
                    return payload
        raise TimeoutError('No matching response; request was not retried')

    def result(self):
        data = bytearray()
        total = None
        while True:
            response = self.request(5, struct.pack('<H', len(data)))
            if len(response) < 4:
                raise RuntimeError('Short result header')
            n, offset = struct.unpack_from('<HH', response)
            chunk = response[4:]
            if n > 8192 or offset != len(data) or (total is not None and n != total):
                raise RuntimeError('Invalid result bounds')
            total = n
            if len(data) + len(chunk) > n or (not chunk and len(data) < n):
                raise RuntimeError('Invalid chunk length')
            data.extend(chunk)
            if len(data) == n:
                return data.decode('ascii', 'replace')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('port', help='native CDC /dev/cu.usbmodem...; never CH343')
    p.add_argument('command', choices=['ping', 'status', 'capture', 'ask', 'result', 'corruption', 'jpeg'])
    p.add_argument('--output', help='new file path for JPEG; existing files are not overwritten')
    p.add_argument('--count', type=int, default=1)
    p.add_argument('--prompt', default='Describe this image concisely.')
    args = p.parse_args()
    t = Terminal(args.port)
    try:
        if args.command == 'ping':
            for i in range(args.count):
                start = time.monotonic()
                response = t.request(1, b'PING')
                if response != b'PONG':
                    raise RuntimeError('Unexpected response')
                print(f'{i+1}: PONG {(time.monotonic()-start)*1000:.1f} ms')
        elif args.command == 'corruption':
            bad = bytearray(encode(1, 0xbeef, b'PING'))
            bad[-1] ^= 1
            t.io.write(bad)
            time.sleep(0.1)
            if t.io.read(512):
                raise RuntimeError('Device replied to a corrupt frame')
            if t.request(1, b'PING') != b'PONG':
                raise RuntimeError('Parser failed to recover')
            print('Corrupt frame rejected; next valid PING passed')
        elif args.command == 'jpeg':
            if not args.output:
                p.error('jpeg requires --output FILE')
            image = bytearray(); total = None
            while True:
                response = t.request(6, struct.pack('<I', len(image)))
                if len(response) < 8:
                    raise RuntimeError('Short JPEG header')
                size, offset = struct.unpack_from('<II', response)
                chunk = response[8:]
                if not 4 <= size <= 256*1024 or offset != len(image) or (total is not None and total != size):
                    raise RuntimeError('Invalid JPEG bounds')
                total = size
                if len(image) + len(chunk) > total or not chunk:
                    raise RuntimeError('Invalid JPEG chunk')
                image.extend(chunk)
                if len(image) == total:
                    break
            if image[:2] != b'\xff\xd8' or image[-2:] != b'\xff\xd9':
                raise RuntimeError('Invalid JPEG markers')
            with open(args.output, 'xb') as f:
                f.write(image)
            print(f'Saved real captured JPEG: {len(image)} bytes')
        elif args.command == 'status':
            print(t.request(2).decode('ascii', 'replace'))
        elif args.command == 'capture':
            print(t.request(3, timeout=15).decode('ascii', 'replace'))
        elif args.command == 'ask':
            print(t.request(4, args.prompt.encode('ascii'), timeout=180).decode('ascii', 'replace'))
            print(t.result())
        else:
            print(t.result())
    finally:
        t.io.close()


if __name__ == '__main__':
    main()
