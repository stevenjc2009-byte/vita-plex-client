from pathlib import Path
import os,re
root=Path(__file__).resolve().parents[1]
cmake=re.search(r'set\(VITA_VERSION "([^"]+)"\)',(root/'CMakeLists.txt').read_text()).group(1)
header=re.search(r'#define APP_VERSION "([^"]+)"',(root/'src/update.h').read_text()).group(1)
assert cmake==header,'Build and app versions differ'
ref=os.environ.get('GITHUB_REF','')
if ref.startswith('refs/tags/'):assert ref=='refs/tags/v'+header,'Release tag differs from VPK version'
print('Versions match:',header)
