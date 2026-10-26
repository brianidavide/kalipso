# SPDX-License-Identifier: AGPL-3.0-or-later
import unittest
from run import normalized, distance, score

class Metrics(unittest.TestCase):
    def test_edits_and_empty(self):
        self.assertEqual(distance(['a','b','c'],['a','d']),2)
        self.assertEqual(distance([],['a']),1)
        self.assertEqual(distance(['a'],[]),1)
    def test_normalization(self):
        self.assertEqual(normalized('CAFÉ—mañana!'),normalized('cafe\u0301 mañana'))
        self.assertNotEqual(normalized('ano'),normalized('año'))
    def test_cjk_not_fake_words(self):
        report=score('你好 世界','你好世間','zh')
        self.assertEqual(report['cer'],.25)
        self.assertNotIn('wer',report)
    def test_word_rate_and_negative_control(self):
        self.assertEqual(score('one two three','one two three','en')['wer'],0)
        self.assertEqual(score('one two three','deliberately wrong','en')['wer'],1)

if __name__=='__main__':unittest.main()
