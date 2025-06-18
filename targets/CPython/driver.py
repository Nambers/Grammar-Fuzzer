import inspect
import json
import builtins
from annotationlib import get_annotations, Format
from typing import Any
import re

_isclass = inspect.isclass
_sig = inspect.signature
_isbuiltiin = inspect.isbuiltin

BLACKLIST = {
    "eval",
    "exec",
    "compile",
    "print",
    "input",
    "open",
    "help",
    "pow",
    "Ellipsis",
    "NotImplemented",
    "Error",
    "__",
}


def resolve_annotations(obj):
    return get_annotations(obj, format=Format.VALUE)


def _parse_text_sig(txt: str) -> dict:
    # e.g.
    # >>> int.to_bytes.__text_signature__
    # "($self, /, length=1, byteorder='big', *, signed=False)"

    if not txt or txt[0] != "(" or txt[-1] != ")":
        return {"pos_or_kw": 0, "kw_only": 0}
    m = re.match(r"\(([^)]*)\)", (txt or "").strip())
    if not m:
        return {"pos_or_kw": 0, "kw_only": 0}
    pos_or_kw = 0
    kw_only = 0
    seen_star = False
    for part in m.group(1).split(","):
        p = part.strip()
        if not p or p in ("/",):
            continue
        if p == "*":
            seen_star = True
            continue
        if seen_star:
            kw_only += 1
        else:
            pos_or_kw += 1
    return {"pos_or_kw": pos_or_kw, "kw_only": kw_only}


def extract_signature(obj, clsname=None, method_type="instance"):
    try:
        sig = _sig(obj)
    except (TypeError, ValueError):
        # fallback: use __text_signature__ (e.g. bytes.hex will fail by _sig)
        txtsig = getattr(obj, "__text_signature__", None)
        counts = _parse_text_sig(txtsig) if txtsig else None
        if counts is None or (counts["pos_or_kw"] + counts["kw_only"]) == 0:
            doc = getattr(obj, "__doc__", "") or ""
            first = doc.splitlines()[0] if doc else ""
            counts = _parse_text_sig(first)
        total = counts["pos_or_kw"] + counts["kw_only"]
        return {
            "paramTypes": ["object"] * total,
            "selfType": clsname if method_type != "static" else None,
            "returnType": "object",
        }
    ann = resolve_annotations(obj)

    param_types = []
    for i, (name, param) in enumerate(sig.parameters.items()):
        if i == 0 and name in ("self", "cls") and method_type != "static":
            continue
        if param.kind in (param.VAR_POSITIONAL, param.VAR_KEYWORD):
            continue

        if param.annotation is inspect.Parameter.empty:
            if param.default is not inspect.Parameter.empty:
                param_types.append(type_name(param.default))
            else:
                param_types.append(type_name(ann.get(name, Any)))
        else:
            param_types.append(type_name(param.annotation))

    return_type = type_name(ann.get("return", Any))
    self_type = clsname if method_type != "static" else None

    return {
        "paramTypes": param_types,
        "selfType": self_type,
        "returnType": return_type,
    }


def type_name(ann):
    if ann in (inspect.Signature.empty, None, Any):
        return "object"
    return getattr(ann, "__forward_arg__", getattr(type(ann), "__name__", "object"))


def collect_class_methods(cls, qualified_name=None):
    methods = {}
    if not inspect.isclass(cls):
        return methods

    clsname = qualified_name or cls.__name__
    methods[clsname] = []
    for attr_name, attr in cls.__dict__.items():
        if attr_name in BLACKLIST or any(a in attr_name for a in BLACKLIST):
            continue
        method_type = "instance"
        if isinstance(attr, staticmethod):
            real_func = attr.__func__
            method_type = "static"
        elif isinstance(attr, classmethod):
            real_func = attr.__func__
            method_type = "class"
        else:
            real_func = attr

        if not callable(real_func):
            methods[clsname].append(
                {"name": attr_name, "type": type_name(real_func), "isCallable": False}
            )
        else:
            sig = extract_signature(real_func, clsname, method_type)
            methods[clsname].append(
                {"name": attr_name, "funcSig": sig, "isCallable": True}
            )

    return methods


def collect_all(enable_builtins=False, results=None):
    if results is None:
        results = {"funcs": {}, "types": []}
    if enable_builtins:
        results["funcs"]["-1"] = []  # Default for builtins
        for name in dir(builtins):
            obj = getattr(builtins, name)
            if name not in BLACKLIST and not any(a in name for a in BLACKLIST):
                if _isclass(obj):
                    results["funcs"].update(collect_class_methods(obj, name))
                else:
                    if callable(obj):
                        results["funcs"]["-1"].append(
                            {
                                "name": name,
                                "funcSig": extract_signature(obj),
                                "isCallable": True,
                            }
                        )
                    else:
                        results["funcs"]["-1"].append(
                            {"name": name, "type": type_name(obj), "isCallable": False}
                        )
    else:
        for name, obj in globals().items():
            if _isclass(obj) and not _isbuiltiin(obj) and name not in BLACKLIST:
                results["types"].append(
                    obj.__class__.__module__ + "." + obj.__class__.__qualname__
                )
                results["funcs"].update(collect_class_methods(obj, name))

    return results


if __name__ == "__main__":
    global result
    result = json.dumps(collect_all())
