#!/usr/bin/env python3
"""Compare released ABI3 positive configurations with current production headers.

Requires local Git history; never downloads sources. The committed corpus names
immutable release commits. Native port operations and live graphics results are
outside this CPU contract check.
"""
import json, re, subprocess, tempfile
from pathlib import Path

root=Path(__file__).resolve().parents[1]
releases=json.loads((root/'tests/fixtures/released_physical_admission.json').read_text())['releases']
code=r'''
#include "old/kern_xelp_platform.hpp"
#include "kern_xelp_platform.hpp"
#include <cstdio>
#include <initializer_list>
#include <set>
static_assert(sizeof(Released::Profile)==sizeof(XELP::Profile),"ABI3 profile size");
static_assert(sizeof(Released::ProbeConfig)==sizeof(XELP::ProbeConfig),"ABI3 configuration size");
int main(){
 // All PCI identities/revisions are covered by the frozen selection test.
 // Enumerate policy states for each distinct admitted physical backend.
 std::set<std::pair<unsigned,unsigned>> seen;
 for(const auto &entry:Released::DeviceTable){
  const auto p=Released::identify((unsigned(entry.device)<<16)|0x8086,0);
  if(!p.admitted())continue;
  auto family=unsigned(p.family),gt=unsigned(p.gt);
  if(!seen.emplace(family,gt).second)continue;
  // TGL uses Combo A/B/C; desktop Display12 also maps physical ports at
  // coordinates 3/4. Include those bits when replaying old ADL/RPL-S policy.
  for(unsigned combo=0;combo<32;++combo)for(unsigned assign=0;assign<4096;++assign)
  for(unsigned backend:{1U,2U,3U,7U,0x19U})for(unsigned legacyBackend=0;legacyBackend<64;++legacyBackend){
   Released::ProbeConfig before;before.profile=p;before.combo=combo;before.comboBackend=backend;
   unsigned bits=assign;
   for(unsigned tc=0;tc<6;++tc,bits>>=2){
    if((bits&3)==1)before.legacy|=8U<<tc;
    if((bits&3)==2)before.typeC|=8U<<tc;
    if((bits&3)==3)before.excluded|=8U<<tc;
   }
   before.legacyBackend=legacyBackend<<3;
   if(!before.valid())continue;
   XELP::ProbeConfig after;
   after.profile=XELP::identify(p.identity,p.revision,p.effectiveIdentity);
   after.combo=before.combo;after.comboBackend=before.comboBackend;
   after.legacy=before.legacy;after.typeC=before.typeC;after.excluded=before.excluded;after.legacyBackend=before.legacyBackend;
   if(!after.valid()){
    fprintf(stderr,"lost ABI3 configuration %08x combo=%x legacy=%x typeC=%x excluded=%x backends=%x/%x\n",
     p.identity,combo,before.legacy,before.typeC,before.excluded,backend,before.legacyBackend);
    return 1;
   }
  }
 }
}
'''
for release in releases:
    commit=release['commit']
    with tempfile.TemporaryDirectory(prefix='wex-released-abi-') as t:
        p=Path(t);old=p/'old';old.mkdir()
        names=subprocess.check_output(['git','-C',str(root),'ls-tree','-r','--name-only',commit,'WhateverXe'],text=True).splitlines()
        for name in names:
            if not name.endswith('.hpp'):continue
            text=subprocess.check_output(['git','-C',str(root),'show',commit+':'+name],text=True)
            text=re.sub(r'\bXELP\b','Released',text)
            for guard in re.findall(r'^#ifndef (\w+)\s*$',text,re.M):
                text=re.sub(r'\b'+re.escape(guard)+r'\b','RELEASED_'+guard,text)
            (old/Path(name).name).write_text(text)
        (p/'test.cpp').write_text(code)
        subprocess.run(['clang++','-std=c++17','-O2','-I'+str(p),'-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True)
    print('PASS released ABI3 positives:',release['tag'],commit,flush=True)
