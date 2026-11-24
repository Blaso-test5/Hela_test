#!/usr/bin/env python3
"""Genera gli stub COM (d3d12_stubs.inc) e il forwarder dello swapchain (dxgi_swapchain_fwd.inc)
leggendo gli header d3d12.h / dxgi*.h. Uso: gen_stubs.py <include_dir> <out_dir>

Gli stub implementano TUTTI i metodi virtuali delle interfacce con un corpo che registra la chiamata
come 'non implementata'; le classi concrete ridefiniscono (override) solo i metodi supportati."""
import re, sys, os, glob

inc, out = sys.argv[1], sys.argv[2]
text = ""
include_dirs = [inc]
if os.path.isdir(os.path.join(inc, "um")):
    include_dirs.append(os.path.join(inc, "um"))
    include_dirs.append(os.path.join(inc, "shared"))
elif os.path.basename(inc).lower() in {"um", "shared"}:
    include_dirs.append(os.path.join(os.path.dirname(inc), "shared" if os.path.basename(inc).lower() == "um" else "um"))
headers = set()
for include_dir in include_dirs:
    headers.update(glob.glob(os.path.join(include_dir, "d3d12.h")))
    headers.update(glob.glob(os.path.join(include_dir, "dxgi*.h")))
for f in sorted(headers):
    text += open(f, encoding="utf-8", errors="ignore").read() + "\n"

def block(name):
    m = re.search(r'^[ \t]*(?:struct |interface )?%s : public (\w+)[ \t]*\n[ \t]*\{(.*?)^[ \t]*\};' % name, text, re.M | re.S)
    if not m:
        raise SystemExit("interfaccia non trovata: " + name)
    parent, body = m.group(1), m.group(2)
    body = re.sub(r'/\*.*?\*/', ' ', body, flags=re.S)
    # tieni solo il ramo #else dei costrutti WIDL_EXPLICIT_AGGREGATE_RETURNS
    body = re.sub(r'#ifdef WIDL_EXPLICIT_AGGREGATE_RETURNS.*?#else\n(.*?)#endif', r'\1', body, flags=re.S)
    # Gli header SDK espongono anche la forma C del vtable per i ritorni aggregate.
    body = re.sub(r'#if defined\(_MSC_VER\) \|\| !defined\(_WIN32\)\s*(.*?)#else.*?#endif', r'\1', body, flags=re.S)
    return parent, body

METH = re.compile(r'virtual\s+(.+?)\s+STDMETHODCALLTYPE\s+(\w+)\(\s*(.*?)\)\s*=\s*0;', re.S)

def own_methods(name):
    parent, body = block(name)
    res = []
    for m in METH.finditer(body):
        ret = " ".join(m.group(1).split())
        args = " ".join(m.group(3).split())
        res.append((ret, m.group(2), args))
    return parent, res

def arg_names(args):
    if not args.strip() or args.strip() == "void":
        return []
    names = []
    for a in args.split(","):
        a = a.strip()
        mm = re.search(r'(\w+)\s*(\[[^\]]*\])?$', a)
        names.append(mm.group(1))
    return names

SKIP = {"IUnknown", "ID3D12Object", "ID3D12DeviceChild", "ID3D12Pageable"}

def chain(name, stop):
    """metodi propri di name e dei suoi antenati fino a 'stop' (escluso), dal piu' base al piu' derivato."""
    levels = []
    cur = name
    while cur not in stop:
        parent, ms = own_methods(cur)
        levels.append(ms)
        cur = parent
    out_ = []
    for ms in reversed(levels):
        out_ += ms
    return out_

def stub_body(ret, name, iface):
    tag = f'"{iface}::{name}"'
    if ret == "void":
        return f"{{ Unimpl({tag}); }}"
    if ret == "HRESULT":
        return f"{{ Unimpl({tag}); return E_NOTIMPL; }}"
    return f"{{ Unimpl({tag}); return {{}}; }}"

def sdk_types(ret, args):
    args = re.sub(r'/\*.*?\*/', ' ', args, flags=re.S)
    cleaned = []
    i = 0
    while i < len(args):
        is_identifier_boundary = i == 0 or not (args[i - 1].isalnum() or args[i - 1] == '_')
        annotation = re.match(r'_[A-Za-z][A-Za-z0-9_]*_', args[i:]) if is_identifier_boundary else None
        if not annotation:
            cleaned.append(args[i])
            i += 1
            continue
        i += annotation.end()
        if i < len(args) and args[i] == '(':
            depth = 1
            i += 1
            while i < len(args) and depth:
                if args[i] == '(':
                    depth += 1
                elif args[i] == ')':
                    depth -= 1
                i += 1
    args = " ".join("".join(cleaned).split())
    return ret.replace("WINBOOL", "BOOL"), args.replace("WINBOOL", "BOOL")

D3D12_IFACES = [
    "ID3D12Heap", "ID3D12Resource", "ID3D12DescriptorHeap", "ID3D12QueryHeap", "ID3D12CommandSignature",
    "ID3D12GraphicsCommandList", "ID3D12CommandQueue", "ID3D12RootSignature", "ID3D12PipelineState",
    "ID3D12Fence", "ID3D12CommandAllocator", "ID3D12Device2",
]

o = ["// GENERATO da tools/gen_stubs.py - non modificare a mano.", "#pragma once", ""]
for i in D3D12_IFACES:
    ms = chain(i, SKIP)
    o.append(f"struct Stub_{i} : Obj<{i}> {{")
    o.append(f"    using Obj<{i}>::Obj;")
    for ret, n, a in ms:
        ret, a = sdk_types(ret, a)
        o.append(f"    virtual {ret} STDMETHODCALLTYPE {n}({a}) {stub_body(ret, n, i)}")
    o.append("};\n")
open(os.path.join(out, "d3d12_stubs.inc"), "w").write("\n".join(o))

# ---- forwarder IDXGISwapChain4 ----
ms = chain("IDXGISwapChain4", {"IUnknown"})
f = ["// GENERATO da tools/gen_stubs.py - non modificare a mano.", "#pragma once", "",
     "struct SwapChainFwd : IDXGISwapChain4 {", "    IDXGISwapChain4* real = nullptr;"]
for ret, n, a in ms:
    ret, a = sdk_types(ret, a)
    names = ", ".join(arg_names(a))
    call = f"real->{n}({names})"
    f.append(f"    virtual {ret} STDMETHODCALLTYPE {n}({a}) {{ " + ("" if ret == "void" else "return ") + call + "; }")
f.append("};\n")
open(os.path.join(out, "dxgi_swapchain_fwd.inc"), "w").write("\n".join(f))

# ---- forwarder IDXGIFactory7 ----
factory_methods = chain("IDXGIFactory7", {"IUnknown"})
factory = ["// GENERATO da tools/gen_stubs.py - non modificare a mano.", "#pragma once", "",
           "struct FactoryFwd : IDXGIFactory7 {", "    IDXGIFactory7* real = nullptr;"]
for ret, n, a in factory_methods:
    ret, a = sdk_types(ret, a)
    names = ", ".join(arg_names(a))
    call = f"real->{n}({names})"
    factory.append(f"    virtual {ret} STDMETHODCALLTYPE {n}({a}) {{ " +
                   ("" if ret == "void" else "return ") + call + "; }")
factory.append("};\n")
open(os.path.join(out, "dxgi_factory_fwd.inc"), "w").write("\n".join(factory))
print("ok", len(ms), "metodi swapchain,", len(factory_methods), "metodi factory")
