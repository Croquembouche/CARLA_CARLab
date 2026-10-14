from pathlib import Path
from PIL import Image
import numpy as np
R=Path('/mnt/simulations/vehicle-interiors');E=R/'exports';n=1024;rng=np.random.default_rng(202603)
noise=rng.random((n,n)).astype('float32');grain=sum(np.roll(np.roll(noise,i,0),j,1) for i in [-1,0,1] for j in [-1,0,1])/9
for name,mean in [('Leather',.355),('Perforated',.42)]:
 rough=np.clip(mean+(grain-.5)*.07,0,1)
 if name=='Perforated':
  yy,xx=np.mgrid[:n,:n];cy=(yy+.5)/n*128;dx=((xx+.5)/n*96+.5*(np.floor(cy)%2))%1-.5;dy=cy%1-.5;d=np.sqrt(dx*dx+dy*dy);hole=np.exp(-(d/.18)**6);mask=1-hole*.88;Image.fromarray((mask*255).astype('uint8')).save(E/'T_MKZ_Perforation_Mask.png')
  h=grain*.035-hole*.25;gx=(np.roll(h,-1,1)-np.roll(h,1,1))*.65;gy=(np.roll(h,-1,0)-np.roll(h,1,0))*.65;normal=np.stack([-gx,gy,np.ones_like(h)],axis=-1);normal/=np.linalg.norm(normal,axis=-1,keepdims=True);Image.fromarray(((normal*.5+.5)*255).astype('uint8')).save(E/'T_MKZ_Perforated_Normal.png');rough+=hole*.14
 Image.fromarray((rough*255).astype('uint8')).save(E/f'T_MKZ_{name}_Roughness.png')
stripe=rng.random(n).astype('float32');stripe=(stripe+np.roll(stripe,1)+np.roll(stripe,-1))/3;stripe=np.broadcast_to(stripe[:,None],(n,n));rough=.30+(stripe-.5)*.12+(grain-.5)*.015
Image.fromarray((rough*255).astype('uint8')).save(E/'T_MKZ_Brushed_Roughness.png')
gy=(np.roll(stripe,-1,0)-np.roll(stripe,1,0))*.04;normal=np.stack([np.zeros_like(gy),gy,np.ones_like(gy)],axis=-1);normal/=np.linalg.norm(normal,axis=-1,keepdims=True);Image.fromarray(((normal*.5+.5)*255).astype('uint8')).save(E/'T_MKZ_Brushed_Normal.png')
