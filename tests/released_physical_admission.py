#!/usr/bin/env python3
"""Preserve physical selections accepted by each released implementation.

The frozen corpus comes from the referenced releases' own headers, not the
current DeviceTable. This checks backend selection, not live functionality.
New support may expand the set without rewriting older positive expectations.
"""
import json, subprocess, tempfile
from pathlib import Path

root=Path(__file__).resolve().parents[1]
corpus=json.loads((root/'tests/fixtures/released_physical_admission.json').read_text())
assert corpus['schema']==1 and corpus['releases']
rows=[]
for release in corpus['releases']:
    assert len(release['commit'])==40 and release['physical_ids']
    for entry in release['physical_ids']:
        for device in entry['devices']:
            rows.append('{0x%s,%d,%d,"%s"}' % (device,entry['revision_min'],entry['revision_max'],release['tag']))
code=r'''
#include "kern_xelp_platform.hpp"
#include <cassert>
#include <cstdio>
struct Released {unsigned device,first,last;const char *tag;};
const Released inputs[]={INPUTS};
int main(){
 for(const auto &input:inputs)for(unsigned rev=input.first;rev<=input.last;++rev){
  const uint32_t physical=(input.device<<16)|0x8086;
  auto p=XELP::identify(physical,rev);
  if(!p.admitted() || !p.displayAdmitted()){
   fprintf(stderr,"lost released physical selection: %s %04x rev %u\n",input.tag,input.device,rev);
   return 1;
  }
  assert(p.identity==physical && p.effectiveIdentity==physical);
  uint32_t property=input.device;
  assert(XELP::same(p,XELP::select(physical,rev,true,&property,sizeof(property))));
 }
 puts("PASS frozen released physical selections, including explicit unchanged device-id properties");
}
'''.replace('INPUTS',',\n'.join(rows))
with tempfile.TemporaryDirectory(prefix='wex-released-selection-') as t:
    p=Path(t);(p/'test.cpp').write_text(code)
    subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
