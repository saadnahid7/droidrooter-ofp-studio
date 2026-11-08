"""Development-only behavioral tests. Users do not need Python.
Changing decryption, bounds, path validation or output handling must break these tests.
Fixtures use PyCryptodome independently of Windows CNG.
"""
import hashlib, json, os, struct, subprocess, tempfile, unittest
from pathlib import Path
from Cryptodome.Cipher import AES

ROOT=Path(__file__).resolve().parents[1]
DRIVER=Path(os.environ.get('OFP_TEST_DRIVER',ROOT/'build'/'ofp-test.exe'))
KEY=b'ab3f76d7989207f2'; IV=b'2bf515b3a9737835'

def mtk(path, entries, key=KEY, iv=IV):
    data=bytearray(); table=bytearray()
    for item in entries:
        name,plain,encrypted=item[:3];label=item[3] if len(item)>3 else name
        start=len(data)
        data+=AES.new(key,AES.MODE_CFB,iv=iv,segment_size=128).encrypt(plain[:encrypted])+plain[encrypted:]
        table+=struct.pack('<32sQQQ32sQ',label.encode()[:31],start,len(plain),encrypted,name.encode(),0)
    header=struct.pack('46sQ4s7s5sH32sH',b'test',0,b'',b'6771',b'emmc',len(entries),b'fixture',0)
    def shuffle(b):
        return bytes((((v^b'geyixue'[i%7])&15)<<4)|((v^b'geyixue'[i%7])>>4) for i,v in enumerate(b))
    path.write_bytes(data+shuffle(table)+shuffle(header))

def qc(path, page=512, bad_hash=False, xml_override=None):
    mask=bytes.fromhex('27827963787265EF89D126B69A495A21')
    def derive(s):
        x=bytes(a^b for a,b in zip(bytes.fromhex(s),mask))
        return hashlib.md5(bytes((v>>4)|((v&15)<<4) for v in x)).hexdigest()[:16].encode()
    key=derive('82C50203285A2CE7D8C3E198383CE94C'); iv=derive('422DD5399181E223813CD8ECDF2E4D72')
    plain=b'ANDROID!'+b'QC-fixture'*71
    sha='0'*64 if bad_hash else hashlib.sha256(plain).hexdigest()
    xml=xml_override or (f'<?xml version="1.0"?><Profile><Firmware><File Path="raw.xml" FileOffsetInSrc="1" SizeInByteInSrc="{len(plain)}" SizeInSectorInSrc="2" sha256="{sha}"/></Firmware><Sahara><File Path="boot.img" FileOffsetInSrc="3" SizeInByteInSrc="{len(plain)}" SizeInSectorInSrc="2" sha256="{hashlib.sha256(plain).hexdigest()}"/></Sahara></Profile>').encode()
    data=bytearray(page*8)
    data[page:page+len(plain)]=plain
    enc=AES.new(key,AES.MODE_CFB,iv=iv,segment_size=128).encrypt(plain)
    data[3*page:3*page+len(enc)]=enc
    data[5*page:5*page+len(xml)]=AES.new(key,AES.MODE_CFB,iv=iv,segment_size=128).encrypt(xml)
    struct.pack_into('<III',data,len(data)-page+16,0x7cef,5,len(xml))
    path.write_bytes(data)
    return plain

class Engine(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.p=Path(self.tmp.name);self.src=self.p/'test.ofp';self.out=self.p/'out'
    def tearDown(self):self.tmp.cleanup()
    def run_tool(self,*args):return subprocess.run([str(DRIVER),str(self.src),*map(str,args)],capture_output=True,text=True)
    def make(self,tail=b'payload'*173):
        self.boot=b'MMM'+bytes(range(256))*17
        mtk(self.src,[('boot.img',self.boot,257),('vendor.img',tail,0)])
    def test_mtk_streaming_encrypted_and_plain(self):
        self.make();r=self.run_tool(self.out);self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual((self.out/'boot.img').read_bytes(),self.boot)
        self.assertEqual((self.out/'vendor.img').read_bytes(),b'payload'*173)
        self.assertIn('boot.img',(self.out/'SHA256SUMS.txt').read_text())
    def test_selection_unicode_path(self):
        self.make();out=self.p/'固件-é';r=self.run_tool(out,'vendor.img');self.assertEqual(r.returncode,0,r.stderr)
        self.assertFalse((out/'boot.img').exists());self.assertTrue((out/'vendor.img').exists())
    def test_existing_file_preserved(self):
        self.make();self.out.mkdir();(self.out/'boot.img').write_bytes(b'original')
        self.assertNotEqual(self.run_tool(self.out).returncode,0);self.assertEqual((self.out/'boot.img').read_bytes(),b'original')
        self.assertFalse((self.out/'vendor.img').exists())
    def test_traversal_rejected(self):
        mtk(self.src,[('boot.img',b'MMM'+b'x'*30,16),('../escape.img',b'bad',0)])
        self.assertNotEqual(self.run_tool(self.out).returncode,0);self.assertFalse((self.p/'escape.img').exists())
    def test_device_name_rejected(self):
        mtk(self.src,[('boot.img',b'MMM'+b'x'*30,16),('CON.img',b'bad',0)])
        self.assertNotEqual(self.run_tool().returncode,0)
    def test_windows_path_aliases_rejected(self):
        for name in ('COM0.bin','LPT1.img','name:stream','file.','dir//file','/absolute','dir/../escape','NUL.txt'):
            with self.subTest(name=name):
                mtk(self.src,[('boot.img',b'MMM'+b'x'*30,16),(name,b'bad',0)])
                self.assertNotEqual(self.run_tool().returncode,0)
    def test_conflicting_file_and_directory_rejected_before_output(self):
        mtk(self.src,[('boot.img',b'MMM'+b'x'*30,16),('folder',b'a',0),('folder/child.img',b'b',0)])
        self.assertNotEqual(self.run_tool(self.out).returncode,0)
        self.assertFalse((self.out/'boot.img').exists())
    def test_destination_junction_rejected(self):
        self.make();outside=self.p/'outside';outside.mkdir()
        link=subprocess.run(['cmd','/c','mklink','/J',str(self.out),str(outside)],capture_output=True)
        self.assertEqual(link.returncode,0,link.stderr)
        self.assertNotEqual(self.run_tool(self.out).returncode,0)
        self.assertFalse(list(outside.iterdir()))
    def test_nested_directories(self):
        mtk(self.src,[('boot.img',b'MMM'+b'x'*30,16),('firmware/dsp.bin',b'nested-output',0)])
        r=self.run_tool(self.out);self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual((self.out/'firmware/dsp.bin').read_bytes(),b'nested-output')
    def test_all_mtk_key_profiles(self):
        import re
        section=(ROOT/'src/keys.h').read_text().split('static const char *qc_keys')[0]
        for i,line in enumerate(re.findall(r'\{([^{}]+)\}',section)):
            keys=re.findall(r'"([^"]+)"',line)
            if len(keys)==3:
                mask=bytes.fromhex(keys[0])
                def derive(v):
                    values=bytes(a^b for a,b in zip(bytes.fromhex(v),mask))
                    return hashlib.md5(bytes((x>>4)|((x&15)<<4) for x in values)).hexdigest()[:16].encode()
                key,iv=derive(keys[1]),derive(keys[2])
            else:key,iv=(v.encode() for v in keys)
            with self.subTest(profile=i):
                plain=b'MMM'+b'profile-test'*123
                mtk(self.src,[('boot.img',plain,len(plain))],key,iv)
                out=self.p/f'key{i}';r=self.run_tool(out);self.assertEqual(r.returncode,0,r.stderr)
                self.assertEqual((out/'boot.img').read_bytes(),plain)
    def test_truncated_table_rejected(self):
        self.make();b=self.src.read_bytes();self.src.write_bytes(b[:50]+b[-108:]);self.assertNotEqual(self.run_tool().returncode,0)
    def test_unknown_key_rejected(self):
        mtk(self.src,[('boot.img',b'MMM'+b'x'*30,16)],b'x'*16,b'y'*16);self.assertNotEqual(self.run_tool().returncode,0)
    def test_identical_duplicate_collapsed(self):
        b=b'MMM'+b'x'*50;mtk(self.src,[('boot.img',b,16),('boot.img',b,16)])
        r=self.run_tool(self.out);self.assertEqual(r.returncode,0,r.stderr);self.assertIn('files=1 logical=2',r.stdout)
    def test_conflicting_duplicate_rejected(self):
        mtk(self.src,[('boot.img',b'MMM'+b'x'*50,16),('boot.img',b'MMM'+b'y'*50,16)])
        self.assertNotEqual(self.run_tool().returncode,0)
    def test_cancel_removes_partial_and_manifest(self):
        self.make(b'x'*(9*1024*1024));r=self.run_tool(self.out,'cancel');self.assertNotEqual(r.returncode,0)
        self.assertFalse(list(self.out.glob('*.partial')));self.assertFalse((self.out/'SHA256SUMS.txt').exists())
    def test_qc_both_pages_copy_and_decrypt(self):
        for page in (512,4096):
            with self.subTest(page=page):
                plain=qc(self.src,page);out=self.p/str(page);r=self.run_tool(out)
                self.assertEqual(r.returncode,0,r.stderr);self.assertEqual((out/'raw.xml').read_bytes(),plain);self.assertEqual((out/'boot.img').read_bytes(),plain)
    def test_qc_bad_vendor_hash_fails(self):
        qc(self.src,bad_hash=True);r=self.run_tool(self.out);self.assertNotEqual(r.returncode,0);self.assertFalse((self.out/'raw.xml').exists())
    def test_qc_dtd_rejected(self):
        qc(self.src,xml_override=b'<?xml version="1.0"?><!DOCTYPE x [<!ENTITY x SYSTEM "file:///C:/Windows/win.ini">]><Profile>&x;</Profile>'+b' '*200)
        self.assertNotEqual(self.run_tool().returncode,0)
    def test_zip_and_ops_explicitly_rejected(self):
        for magic in (b'PK\x03\x04',b'OPS!'):
            self.src.write_bytes(magic+b'0'*2048);self.assertNotEqual(self.run_tool().returncode,0)
    def scatter_fixture(self,broken=False,missing=False):
        scatter=b'''- general: MTK_PLATFORM_CFG
  info:
    - config_version: V2.1.0
      platform: MT6771
      project: oppo6771
- storage_type: EMMC
  description:
  - general: MTK_STORAGE_CFG
    info:
      - storage: EMMC
        boot_channel: MSDC_0
        block_size: 0x20000
  - partition_index: SYS0
    partition_name: preloader
    file_name: boot.img
    is_download: true
    type: NORMAL_ROM
    linear_start_addr: 0x0
    physical_start_addr: 0x0
    partition_size: 0x1000
    region: EMMC_BOOT1_BOOT2
    storage: HW_STORAGE_EMMC
    boundary_check: true
    is_reserved: false
    operation_type: BOOTLOADERS
    reserve: 0x00
  - partition_index: SYS1
    partition_name: system
    file_name: system.img
    is_download: true
    type: EXT4_IMG
    linear_start_addr: 0x1000
    physical_start_addr: 0x1000
    partition_size: 0x2000
    region: EMMC_USER
    storage: HW_STORAGE_EMMC
    boundary_check: true
    is_reserved: false
    operation_type: UPDATE
    reserve: 0x00
- storage_type: UFS
'''
        sparse=struct.pack('<I4H4I',0xed26ff3a,1,0,28,12,4096,3 if broken else 1,1,0)+struct.pack('<2H2I',0xcac1,0,1,4108)+b'x'*4096
        entries=[('boot.img',b'MMM'+b'b'*50,16),('MT6771_Android_scatter.txt',scatter,0)]
        if not missing:entries.append(('system.img',sparse,0))
        mtk(self.src,entries)
    def test_scatter_conversion_preserves_layout_disables_preloader(self):
        self.scatter_fixture();r=self.run_tool(self.out,'scatter');self.assertEqual(r.returncode,0,r.stderr)
        s=(self.out/'MT6771_Android_scatter_SPFT_EMMC.txt').read_text()
        self.assertIn('V1.1.2',s);self.assertIn('EMMC_BOOT_1',s);self.assertIn('physical_start_addr: 0x1000',s)
        self.assertNotIn('is_download: true\n  type: NORMAL_ROM',s);self.assertNotIn('UFS',s)
    def test_invalid_sparse_or_missing_reference_suppresses_scatter(self):
        for broken,missing in ((True,False),(False,True)):
            with self.subTest(broken=broken,missing=missing):
                self.scatter_fixture(broken,missing);out=self.p/f'{broken}-{missing}';r=self.run_tool(out,'scatter')
                self.assertEqual(r.returncode,0,r.stderr);self.assertFalse((out/'MT6771_Android_scatter_SPFT_EMMC.txt').exists())
    def test_duplicate_logical_labels_retained_in_report(self):
        plain=b'MMM'+b'x'*40;mtk(self.src,[('scp.img',plain,16,'scp1'),('scp.img',plain,16,'scp2')])
        r=self.run_tool(self.out);self.assertEqual(r.returncode,0,r.stderr)
        report=self.out/'OFP-Studio-report.txt';self.assertTrue(report.is_file())
        self.assertIn('scp1 | scp2',report.read_text(encoding='utf-8'))
    def test_output_namespace_conflicts_fail_before_images(self):
        for name in ('boot.img.ofp-studio.partial','SHA256SUMS.txt/child','OFP-Studio-report.txt'):
            with self.subTest(name=name):
                mtk(self.src,[('boot.img',b'MMM'+b'x'*40,16),(name,b'bad',0)])
                out=self.p/str(len(list(self.p.iterdir())));r=self.run_tool(out)
                self.assertNotEqual(r.returncode,0);self.assertFalse((out/'boot.img').exists())
    def test_strict_scatter_rejects_unknown_semantics(self):
        for before,after in ((b'V2.1.0',b'V2.1.0BAD'),(b'storage_type: EMMC',b'storage_type: FAKE_EMMC'),(b'EMMC_USER',b'EMMC_UNKNOWN'),(b'file_name: system.img',b'file_name: NONE'),(b'physical_start_addr: 0x1000',b'physical_start_addr: 0xffffffffffffffff')):
            with self.subTest(after=after):
                self.scatter_fixture();data=self.src.read_bytes();self.src.write_bytes(data.replace(before,after)) if len(before)==len(after) else None
                # Rebuild by decoding just the unencrypted fixture table, not production code.
                size=len(data);header=data[-108:]
                def unshuffle(b):return bytes(b'geyixue'[i%7]^((v>>4)|((v&15)<<4)) for i,v in enumerate(b))
                n=struct.unpack('46sQ4s7s5sH32sH',unshuffle(header))[5];table=unshuffle(data[-108-n*96:-108]);items=[]
                for i in range(n):
                    label,start,length,enc,name,crc=struct.unpack('<32sQQQ32sQ',table[i*96:(i+1)*96]);name=name.rstrip(b'\0').decode();raw=data[start:start+length]
                    if enc:raw=AES.new(KEY,AES.MODE_CFB,iv=IV,segment_size=128).decrypt(raw[:enc])+raw[enc:]
                    if name.endswith('scatter.txt'):raw=raw.replace(before,after)
                    items.append((name,raw,enc))
                mtk(self.src,items);out=self.p/str(len(list(self.p.iterdir())));r=self.run_tool(out,'scatter')
                self.assertEqual(r.returncode,0,r.stderr);self.assertFalse((out/'MT6771_Android_scatter_SPFT_EMMC.txt').exists())
    def test_qc_metadata_overlap_rejected_before_writes(self):
        qc(self.src,xml_override=b'<?xml version="1.0"?><Profile><Firmware><File Path="bad.bin" FileOffsetInSrc="5" SizeInByteInSrc="512" SizeInSectorInSrc="1"/></Firmware></Profile>'+b' '*250)
        self.assertNotEqual(self.run_tool(self.out).returncode,0);self.assertFalse((self.out/'bad.bin').exists())
    def test_qc_long_artifact_collision_rejected(self):
        xml=b'<?xml version="1.0"?><Profile><Firmware><File Path="SHA256SUMS.txt.ofp-studio.partial" FileOffsetInSrc="1" SizeInByteInSrc="718" SizeInSectorInSrc="2"/></Firmware></Profile>'+b' '*100
        qc(self.src,xml_override=xml);self.assertNotEqual(self.run_tool(self.out).returncode,0)
        self.assertFalse((self.out/'SHA256SUMS.txt.ofp-studio.partial').exists())
    def test_nested_qc_firmware_is_decrypted_like_reference(self):
        xml=b'<?xml version="1.0"?><Profile><Firmware><Group><File Path="nested.img" FileOffsetInSrc="3" SizeInByteInSrc="718" SizeInSectorInSrc="2"/></Group></Firmware></Profile>'+b' '*100
        plain=qc(self.src,xml_override=xml);r=self.run_tool(self.out)
        self.assertEqual(r.returncode,0,r.stderr);self.assertEqual((self.out/'nested.img').read_bytes(),plain)
    def test_generated_metadata_cancel_does_not_publish_manifest(self):
        self.make();r=self.run_tool(self.out,'cancel-metadata');self.assertNotEqual(r.returncode,0)
        self.assertFalse((self.out/'SHA256SUMS.txt').exists());self.assertFalse((self.out/'OFP-Studio-report.txt').exists())
    def test_handle_owned_output_finalization(self):
        r=subprocess.run([str(DRIVER),'--ownership',str(self.p)],capture_output=True,text=True)
        self.assertEqual(r.returncode,0,r.stderr)

if __name__=='__main__':unittest.main(verbosity=2)
