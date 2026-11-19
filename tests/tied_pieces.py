#!/usr/bin/env python3
"""Regression tests for the TiedPieces rule.

This file is intentionally separate from tests/instrumented.py so an
upstream or master merge cannot drop the checks. Assertions use
presence/absence of specific ICCS moves (not perft node counts), so
unrelated move-generator changes from upstream do not fail this suite.

The engine still implements TiedPieces iff:
  * UCI advertises option TiedPieces as a combo (not a string) with
    None / a4a5 / i4i5 / e4e5 — UCCI GUIs such as EThinker only list combos
  * a pawn sitting on a locked square cannot advance
  * a piece sitting on a locked square cannot be captured
  * empty locked squares stay walkable (setup: a3a4 / i3i4 / e3e4)

Run:
  python tests/tied_pieces.py path/to/pikafish
"""

from __future__ import annotations

import argparse
import os
import queue
import subprocess
import sys
import threading
import time
from typing import Dict, Iterable, List, Optional, Tuple

CYAN = "\033[36m"
RED = "\033[31m"
GREEN = "\033[32m"
RESET = "\033[0m"
BOLD = "\033[1m"

PERFT_TIMEOUT = 60.0

# Frozen-pawn combos: (option, setup moves, walk onto empty locked square,
# frozen advance). After the two pawn pushes, Red to move; `frozen` is the
# locked pawn's only forward step.
PAWN_CASES = (
    ("a4a5", "a3a4 a6a5", "a3a4", "a4a5"),
    ("i4i5", "i3i4 i6i5", "i3i4", "i4i5"),
    ("e4e5", "e3e4 e6e5", "e3e4", "e4e5"),
)

# Red rook on a0, black pawn on a4, e6 pawn blocks flying generals.
CAPTURE_FEN = "rnbakabnr/9/1c5c1/4p4/9/p8/9/1C5C1/9/RNBAKABNR w - - 0 1"
CAPTURE_MOVE = "a0a4"


class EngineError(Exception):
    pass


class Engine:
    def __init__(self, path: str) -> None:
        self.path = os.path.abspath(path)
        if not os.path.isfile(self.path):
            raise EngineError(f"engine not found: {self.path}")

        self.process = subprocess.Popen(
            [self.path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            universal_newlines=True,
            bufsize=1,
            cwd=os.path.dirname(self.path),
        )
        self._queue: queue.Queue[Optional[str]] = queue.Queue()
        self._thread = threading.Thread(target=self._read, daemon=True)
        self._thread.start()

    def _read(self) -> None:
        try:
            assert self.process.stdout is not None
            for line in self.process.stdout:
                self._queue.put(line.rstrip("\r\n"))
        except (OSError, ValueError):
            pass
        finally:
            self._queue.put(None)

    def _alive(self) -> None:
        if self.process.poll() is not None:
            raise EngineError("engine process has terminated")

    def send(self, command: str) -> None:
        self._alive()
        assert self.process.stdin is not None
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()

    def lines(self, timeout: float = PERFT_TIMEOUT) -> Iterable[str]:
        deadline = time.monotonic() + timeout
        while True:
            self._alive()
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise EngineError(f"no matching output after {timeout:.0f}s")
            try:
                line = self._queue.get(timeout=remaining)
            except queue.Empty:
                raise EngineError(f"no matching output after {timeout:.0f}s")
            if line is None:
                raise EngineError("engine process has terminated")
            yield line

    def read_until(self, predicate, timeout: float = PERFT_TIMEOUT) -> List[str]:
        collected: List[str] = []
        for line in self.lines(timeout):
            collected.append(line)
            if predicate(line):
                return collected
        raise EngineError("engine closed before expected output")

    def close(self) -> None:
        try:
            self.send("quit")
        except Exception:
            pass
        try:
            if self.process.stdin:
                self.process.stdin.close()
        except Exception:
            pass
        try:
            self.process.wait(timeout=5)
        except Exception:
            self.process.kill()


def parse_perft(lines: List[str]) -> Tuple[Dict[str, int], int]:
    moves: Dict[str, int] = {}
    nodes = None
    for line in lines:
        if line.startswith("Nodes searched:"):
            nodes = int(line.split(":", 1)[1].strip())
            continue
        if line.startswith("info ") or ":" not in line:
            continue
        move, rest = line.rsplit(":", 1)
        move = move.strip()
        rest = rest.strip()
        if rest.isdigit() and len(move) == 4:
            moves[move] = int(rest)
    if nodes is None:
        raise EngineError("perft output missing 'Nodes searched'")
    return moves, nodes


def go_perft(engine: Engine) -> Tuple[Dict[str, int], int]:
    engine.send("go perft 1")
    lines = engine.read_until(lambda ln: ln.startswith("Nodes searched:"))
    return parse_perft(lines)


class TestTiedPieces:
    def __init__(self, engine: Engine) -> None:
        self.engine = engine
        self.uci_lines: List[str] = []

    def setup(self) -> None:
        self.engine.send("uci")
        self.uci_lines = self.engine.read_until(lambda ln: ln == "uciok")
        self.engine.send("isready")
        self.engine.read_until(lambda ln: ln == "readyok")

    def _set_tied(self, value: str) -> None:
        self.engine.send(f"setoption name TiedPieces value {value}")
        # Unknown options print this immediately; drain nothing — the next
        # command's output would still show it. Peek via a cheap isready
        # handshake and fail if the option vanished in a merge.
        self.engine.send("isready")
        lines = self.engine.read_until(lambda ln: ln == "readyok")
        for line in lines:
            if line.startswith("No such option:"):
                raise AssertionError(
                    "UCI option TiedPieces is missing; an upstream merge "
                    "likely dropped add_extended_options() / xqoptions.cpp"
                )

    def _tied_option_line(self) -> str:
        for ln in self.uci_lines:
            if ln.startswith("option name TiedPieces "):
                return ln
        raise AssertionError(
            "uci does not advertise TiedPieces; the option was removed"
        )

    def test_option_advertised(self) -> None:
        self._tied_option_line()

    def test_tied_pieces_is_uci_combo(self) -> None:
        # Combo (3-arg Option ctor), not string. UCCI-era GUIs such as
        # EThinker only list combo options in the settings UI.
        line = self._tied_option_line()
        padded = f" {line} "
        assert " type string " not in padded, (
            "TiedPieces became type string; EThinker will not list it. "
            f"Got: {line}"
        )
        assert " type combo " in padded, (
            "TiedPieces must be advertised as type combo "
            "(Option(\"None var None var a4a5 var i4i5 var e4e5\", \"None\", ...)). "
            f"Got: {line}"
        )
        for token in ("var None", "var a4a5", "var i4i5", "var e4e5"):
            assert token in line, (
                f"TiedPieces combo is missing '{token}': {line}"
            )

    def test_combo_rejects_unknown_value(self) -> None:
        # Combo silently ignores values that are not in the var list, so the
        # previous lock stays. A string option would accept the garbage value
        # and set_tied_pieces() would fall through to None.
        self._set_tied("a4a5")
        self.engine.send("position startpos moves a3a4 a6a5")
        locked, _ = go_perft(self.engine)
        assert "a4a5" not in locked, "precondition: a4a5 should already be frozen"

        self._set_tied("not-a-combo-value")
        self.engine.send("position startpos moves a3a4 a6a5")
        still_locked, _ = go_perft(self.engine)
        assert "a4a5" not in still_locked, (
            "TiedPieces accepted an unknown value and cleared the lock; "
            "the option was likely changed from combo to string"
        )

    def test_empty_tied_squares_stay_walkable(self) -> None:
        # First pawn step lands on an empty locked square and must remain legal
        # so GUIs can still send `position startpos moves a3a4`.
        for option, _setup, walk_on, _frozen in PAWN_CASES:
            self._set_tied(option)
            self.engine.send("position startpos")
            moves, _ = go_perft(self.engine)
            assert walk_on in moves, (
                f"TiedPieces {option} forbade walking onto empty {walk_on[2:]} "
                f"({walk_on} missing at startpos)"
            )

    def test_cannot_move_piece_on_tied_square(self) -> None:
        for option, setup, _walk_on, frozen in PAWN_CASES:
            self._set_tied("None")
            self.engine.send(f"position startpos moves {setup}")
            unlocked, _ = go_perft(self.engine)
            assert frozen in unlocked, (
                f"control failed: {frozen} should be legal with TiedPieces None "
                f"after `{setup}`"
            )

            self._set_tied(option)
            self.engine.send(f"position startpos moves {setup}")
            locked, _ = go_perft(self.engine)
            assert frozen not in locked, (
                f"TiedPieces {option} did not freeze {frozen}; generate() likely "
                f"lost the tied-piece filter in an upstream merge of movegen.cpp"
            )

    def test_cannot_capture_piece_on_tied_square(self) -> None:
        self._set_tied("None")
        self.engine.send(f"position fen {CAPTURE_FEN}")
        unlocked, _ = go_perft(self.engine)
        assert CAPTURE_MOVE in unlocked, (
            f"control failed: {CAPTURE_MOVE} should capture the a4 pawn when "
            f"TiedPieces is None"
        )

        self._set_tied("a4a5")
        self.engine.send(f"position fen {CAPTURE_FEN}")
        locked, _ = go_perft(self.engine)
        assert CAPTURE_MOVE not in locked, (
            f"TiedPieces a4a5 still allows capturing the locked pawn ({CAPTURE_MOVE})"
        )

    def test_setup_moves_onto_tied_squares_still_apply(self) -> None:
        # applyingPosition must let the GUI replay the pawn pushes that occupy
        # the locked squares. If that guard is lost, `position ... moves a3a4
        # a6a5` would still work (empty destination), but freeze would then
        # fail the previous test. This check only verifies the position command
        # does not kill the engine.
        self._set_tied("a4a5")
        self.engine.send("position startpos moves a3a4 a6a5")
        self.engine.send("isready")
        self.engine.read_until(lambda ln: ln == "readyok")


def run(path: str) -> int:
    engine = Engine(path)
    tests = TestTiedPieces(engine)
    methods = [
        tests.test_option_advertised,
        tests.test_tied_pieces_is_uci_combo,
        tests.test_combo_rejects_unknown_value,
        tests.test_empty_tied_squares_stay_walkable,
        tests.test_cannot_move_piece_on_tied_square,
        tests.test_cannot_capture_piece_on_tied_square,
        tests.test_setup_moves_onto_tied_squares_still_apply,
    ]

    passed = failed = 0
    start = time.time()
    print(f"\nTest Suite: TiedPieces")
    try:
        tests.setup()
        for method in methods:
            name = method.__name__
            print(f"    Running {name}... \r", end="", flush=True)
            t0 = time.time()
            try:
                method()
            except Exception as exc:
                failed += 1
                ms = (time.time() - t0) * 1000
                print(f"    {RED}✗{RESET} {name} ({ms:.2f}ms)")
                print(f"      {CYAN}{type(exc).__name__}: {exc}{RESET}")
                break
            else:
                passed += 1
                ms = (time.time() - t0) * 1000
                print(f"    {GREEN}✓{RESET} {name} ({ms:.2f}ms)")
    finally:
        engine.close()

    duration = time.time() - start
    print(f"\n{BOLD}Test Summary{RESET}\n")
    print(
        f"    Tests: {GREEN}{passed} passed{RESET}, "
        f"{RED}{failed} failed{RESET}, {passed + failed} total"
    )
    print(f"    Time:  {duration:.2f}s\n")
    return 1 if failed else 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify TiedPieces logic is still present in the engine"
    )
    parser.add_argument("engine_path", help="Path to the Pikafish binary")
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    try:
        sys.exit(run(args.engine_path))
    except EngineError as exc:
        print(f"{RED}Engine error: {exc}{RESET}", file=sys.stderr)
        sys.exit(1)
