"""Unit tests for ./cb (deepseek-flash slice).

Kept importable both ways:
  python3 -m unittest discover -s tools/tests -t .            # CI / ./cb verify (cli-tests step)
  python3 -m unittest discover -s tools/tests -t tools/tests  # ad-hoc, same tests

Putting this directory on sys.path lets every module keep a plain ``import support``.
"""

import sys
from pathlib import Path

_HERE = str(Path(__file__).resolve().parent)
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)
