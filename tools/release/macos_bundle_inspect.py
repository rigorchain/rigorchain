#!/usr/bin/env python3
"""Fail-closed Mach-O inspection for the macOS ARM64 wallet bundle.

Live mode reads lipo and otool from the bundle on disk. Tests call
inspect_records() with fixture text and never look at the host's Homebrew
installation. A missing tool, a non-zero tool status, a non-arm64 slice, an
absolute build-machine dependency, or a relocatable path that does not name a
file inside the bundle is a hard failure.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

REQUIRED_PLUGINS = (
    "libqminimal.dylib",
    "libqcocoa.dylib",
    "libqmacstyle.dylib",
)

SYSTEM_PREFIXES = ("/System/Library/", "/usr/lib/")


@dataclass
class MachOView:
    relpath: str
    is_macho: bool = True
    lipo_status: int = 0
    lipo_stdout: str = ""
    otool_status: int = 0
    otool_stdout: str = ""
    load_status: int = 0
    load_stdout: str = ""


def _norm(path: str) -> str:
    parts = []
    for part in path.replace("\\", "/").split("/"):
        if part in ("", "."):
            continue
        if part == "..":
            if not parts:
                return ""
            parts.pop()
            continue
        parts.append(part)
    return "/".join(parts)


def parse_otool_deps(stdout: str) -> list[str]:
    deps = []
    marker = " (compatibility version "
    for raw in stdout.splitlines():
        line = raw.strip()
        if not line or line.endswith(":"):
            continue
        if marker not in line:
            continue
        deps.append(line[: line.index(marker)])
    return deps


def parse_rpaths(load_stdout: str) -> list[str]:
    rpaths = []
    pending = False
    for raw in load_stdout.splitlines():
        line = raw.strip()
        if "LC_RPATH" in line:
            pending = True
            continue
        if pending and line.startswith("path "):
            body = line[len("path ") :]
            offset = body.find(" (offset ")
            if offset != -1:
                body = body[:offset]
            rpaths.append(body.strip())
            pending = False
    return rpaths


def _is_system(path: str) -> bool:
    if path.startswith("/usr/local/"):
        return False
    return path.startswith(SYSTEM_PREFIXES)


def _join_bundle(base_rel: str, suffix: str) -> str:
    base_dir = _norm(os.path.dirname(base_rel))
    if not suffix:
        return ""
    return _norm(f"{base_dir}/{suffix}" if base_dir else suffix)


def resolve_special(token: str, owner_rel: str, exe_rel: str, rpaths: list[str], files: set[str]) -> tuple[bool, str]:
    if token.startswith("@loader_path/"):
        target = _join_bundle(owner_rel, token[len("@loader_path/") :])
        return (target in files and target != "", target)
    if token.startswith("@executable_path/"):
        target = _join_bundle(exe_rel, token[len("@executable_path/") :])
        return (target in files and target != "", target)
    if token.startswith("@rpath/"):
        rest = token[len("@rpath/") :]
        tried = []
        for rp in rpaths:
            if rp.startswith("@loader_path/"):
                root = _join_bundle(owner_rel, rp[len("@loader_path/") :])
            elif rp.startswith("@executable_path/"):
                root = _join_bundle(exe_rel, rp[len("@executable_path/") :])
            elif rp.startswith("/"):
                tried.append(rp)
                continue
            else:
                root = _norm(rp)
            if root == "":
                tried.append(rp)
                continue
            target = _norm(f"{root}/{rest}")
            tried.append(target)
            if target in files:
                return (True, target)
        return (False, ",".join(tried) if tried else "@rpath-empty")
    return (False, token)


def inspect_records(views: list[MachOView], bundle_files: set[str], executable_relpath: str) -> list[str]:
    errors = []
    files = {_norm(p) for p in bundle_files}
    inspected = []
    seen_plugins = set()
    exe = _norm(executable_relpath)

    for view in views:
        rel = _norm(view.relpath)
        base = os.path.basename(rel)
        if base in REQUIRED_PLUGINS and not view.is_macho:
            errors.append(f"required Qt plugin {base} was not inspected as Mach-O ({rel})")
            continue
        if not view.is_macho:
            continue
        inspected.append(rel)
        if base in REQUIRED_PLUGINS:
            seen_plugins.add(base)
        if view.lipo_status != 0:
            errors.append(f"lipo inspection failed for {rel} (status {view.lipo_status})")
            continue
        arch = "".join(view.lipo_stdout.split())
        if arch != "arm64":
            errors.append(f"{rel} architecture [{view.lipo_stdout.strip()}] is not thin arm64")
            continue
        if view.otool_status != 0:
            errors.append(f"otool -L failed for {rel} (status {view.otool_status})")
            continue
        if view.load_status != 0:
            errors.append(f"otool -l failed for {rel} (status {view.load_status})")
            continue
        deps = parse_otool_deps(view.otool_stdout)
        if not deps:
            errors.append(f"otool -L produced no dependencies for {rel}")
            continue
        rpaths = parse_rpaths(view.load_stdout)
        for dep in deps:
            if _is_system(dep):
                continue
            if dep.startswith("/opt/homebrew/") or "/opt/homebrew/" in dep:
                errors.append(f"{rel} links to Homebrew path {dep}")
                continue
            if dep.startswith("/usr/local/"):
                errors.append(f"{rel} links to build-machine /usr/local path {dep}")
                continue
            if dep.startswith(("@rpath/", "@loader_path/", "@executable_path/")):
                ok, detail = resolve_special(dep, rel, exe, rpaths, files)
                if not ok:
                    kind = "rpath" if dep.startswith("@rpath/") else "relocatable"
                    errors.append(f"{rel} broken {kind} dependency {dep} -> {detail}")
                continue
            if dep.startswith("/"):
                errors.append(f"{rel} unresolved non-system dependency {dep}")
                continue
            errors.append(f"{rel} unrecognized dependency {dep}")

    for name in REQUIRED_PLUGINS:
        if name not in seen_plugins:
            errors.append(f"missing required Qt plugin {name}")
    if exe not in inspected:
        errors.append(f"main executable was not inspected: {exe}")
    if not inspected:
        errors.append("no Mach-O files were inspected")
    return errors


def _run(cmd: list[str]) -> tuple[int, str]:
    try:
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    except FileNotFoundError:
        return 127, ""
    return proc.returncode, proc.stdout


def _find_executable(bundle: Path) -> str:
    macos = bundle / "Contents" / "MacOS"
    preferred = macos / "rigor-qt"
    if preferred.is_file():
        return "Contents/MacOS/rigor-qt"
    if macos.is_dir():
        for entry in sorted(macos.iterdir()):
            if entry.is_file():
                return _norm(str(entry.relative_to(bundle)))
    return "Contents/MacOS/rigor-qt"


def inspect_bundle(bundle: Path) -> list[str]:
    if not bundle.is_dir():
        return [f"bundle directory is missing: {bundle}"]
    file_status, _file_out = _run(["file", "-b", str(bundle)])
    if file_status == 127:
        return ["file(1) is missing; refusing to guess which files are Mach-O"]
    views = []
    files = set()
    for dirpath, dirnames, filenames in os.walk(bundle, followlinks=False):
        dirnames[:] = [name for name in dirnames if not (Path(dirpath) / name).is_symlink()]
        for name in filenames:
            path = Path(dirpath) / name
            rel = _norm(str(path.relative_to(bundle)))
            files.add(rel)
            status, desc = _run(["file", "-b", str(path)])
            if status == 127:
                return ["file(1) is missing; refusing to guess which files are Mach-O"]
            if status != 0:
                return [f"file(1) failed for {rel} (status {status})"]
            is_macho = "Mach-O" in desc
            if os.path.basename(rel) in REQUIRED_PLUGINS or is_macho:
                lipo_status, lipo_out = _run(["lipo", "-archs", str(path)])
                otool_status, otool_out = _run(["otool", "-L", str(path)])
                load_status, load_out = _run(["otool", "-l", str(path)])
                views.append(MachOView(
                    relpath=rel,
                    is_macho=is_macho,
                    lipo_status=lipo_status,
                    lipo_stdout=lipo_out,
                    otool_status=otool_status,
                    otool_stdout=otool_out,
                    load_status=load_status,
                    load_stdout=load_out,
                ))
    return inspect_records(views, files, _find_executable(bundle))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Inspect a Rigor-Qt.app bundle for ARM64 relocatable Qt dependencies.")
    parser.add_argument("--bundle", required=True)
    args = parser.parse_args(argv)
    errors = inspect_bundle(Path(args.bundle))
    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        print("BUNDLE_INSPECT=FAIL", file=sys.stderr)
        return 1
    print("BUNDLE_INSPECT=PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
