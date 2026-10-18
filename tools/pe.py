import struct,sys
d=open(sys.argv[2],'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]; n=struct.unpack_from('<H',d,pe+6)[0]; opt=pe+24
osz=struct.unpack_from('<H',d,pe+20)[0]; sec=opt+osz
S=[struct.unpack_from('<8sIIII',d,sec+i*40) for i in range(n)]
base=struct.unpack_from('<I',d,opt+28)[0]
def off(rva):
    for _,vs,va,rs,ro in S:
        if va<=rva<va+max(vs,rs): return rva-va+ro
def cstr(o): return d[o:d.index(b'\0',o)].decode()
dd=opt+96
if sys.argv[1]=='exp':
    rva=struct.unpack_from('<I',d,dd)[0]; o=off(rva)
    nn,names=struct.unpack_from('<I',d,o+24)[0],struct.unpack_from('<I',d,o+32)[0]
    for i in range(nn): print(cstr(off(struct.unpack_from('<I',d,off(names)+4*i)[0])))
else:
    rva=struct.unpack_from('<I',d,dd+8)[0]; o=off(rva)
    while True:
        ilt,_,_,nm,iat=struct.unpack_from('<5I',d,o)
        if nm==0: break
        dll=cstr(off(nm)); t=off(ilt or iat); k=0; fn=[]
        while True:
            e=struct.unpack_from('<I',d,t+4*k)[0]
            if e==0: break
            fn.append((hex(base+iat+4*k), cstr(off(e)+2) if not e&0x80000000 else '#%d'%(e&0xffff))); k+=1
        print(dll,[f for f in fn if sys.argv[1]=='impall' or 'Swap' in f[1] or 'wgl' in f[1] or 'LoadLibrary' in f[1] or dll.lower().startswith('opengl')])
        o+=20
