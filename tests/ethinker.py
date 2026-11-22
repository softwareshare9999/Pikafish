#!/usr/bin/env python3
"""Regression tests for EThinker / old-UCCI position commands.

Official UCI requires `position startpos` / `position fen ...`.
EThinker (auto) omits the `position` prefix and sends a bare
`startpos` or `fen ...` line. This fork keeps that handler in
src/xquci.cpp (hooked from uci.cpp); upstream Pikafish does not.

This file is separate from tests/instrumented.py so an upstream merge
cannot drop the checks. It runs from the fork-only
.github/workflows/ethinker.yml, leaving upstream's pikafish.yml untouched.
Assertions compare the engine's own `d` FEN after a bare command vs the
same setup via standard `position ...`, so unrelated search/eval changes
do not fail this suite.

Run:
  python tests/ethinker.py path/to/pikafish
"""

from __future__ import annotations

import argparse
import os
import queue
import subprocess
import sys
import threading
import time
from typing import Iterable, List, Optional

CYAN = "\033[36m"
RED = "\033[31m"
GREEN = "\033[32m"
RESET = "\033[0m"
BOLD = "\033[1m"

TIMEOUT = 60.0

# Legal from Xiangqi startpos; used only to leave the initial position.
SAMPLE_MOVES = "a3a4 i6i5"


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
        threading.Thread(target=self._read, daemon=True).start()

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

    def lines(self, timeout: float = TIMEOUT) -> Iterable[str]:
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

    def read_until(self, predicate, timeout: float = TIMEOUT) -> List[str]:
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


def _unknown(cmd: str, line: str) -> bool:
    return line.startswith("Unknown command:") and cmd in line


class TestEThinker:
    def __init__(self, engine: Engine) -> None:
        self.engine = engine

    def setup(self) -> None:
        self.engine.send("uci")
        self.engine.read_until(lambda ln: ln == "uciok")
        self.engine.send("isready")
        self.engine.read_until(lambda ln: ln == "readyok")

    def send_ok(self, cmd: str) -> List[str]:
        """Send a command that must not be reported as unknown."""
        self.engine.send(cmd)
        self.engine.send("isready")
        lines = self.engine.read_until(lambda ln: ln == "readyok")
        for line in lines:
            if _unknown(cmd.split()[0], line):
                raise AssertionError(
                    f"command {cmd!r} was rejected as unknown; EThinker "
                    f"bare fen/startpos support was dropped from uci.cpp"
                )
        return lines

    def fen(self) -> str:
        self.engine.send("d")
        fen = None
        lines = self.engine.read_until(lambda ln: ln.startswith("Checkers:"))
        for line in lines:
            if line.startswith("Unknown command:"):
                raise AssertionError(
                    f"debug dump failed: {line}"
                )
            if line.startswith("Fen: "):
                fen = line[5:].strip()
        if not fen:
            raise AssertionError("d did not print a Fen: line")
        return fen

    def test_standard_position_still_works(self) -> None:
        self.send_ok("position startpos")
        start = self.fen()
        self.send_ok(f"position startpos moves {SAMPLE_MOVES}")
        moved = self.fen()
        assert moved != start, (
            f"control failed: `{SAMPLE_MOVES}` did not change the position"
        )
        self.send_ok("position startpos")
        assert self.fen() == start

    def test_bare_startpos_not_unknown(self) -> None:
        # The whole feature: EThinker sends `startpos` with no `position`.
        self.send_ok("startpos")

    def test_bare_startpos_resets_board(self) -> None:
        self.send_ok("position startpos")
        start = self.fen()
        self.send_ok(f"position startpos moves {SAMPLE_MOVES}")
        assert self.fen() != start
        self.send_ok("startpos")
        assert self.fen() == start, (
            "bare `startpos` did not reset to the initial position; "
            "the EThinker handler in UCIEngine::loop() is missing or broken"
        )

    def test_bare_startpos_moves(self) -> None:
        self.send_ok(f"position startpos moves {SAMPLE_MOVES}")
        expected = self.fen()
        self.send_ok("position startpos")
        self.send_ok(f"startpos moves {SAMPLE_MOVES}")
        actual = self.fen()
        assert actual == expected, (
            "bare `startpos moves ...` did not match `position startpos moves ...`; "
            f"got {actual!r}, expected {expected!r}"
        )

    def test_bare_fen(self) -> None:
        self.send_ok(f"position startpos moves {SAMPLE_MOVES}")
        custom = self.fen()
        self.send_ok("position startpos")
        assert self.fen() != custom
        self.send_ok(f"fen {custom}")
        actual = self.fen()
        assert actual == custom, (
            "bare `fen ...` did not apply the FEN; "
            f"got {actual!r}, expected {custom!r}"
        )

    def test_bare_fen_with_moves(self) -> None:
        self.send_ok("position startpos")
        start_fen = self.fen()
        self.send_ok(f"position fen {start_fen} moves {SAMPLE_MOVES}")
        expected = self.fen()
        self.send_ok("position startpos")
        self.send_ok(f"fen {start_fen} moves {SAMPLE_MOVES}")
        actual = self.fen()
        assert actual == expected, (
            "bare `fen ... moves ...` did not match `position fen ... moves ...`; "
            f"got {actual!r}, expected {expected!r}"
        )


def run(path: str) -> int:
    engine = Engine(path)
    tests = TestEThinker(engine)
    methods = [
        tests.test_standard_position_still_works,
        tests.test_bare_startpos_not_unknown,
        tests.test_bare_startpos_resets_board,
        tests.test_bare_startpos_moves,
        tests.test_bare_fen,
        tests.test_bare_fen_with_moves,
    ]

    passed = failed = 0
    start = time.time()
    print("\nTest Suite: EThinkerUCCI")
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
        description="Verify EThinker bare fen/startpos UCCI support is still present"
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
