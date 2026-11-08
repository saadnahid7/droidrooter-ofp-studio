"""Development-only tests of the same C workflow linked into the GUI."""
import gzip,hashlib,json,os,struct,subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
DRIVER=Path(os.environ.get('OFP_MAGISK_DRIVER',ROOT/'build'/'magisk-test.exe'))
def cpio(name,data,mode=0o100750):
    name=name.encode()+b'\0';hdr=b'070701'+b''.join(f'{v:08x}'.encode() for v in (1,mode,0,0,1,0,len(data),0,0,0,0,len(name),0))
    entry=hdr+name;entry+=bytes((-len(entry))%4);entry+=data;entry+=bytes((-len(data))%4);return entry
def stock(path):
    ramdisk=gzip.compress(cpio('init',b'original-init')+cpio('TRAILER!!!',b'',0),mtime=0)
    kernel=bytearray(64);kernel[56:60]=b'ARM\x64';header=bytearray(2048);header[:8]=b'ANDROID!';struct.pack_into('<10I',header,8,64,0x10008000,len(ramdisk),0x11000000,0,0,0,2048,0,0)
    data=header+kernel+bytes(2048-64)+ramdisk;data+=bytes((-len(data))%2048);data+=bytes(4*1024*1024-len(data));path.write_bytes(data)
class Magisk(unittest.TestCase):
    def setUp(self):self.tmp=tempfile.TemporaryDirectory();self.p=Path(self.tmp.name);self.src=self.p/'stock.img';self.out=self.p/'magisk_patched.img';stock(self.src)
    def tearDown(self):self.tmp.cleanup()
    def run_tool(self,*extra,preinit='metadata',out=None):return subprocess.run([str(DRIVER),str(self.src),str(out or self.out),preinit,*extra],capture_output=True,text=True)
    def test_patches_and_reports_without_modifying_stock(self):
        original=self.src.read_bytes();r=self.run_tool();self.assertEqual(r.returncode,0,r.stderr);self.assertEqual(self.src.read_bytes(),original)
        b=self.out.read_bytes();self.assertEqual(len(b),len(original));self.assertNotEqual(b,original)
        report=json.loads(Path(str(self.out)+'.provenance.json').read_text());self.assertEqual(report['stock_sha256'],hashlib.sha256(original).hexdigest());self.assertEqual(report['output_sha256'],hashlib.sha256(b).hexdigest());self.assertFalse(report['physically_qualified']);self.assertEqual(report['preinit_evidence'],'user_supplied_not_device_verified')
        k,rsize,second,page=struct.unpack_from('<I',b,8)[0],struct.unpack_from('<I',b,16)[0],struct.unpack_from('<I',b,24)[0],struct.unpack_from('<I',b,36)[0]
        h=hashlib.sha1();pos=page
        for n in (k,rsize,second):h.update(b[pos:pos+n]);h.update(struct.pack('<I',n));pos+=(n+page-1)//page*page
        self.assertEqual(b[576:596],h.digest());self.assertFalse(list(self.p.glob('*.partial')))
    def test_existing_output_preserved(self):self.out.write_bytes(b'keep');self.assertNotEqual(self.run_tool().returncode,0);self.assertEqual(self.out.read_bytes(),b'keep')
    def test_existing_report_preserved(self):p=Path(str(self.out)+'.provenance.json');p.write_bytes(b'keep');self.assertNotEqual(self.run_tool().returncode,0);self.assertEqual(p.read_bytes(),b'keep');self.assertFalse(self.out.exists())
    def test_input_cannot_be_output(self):before=self.src.read_bytes();self.assertNotEqual(self.run_tool(out=self.src).returncode,0);self.assertEqual(self.src.read_bytes(),before)
    def test_invalid_preinit_refused(self):self.assertNotEqual(self.run_tool(preinit='../bad').returncode,0);self.assertFalse(self.out.exists())
    def test_default_offline_patch_omits_preinit_override(self):
        result=self.run_tool(preinit='');self.assertEqual(result.returncode,0,result.stderr)
        report=json.loads(Path(str(self.out)+'.provenance.json').read_text())
        self.assertEqual(report['preinit_evidence'],'omitted_official_offline_mode')
    def test_cancellation_retains_no_published_artifacts(self):
        for mode in ('cancel','cancel-write'):
            with self.subTest(mode=mode):self.assertNotEqual(self.run_tool(mode).returncode,0);self.assertFalse(self.out.exists());self.assertFalse(Path(str(self.out)+'.provenance.json').exists());self.assertFalse(list(self.p.glob('*.partial')))
    def test_unicode_output_path(self):r=self.run_tool(out=self.p/'固件.img');self.assertEqual(r.returncode,0,r.stderr)
if __name__=='__main__':unittest.main(verbosity=2)
