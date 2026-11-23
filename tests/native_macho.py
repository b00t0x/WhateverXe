"""Small RAM loader for fixed x86_64 kext replays; never loads a kernel kext.

Only explicitly bound imports may execute. Kernel services and physical devices
remain caller-defined boundaries; an unexpected import terminates the replay.
"""
import ctypes as C
import mmap
import os
from pathlib import Path
import struct


def address(value):
    return C.addressof(C.c_char.from_buffer(value))


def put(buf, offset, value, kind='Q'):
    struct.pack_into('<'+kind, buf, offset, value)


def get(buf, offset, kind='I'):
    return struct.unpack_from('<'+kind, buf, offset)[0]


class Image:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        assert get(self.data, 0) == 0xfeedfacf and get(self.data, 4) == 0x1000007
        segments, commands = [], {}
        pos = 32
        for _ in range(get(self.data, 16)):
            cmd, size = struct.unpack_from('<II', self.data, pos)
            commands[cmd] = pos
            if cmd == 0x19:
                segments.append(struct.unpack_from('<16sQQQQ', self.data, pos+8))
            pos += size
        end = max(vm+length for _, vm, length, _, _ in segments)
        self.thunk = (end+4095) & ~4095
        self.mem = mmap.mmap(-1, self.thunk+0x40000,
                            prot=mmap.PROT_READ | mmap.PROT_WRITE | mmap.PROT_EXEC)
        self.base = address(self.mem)
        for _, vm, _, off, length in segments:
            self.mem[vm:vm+length] = self.data[off:off+length]
        symoff, count, stroff, _ = struct.unpack_from('<4I', self.data, commands[2]+8)
        self.names, self.symbols = [], {}
        for i in range(count):
            string, typ, _, _, value = struct.unpack_from('<IBBHQ', self.data, symoff+16*i)
            name = self.data[stroff+string:].split(b'\0', 1)[0].decode()
            self.names.append(name)
            if typ & 0x0e:
                self.symbols[name] = value
        self.callbacks = []
        self.external = []
        dysym = commands[0xb]
        for external, p in ((True, dysym+64), (False, dysym+72)):
            off, count = struct.unpack_from('<II', self.data, p)
            for i in range(count):
                site, info = struct.unpack_from('<II', self.data, off+8*i)
                if external:
                    self.external.append((site, info, self.names[info & 0xffffff]))
                elif info >> 28 == 0 and (info >> 25) & 3 == 3:
                    put(self.mem, site, get(self.mem, site, 'Q')+self.base)

    def ptr(self, name):
        return self.base+self.symbols[name]

    def function(self, name, result, *args):
        return C.CFUNCTYPE(result, *args)(self.ptr(name))

    def callback(self, result, args, body):
        cb = C.CFUNCTYPE(result, *args)(body)
        self.callbacks.append(cb)
        return C.cast(cb, C.c_void_p).value

    def jump(self, destination):
        site = self.thunk
        self.thunk += 16
        assert self.thunk <= len(self.mem)
        self.mem[site:site+12] = b'\x48\xb8'+struct.pack('<Q', destination)+b'\xff\xe0'
        return self.base+site

    def override(self, name, destination):
        off = self.symbols[name]
        self.mem[off:off+12] = b'\x48\xb8'+struct.pack('<Q', destination)+b'\xff\xe0'

    def link(self, others=(), bindings=None):
        bindings = bindings or {}
        thunks = {}
        resolved = {}
        for site, info, name in self.external:
            target = bindings.get(name, resolved.get(name))
            if target is None:
                target = next((other.ptr(name) for other in (self, *others)
                               if name in other.symbols), None)
            if target is None:
                def missing(name=name):
                    os.write(2, ('Unexpected native import: '+name+'\n').encode())
                    os._exit(97)
                target = self.callback(None, (), missing)
            resolved[name] = target
            kind, length = info >> 28, (info >> 25) & 3
            if kind == 0 and length == 3:
                put(self.mem, site, target+get(self.mem, site, 'Q'))
            elif kind == 2 and length == 2:
                if name not in thunks:
                    thunks[name] = self.jump(target)
                put(self.mem, site, thunks[name]-(self.base+site+4), 'i')
            else:
                raise AssertionError(('unsupported native relocation', hex(site), hex(info), name))
