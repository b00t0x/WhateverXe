"""Read production source, expanding same-translation-unit implementation parts.

Replay tests must extract the compiled implementation rather than a stale copy.
Ordinary headers stay as includes and are resolved by the C++ fixture compiler.
"""
import re
from pathlib import Path


def read_source(path):
    path = Path(path)
    return re.sub(r'^#include "([^"\n]+\.inc)"[ \t]*$',
                  lambda m: read_source(path.parent / m[1]),
                  path.read_text(), flags=re.M)
