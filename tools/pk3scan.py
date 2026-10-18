import zipfile,glob,sys,re
import os
root=os.path.join(os.environ.get('COD_DIR','.'),'main')   # set COD_DIR to the game folder
found={}
for pk in sorted(glob.glob(root+r'\*.pk3')):
    try: z=zipfile.ZipFile(pk)
    except Exception as e: continue
    for n in z.namelist():
        if n.lower().startswith('weapons/sp/') and not n.endswith('/'):
            found[n]=(pk,z)
print(len(found),'weapon files')
names=sorted(found)
print(names[:60])
for n in names:
    pk,z=found[n]
    t=z.read(n).decode('latin1')
    f=t.split(chr(92))
    kv=dict(zip(f[1::2],f[2::2]))
    if kv.get('adsOverlayShader') or 'kar98' in n or n.endswith('/m1garand'):
        print(n, {k:kv.get(k) for k in ['handModel','viewModel','adsOverlayShader','adsOverlayReticle','twoHanded','adsZoomFov']})
