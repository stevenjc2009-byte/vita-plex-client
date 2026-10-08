from pathlib import Path
import struct,zipfile,sys
folder=Path(sys.argv[1]);folder.mkdir(exist_ok=True)
def sfo(title='PLEX00001',version='01.41'):
 keys=b'TITLE_ID\0APP_VER\0';data=title.encode()+b'\0'+version.encode()+b'\0';start=20+32
 return struct.pack('<5I',0x46535000,0x101,start,start+len(keys),2)+struct.pack('<HHIII',0,0x204,len(title)+1,len(title)+1,0)+struct.pack('<HHIII',9,0x204,len(version)+1,len(version)+1,len(title)+1)+keys+data
def fixture(name,title='PLEX00001',version='01.41',extra=None):
 with zipfile.ZipFile(folder/(name+'.vpk'),'w',zipfile.ZIP_DEFLATED) as z:
  z.writestr('eboot.bin',b'\0'*512);z.writestr('sce_sys/param.sfo',sfo(title,version));z.writestr('assets/font.bin',b'font'*1024)
  if extra:z.writestr(*extra)
fixture('valid');fixture('wrong-title','OTHER0001');fixture('wrong-version',version='01.40');fixture('traversal',extra=('assets/../../outside',b'bad'));fixture('duplicate',extra=('ASSETS/FONT.BIN',b'bad'));fixture('bomb',extra=('assets/huge',b'0'*(32*1024*1024+1)))
p=folder/'valid.vpk';data=bytearray(p.read_bytes());data[40]^=1;(folder/'corrupt.vpk').write_bytes(data)
