import inspect
import json
import sys
import builtins

BLACKLIST = {
    "eval",
    "exec",
    "compile",
    "print",
    "input",
    "open",
    "help",
}


def normalize_type(ann):
    if ann is inspect.Signature.empty:
        return "object"
    elif isinstance(ann, type):
        return ann.__name__
    else:
        return str(ann)


def index_type(name: str, types: list[str]) -> int:
    return types.index(name)


def extract_signature(obj, types, clsname=None, method_type="instance"):
    try:
        sig = inspect.signature(obj)
        param_types = []
        self_type = (
            -1
            if method_type == "static"
            else index_type(clsname, types) if clsname else -1
        )

        for i, (pname, param) in enumerate(sig.parameters.items()):
            ann = normalize_type(param.annotation)
            ann = "object" if ann in ("Any", "object") else ann
            if i == 0 and pname in ("self", "cls") and method_type != "static":
                continue
            param_types.append(index_type(ann, types))

        return_type = normalize_type(sig.return_annotation)
        return_type = "object" if return_type in ("Any", "object") else return_type

        return {
            "paramTypes": param_types,
            "selfType": self_type,
            "returnType": index_type(return_type, types),
        }

    except Exception:
        return {
            "paramTypes": [index_type("object", types), index_type("object", types)],
            "selfType": (
                -1  # no return
                if method_type == "static"
                else index_type(clsname, types) if clsname else -1
            ),
            "returnType": index_type("object", types),
        }


def collect_class_methods(cls, types, qualified_name=None):
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
        sig = extract_signature(real_func, types, clsname, method_type)
        methods[full_name] = sig

    return methods


def collect_all(enable_builtins=False):
    results = {"funcs": {}, "types": ["object"]}

    if enable_builtins:
        for name in dir(builtins):
            obj = getattr(builtins, name)
            if inspect.isclass(obj):
                results["types"].append(name)

    for name, obj in globals().items():
        if inspect.isclass(obj):
            results["types"].append(name)

    if enable_builtins:
        for name in dir(builtins):
            obj = getattr(builtins, name)
            if inspect.isbuiltin(obj) and name not in BLACKLIST:
                results["funcs"][name] = extract_signature(obj, results["types"])
            if inspect.isclass(obj):
                results["funcs"].update(
                    collect_class_methods(obj, results["types"], name)
                )

    for name, obj in globals().items():
        if inspect.isclass(obj):
            results["funcs"].update(collect_class_methods(obj, results["types"], name))

    return results


if __name__ == "__main__":
    result = collect_all()
