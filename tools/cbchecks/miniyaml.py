"""Minimal, strict-enough YAML subset parser (stdlib only).

`./cb verify` must read ESP-IDF `dependencies.lock` files without a third-party YAML dependency.
Those files are block-style maps/lists with quoted scalars; a regex that picks "the version after the
name" reads the *transitive* range (`version: '>=8,<10'`) instead of the resolved component version,
so we parse indentation properly instead.

Supported: nested block maps, block lists (including `- key: value` items and lists indented at the
same column as their parent key), `key: []`, `key: {}`, quoted scalars, inline `# comments`.
Unsupported (raises ValueError): flow collections with content, multi-line scalars, anchors.
"""

from __future__ import annotations

from typing import Any

_QUOTES = ("'", '"')


def _strip_comment(text: str) -> str:
    """Remove a trailing `# ...` comment that is not inside quotes."""
    quote: str | None = None
    for index, char in enumerate(text):
        if quote:
            if char == quote:
                quote = None
        elif char in _QUOTES:
            quote = char
        elif char == "#" and (index == 0 or text[index - 1] in " \t"):
            return text[:index]
    return text


def _scalar(text: str) -> Any:
    text = text.strip()
    if not text:
        return None
    if text.startswith("[") and text.endswith("]"):
        inner = text[1:-1].strip()
        if not inner:
            return []
        return [_scalar(part) for part in inner.split(",")]
    if text.startswith("{") and text.endswith("}"):
        inner = text[1:-1].strip()
        if not inner:
            return {}
        pair_map: dict[str, Any] = {}
        for part in inner.split(","):
            key, _, value = part.partition(":")
            pair_map[key.strip().strip("'\"")] = _scalar(value)
        return pair_map
    if len(text) >= 2 and text[0] == text[-1] and text[0] in _QUOTES:
        return text[1:-1]
    if text in ("true", "True"):
        return True
    if text in ("false", "False"):
        return False
    if text in ("null", "~", "None"):
        return None
    return text


class _Lines:
    def __init__(self, text: str):
        self.items: list[tuple[int, str]] = []
        for raw in text.splitlines():
            if not raw.strip():
                continue
            body = _strip_comment(raw)
            if not body.strip():
                continue
            stripped = body.lstrip(" ")
            indent = len(body) - len(stripped)
            if "\t" in body[:indent]:
                raise ValueError("tab indentation is not supported")
            self.items.append((indent, stripped.rstrip()))
        self.index = 0

    def peek(self) -> tuple[int, str] | None:
        return self.items[self.index] if self.index < len(self.items) else None


def _parse_map(lines: _Lines, indent: int) -> dict[str, Any]:
    result: dict[str, Any] = {}
    while True:
        current = lines.peek()
        if current is None or current[0] != indent:
            return result
        line_indent, text = current
        if text.startswith("- "):
            return result  # list at this indent belongs to the caller
        key, sep, rest = text.partition(":")
        if not sep:
            raise ValueError(f"expected 'key: value', got {text!r}")
        key = key.strip().strip("'\"")
        rest = rest.strip()
        lines.index += 1
        if rest:
            result[key] = _scalar(rest)
            continue
        nxt = lines.peek()
        if nxt is None:
            result[key] = None
            continue
        next_indent, next_text = nxt
        if next_indent > line_indent:
            result[key] = _parse_map(lines, next_indent) if not next_text.startswith("- ") \
                else _parse_list(lines, next_indent)
        elif next_indent == line_indent and next_text.startswith("- "):
            # YAML allows a block sequence at the same column as its parent key.
            result[key] = _parse_list(lines, next_indent)
        else:
            result[key] = None


def _parse_list(lines: _Lines, indent: int) -> list[Any]:
    items: list[Any] = []
    while True:
        current = lines.peek()
        if current is None or current[0] != indent or not current[1].startswith("- "):
            return items
        _, text = current
        content = text[2:].strip()
        lines.index += 1
        if ":" in content and not content.startswith(("{", "[")):
            key, _, rest = content.partition(":")
            item: dict[str, Any] = {key.strip().strip("'\""): _scalar(rest)}
            nxt = lines.peek()
            if nxt is not None and nxt[0] > indent:
                item.update(_parse_map(lines, nxt[0]))
            items.append(item)
        else:
            items.append(_scalar(content))


def parse(text: str) -> Any:
    lines = _Lines(text)
    if not lines.items:
        return {}
    first_indent, first_text = lines.items[0]
    if first_indent != 0:
        raise ValueError(f"document must start at indentation 0, got {first_indent}")
    if first_text.startswith("- "):
        return _parse_list(lines, 0)
    return _parse_map(lines, 0)


def get_path(data: Any, dotted: str) -> Any:
    """Walk a dotted path; returns None when any segment is missing."""
    node = data
    for segment in dotted.split("."):
        if not isinstance(node, dict) or segment not in node:
            return None
        node = node[segment]
    return node
