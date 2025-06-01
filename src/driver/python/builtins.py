import json
import sys
from driver import collect_all


if __name__ == "__main__":
    json.dump(collect_all(enable_builtins=True), open(sys.argv[1], "w"), indent=2)
