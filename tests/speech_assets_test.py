# SPDX-License-Identifier: AGPL-3.0-or-later
"""Verified downloads and confined archive extraction."""
import hashlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from speech_assets import extract,fetch

class Response(io.BytesIO):
    def geturl(self):return 'https://example.test/model'

class Assets(unittest.TestCase):
    def test_checksum_failure_leaves_no_installable_file(self):
        with tempfile.TemporaryDirectory() as temp:
            cache=Path(temp)
            with patch('urllib.request.urlopen',return_value=Response(b'wrong bytes')):
                with self.assertRaisesRegex(ValueError,'checksum'):fetch('https://example.test/model','0'*64,cache)
            self.assertEqual(list(cache.iterdir()),[])
    def test_verified_cache_needs_no_network(self):
        with tempfile.TemporaryDirectory() as temp:
            cache=Path(temp);payload=b'expected model';digest=hashlib.sha256(payload).hexdigest()
            (cache/digest).write_bytes(payload)
            with patch('urllib.request.urlopen',side_effect=AssertionError('network called')):
                self.assertEqual(fetch('https://example.test/model',digest,cache).read_bytes(),payload)
    def test_archive_traversal_rejected_before_any_write(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);archive=root/'bad.zip'
            with zipfile.ZipFile(archive,'w') as package:
                package.writestr('model/valid',b'ok');package.writestr('../escaped',b'bad')
            with self.assertRaisesRegex(ValueError,'Unsafe'):extract(archive,root/'out',lambda _:True)
            self.assertFalse((root/'out').exists());self.assertFalse((root/'escaped').exists())
    def test_native_install_excludes_python_and_sample_audio(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);archive=root/'runtime.whl'
            with zipfile.ZipFile(archive,'w') as package:
                for name in ['pkg/lib.so','pkg/main.py','pkg/test.wav']:package.writestr(name,b'data')
            extract(archive,root/'out',lambda name:name=='pkg/lib.so')
            self.assertTrue((root/'out/pkg/lib.so').exists());self.assertFalse((root/'out/pkg/main.py').exists());self.assertFalse((root/'out/pkg/test.wav').exists())

if __name__=='__main__':unittest.main()
