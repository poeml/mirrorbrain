import unittest

import mb.hashes


class TestUM(unittest.TestCase):

    def test_HashbagSmall(self):
        hb = mb.hashes.HashBag('tests/data/file1')
        hb.do_zsync_hashes = False
        hb.do_chunked_hashes = True
        hb.do_chunked_with_zsync = False

        hb.chunk_size = mb.hashes.DEFAULT_PIECESIZE
        hb.fill()
        self.assertEqual(hb.dump_raw(), '''piece e5fa44f2b31c1fb553b6021e7360d07d5d91ff5e
md5 b026324c6904b2a9cb4b88d6d61c81d1
sha1 e5fa44f2b31c1fb553b6021e7360d07d5d91ff5e
sha256 4355a46b19d348dc2f57c046f8ef63d4538ebb936000f3c9ee954a27460dd865
btih 90a428db0a7bb9aed0c3df3997ef1882453e9479''')

    def test_Hashbag1M(self):
        f = open('tests/data/file1M', 'w')
        for i in range(1024*104):
            f.write('1')
        f.close()

        hb = mb.hashes.HashBag('tests/data/file1M')
        hb.do_zsync_hashes = False
        hb.do_chunked_hashes = True
        hb.do_chunked_with_zsync = False

        hb.chunk_size = mb.hashes.DEFAULT_PIECESIZE
        hb.fill()
        self.assertEqual(hb.dump_raw(), '''piece 676bfdfe157cf682ea7067d9d8b4f71e560ddd56
md5 169b0ed54597537cb8d841e9a02d7893
sha1 676bfdfe157cf682ea7067d9d8b4f71e560ddd56
sha256 830cd9233e0a2bc65822e0848376a3fc54c1661acc9c743d7e9f09bee9767301
btih 9b299a80ed2048b58f54e4d68ef3436dc6368031''')


if __name__ == '__main__':
    unittest.main()
