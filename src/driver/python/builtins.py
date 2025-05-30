import builtins
import inspect
import json
import sys
from driver import collect_class_methods

if __name__ == "__main__":
    results = {}
    for name in dir(builtins):
        obj = getattr(builtins, name)
        if inspect.isclass(obj):
            results.update(collect_class_methods(obj, name))
    json.dump(results, open(sys.argv[1], "w"), indent=2)
