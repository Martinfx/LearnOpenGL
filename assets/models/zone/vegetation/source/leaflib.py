# Leaf sprite library: RGBA leaf texture (alpha = UV outline & not the dark padding) + component bboxes.
import sys,json,numpy as np
from PIL import Image,ImageFilter
from scipy import ndimage
diff,nor,mask,out=sys.argv[1:5]
d=np.asarray(Image.open(diff).convert('RGB')).astype(np.float32)/255
m=np.asarray(Image.open(mask).filter(ImageFilter.MinFilter(3))).astype(np.float32)/255
mx=d.max(axis=2)
a=m*np.clip((mx-0.10)/0.08,0,1)
a=ndimage.gaussian_filter(a,0.7)
a=np.clip((a-0.5)*2.5+0.5,0,1)
rgba=np.dstack([d,a]); Image.fromarray((rgba*255).astype(np.uint8),'RGBA').save(out+'_rgba.png')
lab,n=ndimage.label(a>0.3)
boxes=[]
for i,sl in enumerate(ndimage.find_objects(lab)):
    y,x=sl; area=(lab[sl]==i+1).sum()
    if area<1500: continue
    boxes.append(dict(i=len(boxes),x0=x.start,x1=x.stop,y0=y.start,y1=y.stop,area=int(area)))
json.dump(dict(size=a.shape[1],boxes=boxes),open(out+'_boxes.json','w'),indent=0)
for b in boxes: print(b)
