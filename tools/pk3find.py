import zipfile,glob,sys
import os
root=os.path.join(os.environ.get('COD_DIR','.'),'main')   # set COD_DIR to the game folder
pat=[p.lower() for p in sys.argv[1:]]
for pk in sorted(glob.glob(root+r'\*.pk3')):
    try: z=zipfile.ZipFile(pk)
    except Exception: continue
    for i in z.infolist():
        n=i.filename.lower()
        if any(p in n for p in pat): print(pk.split(chr(92))[-1], i.filename, i.file_size)
