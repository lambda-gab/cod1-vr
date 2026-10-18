import struct,sys
d=open(sys.argv[1],'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]; n=struct.unpack_from('<H',d,pe+6)[0]; opt=struct.unpack_from('<H',d,pe+20)[0]
base=struct.unpack_from('<I',d,pe+24+28)[0]; sec=pe+24+opt
S=[struct.unpack_from('<8sIIII',d,sec+i*40) for i in range(n)]
for o in sys.argv[2:]:
    o=int(o,16)
    for name,vs,va,rs,ro in S:
        if ro<=o<ro+rs: print(hex(o),'->',hex(base+va+o-ro),name.rstrip(b'\0').decode())
