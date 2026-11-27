import re
def tokenize(s):
    i=0;n=len(s);out=[]
    while i<n:
        c=s[i]
        if c in '()': out.append(c); i+=1
        elif c.isspace(): i+=1
        elif c=='"':
            j=i+1;buf=[]
            while s[j]!='"':
                if s[j]=='\\': buf.append(s[j:j+2]); j+=2
                else: buf.append(s[j]); j+=1
            out.append(('S',''.join(buf))); i=j+1
        else:
            j=i
            while j<n and not s[j].isspace() and s[j] not in '()': j+=1
            out.append(s[i:j]); i=j
    return out
def parse(s):
    toks=tokenize(s); pos=0
    def rd():
        nonlocal pos
        t=toks[pos]; pos+=1
        if t=='(':
            l=[]
            while toks[pos]!=')': l.append(rd())
            pos+=1; return l
        return t
    res=[]
    while pos<len(toks): res.append(rd())
    return res
def dump(x,ind=0):
    if isinstance(x,tuple): return '"'+x[1]+'"'
    if isinstance(x,str): return x
    inner=' '.join(dump(e) if not isinstance(e,list) else '\n'+'  '*(ind+1)+dump(e,ind+1) for e in x)
    return '('+inner+')'
def val(x): return x[1] if isinstance(x,tuple) else x
LIBDIR=__import__('os').environ.get('KICAD_DIR', 'D:/Program Files/KiCad/9.0') + '/share/kicad/symbols/'
_cache={}
def lib(name):
    if name not in _cache:
        path = __import__('os').path.join(__import__('os').path.dirname(__file__), '..', 'radar_revb.kicad_sym') if name == 'radar_revb' else LIBDIR+name+'.kicad_sym'
        _cache[name]=parse(open(path, encoding='utf-8').read())[0]
    return _cache[name]
def find_sym(libname,sym):
    for e in lib(libname):
        if isinstance(e,list) and e[0]=='symbol' and val(e[1])==sym: return e
    raise KeyError(sym)
import copy
def flat_sym(libname,sym):
    s=copy.deepcopy(find_sym(libname,sym))
    ext=[e for e in s if isinstance(e,list) and e[0]=='extends']
    if not ext: return s
    parent=val(ext[0][1]); p=flat_sym(libname,parent)
    p=copy.deepcopy(p)
    # override props
    cprops={val(e[1]):e for e in s if isinstance(e,list) and e[0]=='property'}
    newp=[p[0],('S',sym)]
    for e in p[2:]:
        if isinstance(e,list) and e[0]=='property' and val(e[1]) in cprops:
            newp.append(cprops.pop(val(e[1])))
        elif isinstance(e,list) and e[0]=='symbol':
            e[1]=('S',val(e[1]).replace(parent,sym,1)); newp.append(e)
        else: newp.append(e)
    # insert remaining props before first sub-symbol
    idx=next(i for i,e in enumerate(newp) if isinstance(e,list) and e[0]=='symbol') if any(isinstance(e,list) and e[0]=='symbol' for e in newp) else len(newp)
    for v in cprops.values(): newp.insert(idx,v)
    return newp
def pins(symb):
    """returns list of dict(unit,num,name,type,x,y,hidden)"""
    out=[]
    for sub in symb:
        if isinstance(sub,list) and sub[0]=='symbol':
            nm=val(sub[1]); m=re.search(r'_(\d+)_(\d+)$',nm); unit=int(m.group(1))
            for e in sub:
                if isinstance(e,list) and e[0]=='pin':
                    d={'unit':unit,'type':e[1],'hidden':'hide' in e}
                    for f in e:
                        if isinstance(f,list):
                            if f[0]=='at': d['x']=float(f[1]);d['y']=float(f[2]);d['rot']=float(f[3]) if len(f)>3 else 0
                            if f[0]=='name': d['name']=val(f[1])
                            if f[0]=='number': d['num']=val(f[1])
                    out.append(d)
    return out
def prop(symb,k):
    for e in symb:
        if isinstance(e,list) and e[0]=='property' and val(e[1])==k: return val(e[2])
