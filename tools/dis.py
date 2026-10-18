import sys,struct
# needs: pip install capstone
from capstone import *
d=open(sys.argv[1],'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]; n=struct.unpack_from('<H',d,pe+6)[0]; osz=struct.unpack_from('<H',d,pe+20)[0]
base=struct.unpack_from('<I',d,pe+24+28)[0]; sec=pe+24+osz
S=[struct.unpack_from('<8sIIII',d,sec+i*40) for i in range(n)]
va=int(sys.argv[2],16); ln=int(sys.argv[3],0)
for _,vs,v,rs,ro in S:
    if base+v<=va<base+v+rs: o=va-base-v+ro
md=Cs(CS_ARCH_X86,CS_MODE_32)
for i in md.disasm(d[o:o+ln],va): print(hex(i.address),i.mnemonic,i.op_str)
