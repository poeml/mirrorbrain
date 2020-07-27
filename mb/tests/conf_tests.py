import unittest

from mb.conf import adjust_zsync_block_size_for_1G


class TestConfig(unittest.TestCase):

    def test_adjust_zsync_block_size_for_1G(self):
        cases = {
            0: None,
            1023: None,
            1025: 1024,
            3*1024: 2*1024,
            4*1024: 4*1024,
            4*1024+1: 4*1024,
            1024*1024*1024+1: 1024*1024*1024
        }
        for n in cases:
            self.assertEqual(cases[n], adjust_zsync_block_size_for_1G(n), "for input " + repr(n))

if __name__ == '__main__':
    unittest.main()
