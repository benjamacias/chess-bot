#!/usr/bin/env python3
"""Valida legalidad de líneas BookLine en opening_book_improved.cpp.

Uso:
  python engine/validate_book.py
  python engine/validate_book.py --book-file engine/opening_book_improved.cpp
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

try:
    import chess
except ImportError:
    print("ERROR: falta python-chess. Instala con: pip install python-chess")
    sys.exit(1)

LINE_RE = re.compile(
    r"\{\s*(?:MAIN_LINE|GOOD_ALT|\d+)\s*,\s*\{([^}]*)\}\s*\}",
    re.MULTILINE,
)
MOVE_RE = re.compile(r'"([a-h][1-8][a-h][1-8][qrbn]?)"')


def parse_book_lines(cpp_path: Path) -> list[list[str]]:
    text = cpp_path.read_text(encoding="utf-8")
    blocks = LINE_RE.findall(text)
    lines: list[list[str]] = []
    for block in blocks:
        moves = MOVE_RE.findall(block)
        if moves:
            lines.append(moves)
    return lines


def validate_lines(lines: list[list[str]]) -> int:
    errors = 0
    key_collisions: dict[str, set[str]] = defaultdict(set)

    for idx, pv in enumerate(lines, start=1):
        board = chess.Board()
        hist: list[str] = []

        for ply, uci in enumerate(pv, start=1):
            move = chess.Move.from_uci(uci)
            if move not in board.legal_moves:
                print(f"❌ Línea {idx}, ply {ply}: movimiento ilegal {uci} después de {' '.join(hist) or '<startpos>'}")
                errors += 1
                break

            prefix = " ".join(hist)
            key_collisions[prefix].add(uci)
            board.push(move)
            hist.append(uci)

    multi = {k: v for k, v in key_collisions.items() if len(v) > 1}

    print(f"Líneas analizadas: {len(lines)}")
    print(f"Prefijos con múltiples candidatos: {len(multi)}")
    if multi:
        print("Ejemplos de colisiones:")
        for i, (k, v) in enumerate(sorted(multi.items())[:8], start=1):
            pfx = k if k else "<startpos>"
            print(f"  {i}. {pfx} -> {sorted(v)}")

    if errors == 0:
        print("✅ Todas las PV son legales desde startpos")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Validar BookLine PVs")
    parser.add_argument("--book-file", default="engine/opening_book_improved.cpp")
    args = parser.parse_args()

    cpp_path = Path(args.book_file)
    if not cpp_path.exists():
        print(f"ERROR: no existe {cpp_path}")
        return 2

    lines = parse_book_lines(cpp_path)
    if not lines:
        print("ERROR: no se pudieron parsear líneas BookLine")
        return 2

    return 1 if validate_lines(lines) else 0


if __name__ == "__main__":
    raise SystemExit(main())
