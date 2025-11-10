#!/usr/bin/env python3

from lark import Lark, logger
import sys, pathlib
import logging


logging.basicConfig(level=logging.DEBUG)


logger.setLevel(logging.DEBUG)


PARSER = "earley"
# PARSER = "lalr"

lark_grammar = sys.argv[1]

# NOTE: run from the repo root
grammar = pathlib.Path(lark_grammar).read_text()
parser_kwargs = dict(start="start", parser=PARSER, debug=True)
if PARSER == "earley":
    parser_kwargs["ambiguity"] = "resolve"
parser = Lark(grammar, **parser_kwargs)


def validate(path):
    src = pathlib.Path(path).read_text()
    try:
        tree = parser.parse(src)
        print(f"OK: {path}")

        print(tree.pretty())
    except Exception as e:
        # Lark exceptions include line/column; print a compact message
        print(f"ERROR: {path}\n{e}", file=sys.stderr)
        return False

    return True


if __name__ == "__main__":
    for p in sys.argv[2:]:
        print(f"Validating {p} with {PARSER} parser")
        if not validate(p):
            exit(1)
