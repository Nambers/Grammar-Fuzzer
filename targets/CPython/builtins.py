import json
import sys
from driver import collect_all


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
    OPS = [
        "+",
        "-",
        "*",
        "/",
        "%",
        "**",
        "//",
        "==",
        "!=",
        "<",
        ">",
        "<=",
        ">=",
        "&",
        "|",
        "^",
        "<<",
        ">>",
    ]
    op_maps = results["ops"]
    # for every types in results["types"], test accepted other types
    for op in OPS:
        re = [[]] # first element is object
        for t1 in range(1, len(results["types"])):
            res = []
            for t2 in range(1, len(results["types"])):
                try:
                    eval(f"{results['types'][t1]}() {op} {results['types'][t2]}()")
                except Exception:
                    continue
                res.append(t2)
            re.append(res)
        op_maps.append(re)


if __name__ == "__main__":
    results = collect_all(
        enable_builtins=True, results={"funcs": {}, "types": ["object"]}
    )
    convert_types_to_index(results)
    results["ops"] = []
    collect_ops(results)
    json.dump(results, open(sys.argv[1], "w"), indent=2)
