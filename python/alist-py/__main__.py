from .alist import AListParser
from .parse_file import parse_file

import sys, json

def main():
    data = parse_file(sys.stdin, AListParser())
    if data is not None:
        sys.stdout.write(json.dumps(data, sort_keys = True, indent = 4, separators = (',', ':')))

main()
