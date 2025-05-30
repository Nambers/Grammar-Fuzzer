import builtins
import inspect
import json
import sys


def normalize_type(ann):
    if ann is inspect.Signature.empty:
        return "object"
    elif isinstance(ann, type):
        return ann.__name__
    else:
        return str(ann)


def extract_signature(obj, clsname=None, method_type="instance"):
    try:
        sig = inspect.signature(obj)
        param_types = []
        self_type = None if method_type == "static" else clsname

        for i, (pname, param) in enumerate(sig.parameters.items()):
            ann = normalize_type(param.annotation)
            if i == 0 and pname in ("self", "cls") and method_type != "static":
                # consume self/cls, already handled
                continue
            else:
                param_types.append("object" if ann in ("Any", "object") else ann)

        return_type = normalize_type(sig.return_annotation)
        return_type = "object" if return_type in ("Any", "object") else return_type

        return {
            "paramTypes": param_types,
            "selfType": self_type,
            "returnType": return_type,
        }

    except Exception:
        return {
            "paramTypes": ["object", "object"],
            "selfType": None if method_type == "static" else clsname,
            "returnType": "object",
        }


def collect_class_methods(cls, qualified_name=None):
    methods = {}
    if not inspect.isclass(cls):
        return methods

    clsname = qualified_name or cls.__name__
    for attr_name, attr in cls.__dict__.items():
        if attr_name.startswith("__") and attr_name.endswith("__"):
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


if __name__ == "__main__":
    results = {}
    user_ctx = dict(globals())
    for name, obj in user_ctx.items():
        if inspect.isclass(obj):
            results.update(collect_class_methods(obj, name))
    result = json.dumps(results, indent=2)
