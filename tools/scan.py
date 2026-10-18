import re,sys,struct
pat=re.compile(sys.argv[2],re.I)
d=open(sys.argv[1],'rb').read()
seen=set()
for m in re.finditer(rb'[\x20-\x7e]{5,}',d):
    s=m.group().decode()
    if pat.search(s) and s not in seen:
        seen.add(s); print(hex(m.start()),s)
