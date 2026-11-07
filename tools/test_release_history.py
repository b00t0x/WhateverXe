#!/usr/bin/env python3
"""Compare released ABI3 positive configurations with current production headers.

Requires local Git history; never downloads sources. The committed corpus names
immutable release commits. Native port operations and live graphics results are
outside this CPU contract check.
"""
import argparse, json, re, subprocess, tempfile
from pathlib import Path

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--abi-only',action='store_true',help='compile packet layout and enum comparisons without enumerating policy states')
args=parser.parse_args()
releases=json.loads((root/'tests/fixtures/released_physical_admission.json').read_text())['releases']
code=r'''
#include "old/kern_xelp_platform.hpp"
#include "kern_xelp_platform.hpp"
#include <cstdio>
#include <initializer_list>
#include <set>
#include <cstddef>
static_assert(sizeof(Released::Profile)==sizeof(XELP::Profile),"ABI3 profile size");
static_assert(sizeof(Released::ProbeConfig)==sizeof(XELP::ProbeConfig),"ABI3 configuration size");
LAYOUT_CHECKS
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
        checks=[]
        for structure,fields in [('Profile','identity revision family display gt donor effectiveIdentity'),
                                 ('ProbeConfig','abi bytes profile combo legacy typeC excluded comboBackend legacyBackend')]:
            checks.append(f'static_assert(alignof(Released::{structure})==alignof(XELP::{structure}),"ABI3 {structure} alignment");')
            for field in fields.split():
                checks.append(f'static_assert(offsetof(Released::{structure},{field})==offsetof(XELP::{structure},{field}),"ABI3 {structure}.{field} offset");')
                checks.append(f'static_assert(sizeof(decltype(Released::{structure}::{field}))==sizeof(decltype(XELP::{structure}::{field})),"ABI3 {structure}.{field} width");')
        for name in names:
            if not name.endswith('.hpp'):continue
            text=subprocess.check_output(['git','-C',str(root),'show',commit+':'+name],text=True)
            if name.endswith('/kern_xelp_devices.hpp'):
                for enum,body in re.findall(r'enum class (Family|Display|GT|Donor)\s*:\s*\w+\s*\{([^}]+)\}',text):
                    checks.append(f'static_assert(sizeof(Released::{enum})==sizeof(XELP::{enum}) && alignof(Released::{enum})==alignof(XELP::{enum}),"ABI3 {enum} representation");')
                    for entry in body.split(','):
                        member=entry.split('=')[0].strip()
                        checks.append(f'static_assert(unsigned(Released::{enum}::{member})==unsigned(XELP::{enum}::{member}),"ABI3 {enum}.{member} value");')
            text=re.sub(r'\bXELP\b','Released',text)
            for guard in re.findall(r'^#ifndef (\w+)\s*$',text,re.M):
                text=re.sub(r'\b'+re.escape(guard)+r'\b','RELEASED_'+guard,text)
            (old/Path(name).name).write_text(text)
        selected=code[:code.index('int main(){')]+'int main(){}\n' if args.abi_only else code
        (p/'test.cpp').write_text(selected.replace('LAYOUT_CHECKS','\n'.join(checks)))
        subprocess.run(['clang++','-std=c++17','-O2','-I'+str(p),'-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True)
    print('PASS released ABI3 '+('layout/enums:' if args.abi_only else 'layout/enums and positives:'),release['tag'],commit,flush=True)
