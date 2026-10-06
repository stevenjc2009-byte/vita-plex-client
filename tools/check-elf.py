import re,subprocess,sys
text=subprocess.check_output(['arm-vita-eabi-readelf','-l',sys.argv[1]],text=True)
segments=[]
for line in text.splitlines():
    fields=line.split()
    if fields and fields[0]=='LOAD':segments.append((int(fields[2],16),int(fields[5],16)))
assert len(segments)>=2,'Expected code and data segments'
for (base,size),(next_base,_) in zip(segments,segments[1:]):assert base+size<=next_base,'ELF segments overlap'
print('ELF load segments do not overlap')
