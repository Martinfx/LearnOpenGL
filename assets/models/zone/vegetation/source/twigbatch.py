# Bakes the 16 twig cells (4 atlases x 4 twigs, albedo + normal + AO) with twigbake.py, 4 at a time.
# Run in a work folder holding the leaf library (see twigs.sh), then twigatlas.py <out> packs the atlases.
import json,subprocess,sys,os
HERE=os.path.dirname(os.path.abspath(__file__))
from concurrent.futures import ThreadPoolExecutor
TS=dict(boxes="lib_ts_boxes.json",leaf_rgba="lib_ts_rgba.png",leaf_nor="lib_ts_nor.png",stem="dl/tree_small_02/textures/tree_small_02_branch_diff_2k.jpg",orient="right")
IT=dict(boxes="lib_it_boxes.json",leaf_rgba="lib_it_rgba.png",leaf_nor="lib_it_nor.png",stem="dl/island_tree_02/textures/island_tree_02_branches_diff_2k.jpg",orient="bottom",leaves=[0,1,2,3,4,5,6,7])
OV=[3,5,9,11,5,9,11,6,8]          # ovate singles; 3,6,8 carry rust spots: rarer
PIN=dict(TS,orient="bottom",leaves=[1])
A={
 'birch':(TS,dict(leaves=OV),0.7,[
   dict(length=0.62,leaves=15,leaf=0.05,side_shoots=4,angle=(35,70),droop=(0.05,0.25),r=0.004),
   dict(length=0.48,leaves=12,leaf=0.05,side_shoots=3,angle=(35,70),droop=(0.05,0.25),r=0.004),
   dict(length=0.66,leaves=16,leaf=0.048,side_shoots=5,angle=(30,60),droop=(0.1,0.3),r=0.004),
   dict(length=0.4,leaves=9,leaf=0.05,side_shoots=2,angle=(40,75),r=0.0035)]),
 'broad':(TS,dict(leaves=OV),0.7,[
   dict(length=0.56,leaves=11,leaf=0.085,side_shoots=3,angle=(35,65),r=0.006),
   dict(length=0.46,leaves=10,leaf=0.08,side_shoots=2,angle=(35,65),r=0.006),
   dict(length=0.6,leaves=12,leaf=0.08,side_shoots=4,angle=(30,60),r=0.006),
   dict(length=0.36,leaves=7,leaf=0.085,side_shoots=1,angle=(40,70),r=0.005)]),
 'pinnate':(PIN,{},0.85,[
   dict(length=0.45,leaves=4,leaf=0.3,side_shoots=0,opposite=True,angle=(40,60),fold=0.02,droop=(0.05,0.2),r=0.008,t0=0.2),
   dict(length=0.4,leaves=5,leaf=0.28,side_shoots=1,angle=(35,60),fold=0.02,droop=(0.05,0.2),r=0.008,t0=0.15),
   dict(length=0.5,leaves=6,leaf=0.27,side_shoots=0,angle=(30,55),fold=0.02,droop=(0.1,0.25),r=0.008,t0=0.15),
   dict(length=0.3,leaves=3,leaf=0.3,side_shoots=0,opposite=True,angle=(40,65),fold=0.02,r=0.007,t0=0.3)]),
 'lance':(IT,{},0.75,[
   dict(length=0.5,leaves=10,leaf=0.12,side_shoots=2,angle=(30,55),fold=0.18,r=0.006),
   dict(length=0.42,leaves=8,leaf=0.12,side_shoots=2,angle=(30,55),fold=0.18,r=0.006),
   dict(length=0.58,leaves=12,leaf=0.11,side_shoots=3,angle=(25,50),fold=0.18,droop=(0.05,0.2),r=0.006),
   dict(length=0.34,leaves=6,leaf=0.12,side_shoots=1,angle=(35,60),fold=0.18,r=0.005)]),
}
jobs=[]
os.makedirs('cells',exist_ok=True)
for name,(base,extra,size,twigs) in A.items():
    for ci,tw in enumerate(twigs):
        for mode in ('albedo','normal','ao'):
            cfg=dict(base,**extra,seed=100+ci*7+len(name),size=size,samples=24,twig=tw,mode=mode,
                     out=os.path.abspath('cells')+f"/{name}_{ci}_{mode}"+('.png' if mode=='albedo' else '.npy'))
            jobs.append(cfg)
def run(c):
    r=subprocess.run([sys.executable,'-I',os.path.join(HERE,'twigbake.py'),json.dumps(c)],capture_output=True,text=True)
    return [l for l in (r.stdout+r.stderr).splitlines() if 'TWIG' in l or 'Error' in l]
with ThreadPoolExecutor(4) as ex:
    for out in ex.map(run,jobs): print(*out)
json.dump({k:v[2] for k,v in A.items()},open('cells/sizes.json','w'))
