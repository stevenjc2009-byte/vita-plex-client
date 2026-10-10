#!/usr/bin/env python3
"""Bundle exact corresponding sources alongside a static-codec release."""
from pathlib import Path
import hashlib, subprocess, zipfile, sys
root=Path(__file__).resolve().parents[1];deps=root/'build/deps'
expected={'ffmpeg-n6.0.tar.gz':'aacce24d5bb6c67fbf1e3343bc15a6977ad700cdb4f43c8551f6544fa54ab7ec','wiliwili-ffmpeg.patch':'2d38529d10c74560db3909cf8c2f3e359b128c04fb29f6dd0085770ed81cef6c'}
for name,digest in expected.items():assert hashlib.sha256((deps/name).read_bytes()).hexdigest()==digest,name
source=Path(sys.argv[1]) if len(sys.argv)>1 else deps/'FFmpeg-ea3d24bbe3c58b171e55fe2151fc7ffaca3ab3d2'
with zipfile.ZipFile(root/'build/codec-sources.zip','w',zipfile.ZIP_DEFLATED) as z:
 for name in expected:z.write(deps/name,name)
 for name in ('tools/build-stream-codecs.sh','assets/STREAM-CODECS-NOTICES.txt','assets/FFmpeg-LGPL-2.1.txt','assets/wiliwili-GPL-3.0.txt'):z.write(root/name,name)
 for name in ('config.h','config_components.h','ffbuild/config.mak','ffbuild/config.sh'):
  if (source/name).is_file():z.write(source/name,'generated/'+name)
with zipfile.ZipFile(root/'build/client-source.zip','w',zipfile.ZIP_DEFLATED) as z:
 # Actions containers use a different owner from the checkout action. Trust
 # only this exact repository for this read, without changing global Git config.
 for raw in subprocess.check_output(['git','-c','safe.directory='+str(root),'ls-files','-z'],cwd=root).split(b'\0'):
  if raw:
   name=raw.decode();assert not name.startswith(('build/','.git/'));z.write(root/name,'vita-plex-client/'+name)
print('Client and pinned codec sources packaged')
