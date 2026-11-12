#!/usr/bin/env python3
"""Exercise the real AUX observer and power wrappers with inaccessible MMIO.

Optional revision argument replays the old source to reproduce its invalid read.
No GPU, remote host, or Apple binary is used.
"""
from pathlib import Path
import re, subprocess, tempfile, sys
repo = Path(__file__).resolve().parents[1]
source = (repo / "WhateverXe/kern_xe.cpp").read_text()
header = (repo / "WhateverXe/kern_xe.hpp").read_text()
if len(sys.argv) > 1:
    source = subprocess.check_output(["git", "-C", str(repo), "show",
        sys.argv[1] + ":WhateverXe/kern_xe.cpp"], text=True)
    header = subprocess.check_output(["git", "-C", str(repo), "show",
        sys.argv[1] + ":WhateverXe/kern_xe.hpp"], text=True)
patterns = [r"void tglAUXRecord\(.*?\n\}",
    r"void XE::wrapTGLEnableAUXPower\(.*?\n\}",
    r"void XE::wrapTGLDisableAUXPower\(.*?\n\}"]
body = "\n".join(re.search(pattern, source, re.S).group() for pattern in patterns)
fixture = (repo / "tests/tgl_aux_observer_fixture.cpp").read_text()
fields = header[header.index("\tbool tglAUXParentReady"):header.index("\tstatic void wrapTGLEnableAUXPower")]
fixture = fixture.replace("// AUX_FIELDS", fields)
setup="object.tglAUXParentReady=true;"
if "tglTCWiring" in fields:setup+="object.tglTCWiring=TGLTCWiring::fromMasks(0x10,0,0);"
fixture=fixture.replace("int main() {","int main() {\n "+setup)
if "tglAUXParentRefs[9]" not in fields:
    fixture = fixture.replace("object.tglAUXParentRefs[4]", "object.tglAUXParentRefs")
with tempfile.TemporaryDirectory(prefix="tgl-aux-observer-") as directory:
    tmp = Path(directory)
    (tmp / "check.cpp").write_text(fixture.replace("// PRODUCTION_FUNCTIONS", body))
    subprocess.run(["clang++", "-std=c++14", "-O1", "-g", "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer", "-I", str(repo/"WhateverXe"), str(tmp / "check.cpp"), "-o", str(tmp / "check")], check=True)
    subprocess.run([str(tmp / "check")], check=True)
