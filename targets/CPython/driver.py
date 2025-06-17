import inspect
import json
import builtins
from annotationlib import get_annotations, Format
from typing import Any

_isbuiltin = inspect.isbuiltin
_isclass = inspect.isclass
_sig = inspect.signature

BLACKLIST = {
    "eval",
    "exec",
    "compile",
    "print",
    "input",
    "open",
    "help",
    "pow",
    "__pow__",
    "__rpow__",
}


def resolve_annotations(obj):
    return get_annotations(obj, format=Format.VALUE)


def extract_signature(obj, clsname=None, method_type="instance"):
    try:
        sig = _sig(obj)
    except (TypeError, ValueError):
        return {
            "paramTypes": ["object", "object"],
            "selfType": clsname if method_type != "static" else None,
            "returnType": "object",
        }

    try:
        ann = resolve_annotations(obj)
    except Exception:
        ann = {}

    param_types = []
    for i, (name, param) in enumerate(sig.parameters.items()):
        if i == 0 and name in ("self", "cls") and method_type != "static":
            continue
        param_types.append(type_name(ann.get(name, Any)))

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
    return getattr(ann, "__forward_arg__", getattr(ann, "__name__", str(ann)))


def collect_class_methods(cls, qualified_name=None):
    methods = {}
    if not inspect.isclass(cls):
        return methods

    clsname = qualified_name or cls.__name__
    for attr_name, attr in cls.__dict__.items():
        if attr_name in BLACKLIST:
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
            continue

        full_name = f"{clsname}.{attr_name}"
        sig = extract_signature(real_func, clsname, method_type)
        methods[full_name] = sig

    return methods


def collect_all(enable_builtins=False, results=None):
    if results is None:
        results = {"funcs": {}, "types": []}
    if enable_builtins:
        for name in dir(builtins):
            obj = getattr(builtins, name)
            if _isbuiltin(obj) and name not in BLACKLIST:
                results["funcs"][name] = extract_signature(obj)
            if _isclass(obj):
                results["funcs"].update(collect_class_methods(obj, name))
    else:
        for name, obj in globals().items():
            if _isclass(obj):
                results["types"].append(
                    obj.__class__.__module__ + "." + obj.__class__.__qualname__
                )
                results["funcs"].update(collect_class_methods(obj, name))

    return results


if __name__ == "__main__":
    global result
    result = json.dumps(collect_all())
