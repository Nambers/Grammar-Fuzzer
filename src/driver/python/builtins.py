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
        sig["selfType"] = get_index(sig["selfType"]) if sig["selfType"] is not None else -1


if __name__ == "__main__":
    results = collect_all(enable_builtins=True, results={"funcs": {}, "types": ["object"]})
    convert_types_to_index(results)
    json.dump(results, open(sys.argv[1], "w"), indent=2)
