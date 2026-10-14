from pathlib import Path
import numpy as np
from PIL import Image
R=Path('/mnt/simulations/vehicle-interiors/exports');n=1024
rng=np.random.default_rng(32020)
noise=rng.random((n,n)).astype(np.float32)
# Periodic filtered grain: no borrowed photograph or baked lighting.
noise=sum(np.roll(np.roll(noise,i,0),j,1) for i in [-1,0,1] for j in [-1,0,1])/9
for name,holes in [('Leather',False),('Perforated',True)]:
 h=noise*.08
 if holes:
  yy,xx=np.mgrid[:n,:n];dx=((xx+.5)/n*40)%1-.5;dy=((yy+.5)/n*72)%1-.5;d=np.sqrt(dx*dx+dy*dy)
  h-=.38*np.exp(-(d/.15)**4)
 gx=(np.roll(h,-1,1)-np.roll(h,1,1))*1.4;gy=(np.roll(h,-1,0)-np.roll(h,1,0))*1.4
 normal=np.stack([-gx,gy,np.ones_like(h)],axis=-1);normal/=np.linalg.norm(normal,axis=-1,keepdims=True)
 Image.fromarray(np.clip((normal*.5+.5)*255,0,255).astype('uint8')).save(R/f'T_MKZ_{name}_Normal.png')
 rough=np.clip((.52 if holes else .45)+(noise-.5)*.08,0,1)
 Image.fromarray((rough*255).astype('uint8')).save(R/f'T_MKZ_{name}_Roughness.png')
