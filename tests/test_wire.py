import binascii
import ctypes
import pathlib
import random
import subprocess
import sys
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'host'))
from terminal import encode, Parser


class WireTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        lib = pathlib.Path(cls.tmp.name) / 'wire.dylib'
        subprocess.run(['cc', '-shared', '-fPIC', '-I'+str(ROOT/'common'),
                        str(ROOT/'common/wire.c'), '-o', str(lib)], check=True)
        cls.lib = ctypes.CDLL(str(lib))
        cls.lib.tv_crc.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
        cls.lib.tv_crc.restype = ctypes.c_uint16

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_c_python_crc_agreement(self):
        r = random.Random(84)
        for n in range(247):
            payload = bytes(r.randrange(256) for _ in range(n))
            self.assertEqual(self.lib.tv_crc(payload, n), binascii.crc_hqx(payload, 0xffff))

    def test_golden_ping(self):
        self.assertEqual(encode(1, 1, b'PING').hex(), '545601010100040050494e47cac4')
        self.assertEqual(Parser().feed(encode(1, 1, b'PING')), [(1, 1, b'PING')])

    def test_stream_and_recovery(self):
        r = random.Random(1)
        expected = [(4, i, bytes(r.randrange(256) for _ in range(i))) for i in range(241)]
        stream = b''.join(encode(*x) for x in expected)
        p = Parser(); found = []
        for byte in stream:
            found.extend(p.feed(bytes([byte])))
        self.assertEqual(found, expected)
        bad = bytearray(encode(1, 999, b'PING')); bad[-1] ^= 0x80
        self.assertEqual(Parser().feed(b'garbage' + bad + encode(1, 1, b'PING')), [(1, 1, b'PING')])


if __name__ == '__main__':
    unittest.main()
