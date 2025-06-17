import json
import sys
import builtins
import operator
import inspect
from driver import collect_all
import warnings

# ignore exactly the "~ on bool" deprecation
warnings.filterwarnings(
    "ignore",
    category=DeprecationWarning,
)

OPS = [
    operator.add,
    operator.sub,
    operator.mul,
    operator.truediv,
    operator.mod,
    operator.pow,
    operator.floordiv,
    operator.eq,
    operator.ne,
    operator.lt,
    operator.gt,
    operator.le,
    operator.ge,
    operator.and_,
    operator.or_,
    operator.xor,
    operator.lshift,
    operator.rshift,
]
UOPS = [operator.neg, operator.not_, operator.inv]


def try_construct_dummy(cls):
    try:
        return cls()
    except Exception:
        init = getattr(cls, "__init__", None) or getattr(cls, "__new__", None)
        if not init:
            return None
        sig = inspect.signature(init)
        params = [
            p
            for name, p in sig.parameters.items()
            if name not in ("self", "cls")
            and p.kind
            in (
                inspect.Parameter.POSITIONAL_ONLY,
                inspect.Parameter.POSITIONAL_OR_KEYWORD,
            )
            and p.default is inspect.Parameter.empty
        ]
        args = [object()] * len(params)
        try:
            return cls(*args)
        except Exception:
            return None


def convert_types_to_index(results: dict):
    type_list = results["types"]
    idx_map = {name: i for i, name in enumerate(type_list) if name}

    def get_index(type_name: str) -> int:
        if not type_name:
            return -1
        if type_name not in idx_map:
            idx_map[type_name] = len(type_list)
            type_list.append(type_name)
        return idx_map[type_name]

    for sig in results["funcs"].values():
        sig["paramTypes"] = [get_index(t) for t in sig["paramTypes"]]
        sig["returnType"] = get_index(sig["returnType"])
        sig["selfType"] = get_index(sig["selfType"] or "")


def safe_call(op, *args) -> bool:
    try:
        op(*args)
        return True
    except Exception:
        return False


if __name__ == "__main__":
    results = collect_all(
        enable_builtins=True, results={"funcs": {}, "types": ["object"]}
    )

    convert_types_to_index(results)

    constructors = []
    for name in results["types"]:
        cls = eval(name, builtins.__dict__, globals())
        constructors.append(cls)

    instances = [try_construct_dummy(ctor) for ctor in constructors]

    results["ops"] = []
    for op in OPS:
        table = [[]]
        for i in range(1, len(instances)):
            success = [
                j
                for j in range(1, len(instances))
                if safe_call(op, instances[i], instances[j])
            ]
            table.append(success)
        results["ops"].append(table)

    results["uops"] = [[]]
    for uop in UOPS:
        results["uops"].append(
            [i for i in range(1, len(instances)) if safe_call(uop, instances[i])]
        )

    with open(sys.argv[1], "w") as f:
        json.dump(results, f, indent=2)
