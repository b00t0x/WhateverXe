#!/usr/bin/env python3
"""Regenerate the positive physical-selection corpus from immutable Git history.

Each selected revision compiles against its own production headers. This does
not qualify physical hardware, override combinations, or native driver paths.
"""
import argparse, json, re, subprocess, tempfile
from pathlib import Path

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ref',action='append',help='release tag or commit; repeat to select a bounded history')
parser.add_argument('--output',type=Path,default=root/'tests/fixtures/released_physical_admission.json')
args=parser.parse_args()
def git(*argv):
    return subprocess.check_output(['git','-C',str(root),*argv])
refs=args.ref or [r for r in git('tag','--sort=version:refname').decode().splitlines() if re.fullmatch(r'\d+\.\d+\.\d+',r)]
result={'schema':1,'scope':'Physical PCI/backend admission only; not a hardware validation claim.','releases':[]}
code=r'''
#include "kern_xelp_platform.hpp"
#include <cstdio>
int main(){for(unsigned id=0;id<65536;++id){
 unsigned first=256;
 for(unsigned rev=0;rev<=256;++rev){
  bool accepted=rev<256 && XELP::identify((id<<16)|0x8086,rev).admitted();
  if(accepted && first==256)first=rev;
  if(!accepted && first!=256){printf("%04x %u %u\n",id,first,rev-1);first=256;}
 }
}}
'''
for ref in refs:
    commit=git('rev-parse',ref+'^{commit}').decode().strip()
    with tempfile.TemporaryDirectory(prefix='wex-history-') as t:
        p=Path(t)
        for name in git('ls-tree','-r','--name-only',commit,'WhateverXe').decode().splitlines():
            if name.endswith('.hpp'):
                (p/Path(name).name).write_bytes(git('show',commit+':'+name))
        (p/'test.cpp').write_text(code)
        subprocess.run(['clang++','-std=c++17','-O2','-I'+str(p),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
        rows=subprocess.check_output([str(p/'test')],text=True).splitlines()
        groups={}
        for row in rows:
            device,first,last=row.split()
            groups.setdefault((int(first),int(last)),[]).append(device)
        result['releases'].append({'tag':ref,'commit':commit,'physical_ids':[
            {'devices':devices,'revision_min':first,'revision_max':last} for (first,last),devices in groups.items()]})
args.output.write_text(json.dumps(result,indent=2)+'\n')
