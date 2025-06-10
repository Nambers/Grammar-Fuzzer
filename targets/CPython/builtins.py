import json
import sys
from driver import collect_all
import operator

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

UOPS = [
    operator.neg,
    operator.not_,
    operator.inv,
]


def convert_types_to_index(results: dict):
    type_list = results["types"]
    for sig in results["funcs"].values():

        def get_index(type_name):
            if type_name == "":
                return -1
            if type_name not in type_list:
                type_list.append(type_name)
            return type_list.index(type_name)

        sig["paramTypes"] = [get_index(t) for t in sig["paramTypes"]]
        sig["returnType"] = get_index(sig["returnType"])
        sig["selfType"] = (
            get_index(sig["selfType"]) if sig["selfType"] is not None else -1
        )


def collect_ops(results: dict):
    op_maps = results["ops"]
    for op in OPS:
        re = [[]]  # first element is object
        for t1 in range(len(results["types"]) - 1):
            res = []
            for t2 in range(len(results["types"]) - 1):
                try:
                    op(
                        eval(results["types"][t1])(),
                        eval(results["types"][t2])(),
                    )
                except Exception:
                    continue
                res.append(t2)
            re.append(res)
        op_maps.append(re)

def collect_uops(results: dict):
    op_maps = results["uops"]
    op_maps.append([])  # first element is object
    for op in UOPS:
        re = []  # first element is object
        for t1 in range(1, len(results["types"])):
            try:
                op(eval(results["types"][t1])())
            except Exception:
                continue
            re.append(t1)
        op_maps.append(re)


if __name__ == "__main__":
    results = collect_all(
        enable_builtins=True, results={"funcs": {}, "types": ["object"]}
    )
    convert_types_to_index(results)
    results["ops"] = []
    collect_ops(results)
    results["uops"] = []
    collect_uops(results)
    json.dump(results, open(sys.argv[1], "w"), indent=2)
