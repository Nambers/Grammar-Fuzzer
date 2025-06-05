import inspect
import json
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


def extract_signature(obj, clsname=None, method_type="instance"):
    param_types = []
    return_type = "object"
    self_type = (
        None
        if method_type == "static"
        else clsname if clsname else None
    )

    try:
        sig = inspect.signature(obj)
    except (TypeError, ValueError):
        # If the object has no signature (e.g., built-in descriptors)
        return {
            "paramTypes": ["object", "object"],
            "selfType": self_type,
            "returnType": "object",
        }

    for i, (pname, param) in enumerate(sig.parameters.items()):
        try:
            ann = normalize_type(param.annotation)
        except Exception:
            ann = "object"
        ann = "object" if ann in ("Any", "object") else ann
        if i == 0 and pname in ("self", "cls") and method_type != "static":
            continue
        param_types.append(ann)

    try:
        return_ann = normalize_type(sig.return_annotation)
    except Exception:
        return_ann = "object"
    return_type = "object" if return_ann in ("Any", "object") else return_ann

    return {
        "paramTypes": param_types,
        "selfType": self_type,
        "returnType": return_type,
    }


def collect_class_methods(cls, qualified_name=None):
    methods = {}
    if not inspect.isclass(cls):
        return methods

    clsname = qualified_name or cls.__name__
    for attr_name, attr in cls.__dict__.items():
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


def collect_all(enable_builtins=False, results={"funcs": {}, "types": []}):
    if not enable_builtins:
        for name, obj in globals().items():
            if inspect.isclass(obj):
                results["types"].append(name)

    if enable_builtins:
        for name in dir(builtins):
            obj = getattr(builtins, name)
            if inspect.isbuiltin(obj) and name not in BLACKLIST:
                results["funcs"][name] = extract_signature(obj)
            if inspect.isclass(obj):
                results["funcs"].update(
                    collect_class_methods(obj, name)
                )

    for name, obj in globals().items():
        if inspect.isclass(obj):
            results["funcs"].update(collect_class_methods(obj, name))

    return results


if __name__ == "__main__":
    global result
    result = json.dumps(collect_all())
