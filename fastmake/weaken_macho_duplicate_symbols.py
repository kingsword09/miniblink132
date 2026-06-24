#!/usr/bin/env python3
"""Weaken selected Mach-O duplicate symbols reported by ld64.

The macOS linker does not accept duplicate strong C++ definitions in the same
way as the Windows build this project historically targets.  This helper keeps
the workaround narrow: it parses the current ld64 duplicate-symbol log, chooses
the generated/stub/platform-mismatch side of each conflict where that is known,
then weakens only those symbols in the affected object or archive member.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


DUPLICATE_RE = re.compile(r"^duplicate symbol '(.+)' in:$")
ARCHIVE_SPEC_RE = re.compile(r"^(?P<archive>.+\.a)\[\d+\]\((?P<member>[^)]+)\)$")

LLVM_AR = Path("/opt/homebrew/opt/llvm/bin/llvm-ar")
LLVM_NM = Path("/opt/homebrew/opt/llvm/bin/llvm-nm")
LLVM_OBJCOPY = Path("/opt/homebrew/opt/llvm/bin/llvm-objcopy")
CXXFILT = Path("/usr/bin/c++filt")


@dataclass(frozen=True)
class ObjectSpec:
    raw: str
    path: Path
    member: str | None = None

    @property
    def basename(self) -> str:
        return self.member or self.path.name

    @property
    def key(self) -> tuple[str, str | None]:
        return (str(self.path), self.member)

    @property
    def label(self) -> str:
        if self.member:
            return f"{self.path}[{self.member}]"
        return str(self.path)


@dataclass
class DuplicateBlock:
    symbol: str
    objects: list[ObjectSpec]


def run(cmd: list[str], *, cwd: Path | None = None, capture: bool = False) -> str:
    kwargs = {
        "cwd": str(cwd) if cwd else None,
        "text": True,
        "check": True,
    }
    if capture:
        kwargs.update({"stdout": subprocess.PIPE, "stderr": subprocess.PIPE})
    result = subprocess.run(cmd, **kwargs)
    return result.stdout if capture else ""


def parse_object_spec(text: str) -> ObjectSpec:
    match = ARCHIVE_SPEC_RE.match(text)
    if match:
        return ObjectSpec(text, Path(match.group("archive")), match.group("member"))
    return ObjectSpec(text, Path(text), None)


def parse_duplicates(log_path: Path) -> list[DuplicateBlock]:
    lines = log_path.read_text(errors="ignore").splitlines()
    blocks: list[DuplicateBlock] = []
    i = 0
    while i < len(lines):
        match = DUPLICATE_RE.match(lines[i])
        if not match:
            i += 1
            continue
        symbol = match.group(1)
        objects: list[ObjectSpec] = []
        i += 1
        while i < len(lines) and lines[i].startswith("    "):
            objects.append(parse_object_spec(lines[i].strip()))
            i += 1
        blocks.append(DuplicateBlock(symbol, objects))
    return blocks


def choose_weaken_side(block: DuplicateBlock) -> list[ObjectSpec]:
    """Pick the side that should lose strong-symbol precedence.

    These patterns are intentionally concrete.  They encode the currently known
    conflict classes: compat stubs, platform fallback implementations, and
    helper definitions that were copied into larger objects during the port.
    Unknown pairs fall back to the first object reported by ld64.
    """

    weak_basename_patterns = (
        "mojo_empty",
        "partition_alloc_support",
        "video_frame_69fb92b7",
        "canvas_resource_provider",
        "canvas_resource_",
        "clipboard_promise",
        "html_canvas_element",
        "html_media_element",
        "message_port_",
        "v8_union_",
        "surface_saved_frame",
        "layer_tree_host_impl",
        "font_fallback_linux",
        "font_render_params_skia",
    )
    selected: list[ObjectSpec] = []
    for pattern in weak_basename_patterns:
        for obj in block.objects:
            if pattern in obj.basename:
                selected.append(obj)
        if selected:
            return selected
    return block.objects[:1]


def normalize_demangled(symbol: str) -> str:
    return re.sub(r"\[abi:[^\]]+\]", "", symbol).strip()


def demangle(raw_symbols: list[str]) -> list[str]:
    if not raw_symbols:
        return []
    proc = subprocess.run(
        [str(CXXFILT)],
        input="\n".join(raw_symbols) + "\n",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=True,
    )
    return proc.stdout.splitlines()


def nm_defined_symbols(path: Path) -> list[tuple[str, str]]:
    output = run(
        [str(LLVM_NM), "-gU", "--defined-only", str(path)],
        capture=True,
    )
    raw_symbols: list[str] = []
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 3 and len(parts[1]) == 1:
            raw_symbols.append(parts[2])
    demangled = demangle(raw_symbols)
    return list(zip(raw_symbols, demangled))


def raw_symbols_for(path: Path, wanted_demangled: set[str]) -> set[str]:
    wanted_norm = {normalize_demangled(s) for s in wanted_demangled}
    raw: set[str] = set()
    for raw_symbol, demangled in nm_defined_symbols(path):
        if demangled in wanted_demangled or normalize_demangled(demangled) in wanted_norm:
            raw.add(raw_symbol)
    return raw


def backup_once(path: Path, backup_root: Path, repo_root: Path) -> Path:
    try:
        rel = path.resolve().relative_to(repo_root.resolve())
    except ValueError:
        rel = Path(path.name)
    backup = backup_root / rel
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        shutil.copy2(path, backup)
    return backup


def weaken_object(path: Path, symbols: set[str], dry_run: bool) -> None:
    if dry_run or not symbols:
        return
    tmp = path.with_suffix(path.suffix + ".weaken-tmp")
    cmd = [str(LLVM_OBJCOPY)]
    for symbol in sorted(symbols):
        cmd += ["--weaken-symbol", symbol]
    cmd += [str(path), str(tmp)]
    run(cmd)
    os.replace(tmp, path)


def weaken_archive_member(
    archive: Path,
    member: str,
    symbols: set[str],
    dry_run: bool,
) -> None:
    if dry_run or not symbols:
        return
    with tempfile.TemporaryDirectory(prefix="macho-weak-") as tmp_name:
        tmp_dir = Path(tmp_name)
        run([str(LLVM_AR), "x", str(archive), member], cwd=tmp_dir)
        member_path = tmp_dir / member
        weakened = tmp_dir / f"{member}.weakened"
        cmd = [str(LLVM_OBJCOPY)]
        for symbol in sorted(symbols):
            cmd += ["--weaken-symbol", symbol]
        cmd += [str(member_path), str(weakened)]
        run(cmd)
        os.replace(weakened, member_path)
        run([str(LLVM_AR), "rcs", str(archive), member], cwd=tmp_dir)


def check_tools() -> None:
    missing = [p for p in (LLVM_AR, LLVM_NM, LLVM_OBJCOPY, CXXFILT) if not p.exists()]
    if missing:
        raise SystemExit("missing tools: " + ", ".join(str(p) for p in missing))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, default=Path("out/build_logs/blink_mac_release_arm64.log"))
    parser.add_argument("--backup-root", type=Path, default=Path("out/weak_backups"))
    parser.add_argument("--report", type=Path, default=Path("out/build_logs/mac_duplicate_weaken_report.json"))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)

    check_tools()
    repo_root = Path.cwd()
    blocks = parse_duplicates(args.log)

    desired: dict[tuple[str, str | None], set[str]] = {}
    decisions: list[dict[str, object]] = []
    for block in blocks:
        selected = choose_weaken_side(block)
        for obj in selected:
            desired.setdefault(obj.key, set()).add(block.symbol)
        decisions.append(
            {
                "symbol": block.symbol,
                "objects": [obj.label for obj in block.objects],
                "weaken": [obj.label for obj in selected],
            }
        )

    resolved: dict[tuple[str, str | None], set[str]] = {}
    missing: list[dict[str, object]] = []
    for key, wanted in sorted(desired.items()):
        path = Path(key[0])
        member = key[1]
        if member:
            with tempfile.TemporaryDirectory(prefix="macho-nm-") as tmp_name:
                tmp_dir = Path(tmp_name)
                run([str(LLVM_AR), "x", str(path), member], cwd=tmp_dir)
                member_path = tmp_dir / member
                raw = raw_symbols_for(member_path, wanted)
        else:
            raw = raw_symbols_for(path, wanted)
        resolved[key] = raw
        if not raw:
            missing.append({"object": f"{path}[{member}]" if member else str(path), "wanted": sorted(wanted)})

    if missing:
        print(json.dumps({"missing": missing}, indent=2, ensure_ascii=False), file=sys.stderr)
        return 2

    args.backup_root.mkdir(parents=True, exist_ok=True)
    for key, raw in sorted(resolved.items()):
        path = Path(key[0])
        member = key[1]
        backup_once(path, args.backup_root, repo_root)
        if member:
            weaken_archive_member(path, member, raw, args.dry_run)
        else:
            weaken_object(path, raw, args.dry_run)

    report = {
        "log": str(args.log),
        "duplicate_blocks": len(blocks),
        "targets": [
            {
                "object": f"{Path(key[0])}[{key[1]}]" if key[1] else key[0],
                "raw_symbols": sorted(raw),
            }
            for key, raw in sorted(resolved.items())
        ],
        "decisions": decisions,
        "dry_run": args.dry_run,
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n")

    action = "would weaken" if args.dry_run else "weakened"
    raw_count = sum(len(v) for v in resolved.values())
    print(f"{action} {raw_count} raw symbols in {len(resolved)} object/member targets")
    print(f"report: {args.report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
