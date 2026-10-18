import struct,sys
d=open(sys.argv[1],'rb').read()
for a in sys.argv[2:]:
    pat=b'\xff\x15'+struct.pack('<I',int(a,16)); o=d.find(pat); hits=[]
    while o!=-1: hits.append(hex(0x400000+o)); o=d.find(pat,o+1)
    print(a,hits)
