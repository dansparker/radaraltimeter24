"""Reichweitenabschätzung K-LC1a als Höhenmesser (Bodenflächenziel), siehe README.md."""
import numpy as np
c=3e8; lam=c/24.125e9
fs=300e3; N=2048; L=2432; B=90e6
dbin=fs/N                     # 146.5 Hz
k=dbin/(c*(L-1)/(2*B*N))      # bin width in m -> Hz/m
ENBW=1.5*dbin
r1=0.0167*10**(114/40)        # datasheet: sigma=1 -> SNR 6 dB @500 Hz BW
def e_mod(f): return 45e-9*(f/500)**-0.455
def e_opa(f): return 10e-9*np.sqrt(1+1500/f)
def NI(f):
    et=np.sqrt(e_mod(f)**2+e_opa(f)**2+(2.8e-9)**2)
    return 20*np.log10(45e-9/et)
Lsys=3.0; req=12.0
thE=np.radians(80); thH=np.radians(34)
def peak_snr(R,s0db,vh,along=thH,across=thE):
    n=601; a=np.linspace(-1.2,1.2,n); da=a[1]-a[0]
    tx,ty=np.meshgrid(a,a,indexing='ij')
    X=np.tan(tx); Y=np.tan(ty)
    r=R*np.sqrt(1+X**2+Y**2)
    u=R*X/r; v=R*Y/r
    g1=np.exp(-4*np.log(2)*(np.arcsin(u)**2/along**2+np.arcsin(v)**2/across**2))
    dA=R**2/np.cos(tx)**2/np.cos(ty)**2*da*da
    s0=10**(s0db/10)
    f=k*r-2*vh*u/lam
    fb=np.clip(f,50,None)
    snr=10**((6+10*np.log10(500/ENBW)+NI(fb)-Lsys)/10)*(r1/r)**4*s0*dA*g1**2
    idx=np.round(f/dbin).astype(int)
    m=idx>0
    h=np.bincount(idx[m],weights=snr[m])
    return 10*np.log10(h.max()), h.argmax()*dbin/k
print("Hz/m",k,"bin m",dbin/k)
for vh in (0,40):
  for s0 in (-10,0,10):
    Rs=np.arange(10,801,5); best=None
    for R in Rs:
        s,_=peak_snr(R,s0,vh)
        if s>=req: best=R
Rs=np.concatenate([np.arange(5,100,5),np.arange(100,801,10)])
for req in (12,5):
  for vh in (0,40):
    row=[]
    for s0 in (-10,0,10):
        best=0
        for R in Rs:
            if peak_snr(R,s0,vh)[0]>=req: best=R
            elif R>30: break
        row.append(best)
    print("req",req,"vh",vh,row)
