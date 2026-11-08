"""Error types and exit-code mapping for ./cb.

Exit codes are frozen by CONTRACT.md: 0 ok, 1 build/test failure, 2 board/port absent,
3 usage or environment problem.
"""

CODE_OK = 0
CODE_FAIL = 1
CODE_BOARD = 2
CODE_USAGE = 3


class CbError(Exception):
    """Base class for all user-visible failures."""

    code = CODE_FAIL

    def __init__(self, message: str):
        super().__init__(message)
        self.message = message


class BuildError(CbError):
    """A build/test command failed (exit 1)."""

    code = CODE_FAIL


class BoardError(CbError):
    """No usable board / port (exit 2)."""

    code = CODE_BOARD


class UsageError(CbError):
    """Bad argument or unknown target (exit 3)."""

    code = CODE_USAGE


class EnvError(CbError):
    """Toolchain or repository layout problem (exit 3)."""

    code = CODE_USAGE


def code_of(exc: BaseException) -> int:
    return getattr(exc, "code", CODE_FAIL)
