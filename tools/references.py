#!/usr/bin/env python3
"""Acquire and inspect pinned external plugin references without modifying local sources."""
from __future__ import annotations

import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / ".reference-data"
REPOS = DATA / "repos"
MANAGED = DATA / "managed-repos"
EXTRACTS = DATA / "extracts"
ROOT_FILE = ROOT / ".references"
MANIFEST = ROOT / "tools" / "reference-manifest.json"
MAX_TOTAL = 64 * 1024 * 1024
MAX_FILE = 4 * 1024 * 1024
MAX_RATIO = 200


def load_manifest():
    with MANIFEST.open(encoding="utf-8") as stream:
        return json.load(stream)


def run(args, *, cwd=None, check=True, capture=False):
    return subprocess.run(args, cwd=cwd, check=check, text=True,
                          stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.PIPE if capture else None)


def root_entries():
    if not ROOT_FILE.exists():
        return []
    return [Path(line.strip()).expanduser() for line in ROOT_FILE.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.lstrip().startswith("#")]


def looks_git(path):
    if not path.is_dir():
        return False
    result = run(["git", "-C", str(path), "rev-parse", "--show-toplevel"],
                 check=False, capture=True)
    return result.returncode == 0 and Path(result.stdout.strip()).resolve() == path.resolve()


def existing_reference(name, entries):
    """Reuse an existing listed source root or a named child; never mutate it."""
    candidates = []
    for entry in entries:
        # A listed repository root wins over its similarly named source subdirectory.
        candidates.extend((entry, entry / name) if entry.name.casefold() == name.casefold() else (entry / name,))
    for path in candidates:
        try:
            path = path.resolve(strict=True)
        except (OSError, RuntimeError):
            continue
        if path.is_dir() and path.name.casefold() == name.casefold():
            return path
    return None


def git_state(path):
    if not looks_git(path):
        return None, None, None
    rev = run(["git", "-C", str(path), "rev-parse", "HEAD"], check=False, capture=True)
    status = run(["git", "-C", str(path), "status", "--porcelain"], check=False, capture=True)
    branch = run(["git", "-C", str(path), "symbolic-ref", "-q", "HEAD"], check=False, capture=True)
    return (rev.stdout.strip() if rev.returncode == 0 else None,
            bool(status.stdout.strip()) if status.returncode == 0 else None,
            None if branch.returncode else branch.stdout.strip())


def safe_link(link, target):
    target = target.resolve(strict=True)
    link.parent.mkdir(parents=True, exist_ok=True)
    if link.is_symlink():
        if link.resolve() == target:
            return
        link.unlink()
    elif link.exists():
        raise RuntimeError(f"Refusing to replace non-symlink generated path: {link}")
    link.symlink_to(target, target_is_directory=True)


def acquire_repo(entry, local_root):
    name, revision = entry["name"], entry.get("revision")
    local = existing_reference(name, local_root)
    if local:
        return local
    if not entry.get("url") or not revision:
        return None
    target = MANAGED / name
    if target.exists() or target.is_symlink():
        if target.is_symlink():
            candidate = target.resolve()
            if looks_git(candidate):
                return candidate
        elif looks_git(target):
            return target.resolve()
        else:
            raise RuntimeError(f"Refusing to overwrite existing non-repository path: {target}")
    MANAGED.mkdir(parents=True, exist_ok=True)
    tmp = Path(tempfile.mkdtemp(prefix=f".{name}-", dir=MANAGED))
    try:
        run(["git", "clone", "--no-checkout", "--filter=blob:none", entry["url"], str(tmp)])
        run(["git", "-C", str(tmp), "checkout", "--detach", revision])
        actual, dirty, _ = git_state(tmp)
        if actual != revision or dirty:
            raise RuntimeError(f"Clone did not produce clean pinned revision {revision}: {name}")
        final = MANAGED / name
        if final.exists() or final.is_symlink():
            raise RuntimeError(f"Reference path appeared during clone: {final}")
        tmp.rename(final)
        return final.resolve()
    except Exception:
        shutil.rmtree(tmp, ignore_errors=True)
        raise


def menu_root_override():
    value = os.environ.get("NEURO_FNV_MENU_ROOT")
    if value:
        path = Path(value).expanduser().resolve(strict=True)
        if not path.is_dir() or not (path / "menus").is_dir():
            raise RuntimeError(f"NEURO_FNV_MENU_ROOT must be a directory containing menus/: {path}")
        return path
    for entry in root_entries():
        for candidate in (entry, entry / "extracts"):
            try:
                candidate = candidate.resolve(strict=True)
            except (OSError, RuntimeError):
                continue
            if candidate.is_dir() and (candidate / "menus").is_dir():
                return candidate
    return None


def extract_archive(archive):
    archive = Path(archive).expanduser().resolve(strict=True)
    if not archive.is_file():
        raise RuntimeError(f"Menu archive is not a file: {archive}")
    DATA.mkdir(parents=True, exist_ok=True)
    # Never stage beneath EXTRACTS: it may be a symlink to user-owned data.
    staging = Path(tempfile.mkdtemp(prefix=".menu-stage-", dir=DATA))
    try:
        total = 0
        count = 0
        with zipfile.ZipFile(archive) as zf:
            for info in zf.infolist():
                raw = info.filename
                if "\\" in raw or raw.startswith("/"):
                    raise RuntimeError(f"Unsafe ZIP member path: {raw!r}")
                parts = PurePosixPath(raw).parts
                if not parts or any(part in ("", ".", "..") for part in parts):
                    raise RuntimeError(f"Unsafe ZIP member path: {raw!r}")
                mode = info.external_attr >> 16
                if stat.S_ISLNK(mode):
                    raise RuntimeError(f"ZIP symlinks are not allowed: {raw}")
                if info.is_dir():
                    if parts[0] != "menus":
                        raise RuntimeError(f"Only menus/**/*.xml files are allowed in the archive: {raw}")
                    continue
                file_type = stat.S_IFMT(mode)
                if file_type not in (0, stat.S_IFREG):
                    raise RuntimeError(f"ZIP special files are not allowed: {raw}")
                if parts[0] != "menus" or len(parts) < 2 or not raw.lower().endswith(".xml"):
                    raise RuntimeError(f"Only menus/**/*.xml files are allowed in the archive: {raw}")
                if info.file_size > MAX_FILE:
                    raise RuntimeError(f"Menu XML exceeds {MAX_FILE} bytes: {raw}")
                total += info.file_size
                count += 1
                if total > MAX_TOTAL:
                    raise RuntimeError(f"Menu archive exceeds {MAX_TOTAL} uncompressed bytes")
                if info.file_size and (not info.compress_size or info.file_size / info.compress_size > MAX_RATIO):
                    raise RuntimeError(f"Suspicious compression ratio in ZIP member: {raw}")
                destination = staging.joinpath(*parts)
                destination.parent.mkdir(parents=True, exist_ok=True)
                with zf.open(info) as source, destination.open("xb") as output:
                    shutil.copyfileobj(source, output, length=64 * 1024)
                # XML is retained as reference evidence; the game accepts constructs
                # beyond the generic XML parser's compatibility surface.
        if not count:
            raise RuntimeError("Menu archive contains no menus/**/*.xml files")
        final = DATA / "private-menus"
        if final.exists() or final.is_symlink():
            if final.is_symlink() or not final.is_dir():
                raise RuntimeError(f"Refusing to replace existing menu data: {final}")
            staged_files = sorted(path.relative_to(staging) for path in staging.rglob("*") if path.is_file())
            final_files = sorted(path.relative_to(final) for path in final.rglob("*") if path.is_file())
            if staged_files == final_files and all((staging / path).read_bytes() == (final / path).read_bytes()
                                                   for path in staged_files):
                shutil.rmtree(staging)
                return final
            raise RuntimeError(f"Private menu data already exists and differs from the configured archive: {final}")
        staging.rename(final)
        return final
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise


def setup():
    manifest = load_manifest()
    entries = root_entries()
    selections = {}
    errors = []
    explicit = os.environ.get("NEURO_FNV_REFERENCE_ROOT")
    explicit_root = Path(explicit).expanduser().resolve(strict=True) if explicit else None
    if explicit_root and not explicit_root.is_dir():
        raise RuntimeError(f"NEURO_FNV_REFERENCE_ROOT is not a directory: {explicit_root}")
    for entry in manifest["references"]:
        try:
            candidate = explicit_root / entry["name"] if explicit_root else None
            if candidate and candidate.exists():
                path = candidate.resolve(strict=True)
                if not path.is_dir():
                    raise RuntimeError(f"Explicit reference override is not a directory: {path}")
            else:
                path = acquire_repo(entry, entries)
            if path is None:
                errors.append(f"{entry['name']}: no public upstream configured; supply it in an existing .references root")
                continue
            selections[entry["name"]] = path
        except Exception as exc:
            errors.append(f"{entry['name']}: {exc}")
    for name, path in selections.items():
        canonical = REPOS / name
        if canonical.exists() and canonical.resolve() == path.resolve():
            continue
        safe_link(canonical, path)
    try:
        if os.environ.get("NEURO_FNV_MENU_ROOT"):
            safe_link(EXTRACTS, menu_root_override())
        elif os.environ.get("NEURO_FNV_MENU_ARCHIVE"):
            extracted = extract_archive(os.environ["NEURO_FNV_MENU_ARCHIVE"])
            safe_link(EXTRACTS, extracted)
        else:
            override = menu_root_override()
            if override:
                safe_link(EXTRACTS, override)
    except Exception as exc:
        errors.append(f"menus: {exc}")
    # Keep the legacy consumer file machine-local and absolute, covering each selected ref
    # plus the optional external menu source without embedding any machine-specific value.
    roots = sorted({str(path.resolve()) for path in selections.values()})
    menu_path = EXTRACTS.resolve() if EXTRACTS.is_symlink() else None
    if menu_path:
        roots.append(str(menu_path))
    ROOT_FILE.write_text("".join(f"{path}\n" for path in dict.fromkeys(roots)), encoding="utf-8")
    if errors:
        raise RuntimeError("Reference setup incomplete:\n- " + "\n- ".join(errors))
    check()


def check():
    manifest = load_manifest()
    failures = []
    print("Reference status:")
    for entry in manifest["references"]:
        name, expected = entry["name"], entry.get("revision")
        path = REPOS / name
        if not path.exists():
            print(f"- {name}: MISSING")
            failures.append(name)
            continue
        managed_path = MANAGED / name
        managed = managed_path.exists() and path.resolve() == managed_path.resolve()
        actual, dirty, branch = git_state(path.resolve())
        if actual is None:
            state = f"unversioned/non-git source (expected {expected or 'no pinned revision'})"
        else:
            state = "pinned" if expected and actual == expected else f"unpinned/unexpected revision (expected {expected or 'none'})"
        if dirty:
            state += ", dirty"
        if branch is not None:
            state += ", attached HEAD"
        print(f"- {name}: {actual or 'unknown'} ({state}) [{path.resolve()}]")
        # External roots are user-owned: report drift but never rewrite or reject them.
        # Managed acquisitions must remain clean, pinned, and detached.
        if managed and (expected and actual != expected or dirty or branch is not None):
            failures.append(name)
    if EXTRACTS.exists():
        menu_dir = EXTRACTS.resolve() / "menus"
        if menu_dir.is_dir():
            xml = sorted(menu_dir.rglob("*.xml"))
            print(f"- private menu XML: present ({len(xml)} XML files; {EXTRACTS.resolve()})")
        else:
            print("- private menu XML: absent (no menus/ directory)")
    else:
        print("- private menu XML: absent (optional; no archive/root configured)")
    if failures:
        raise RuntimeError("Reference check found missing or incorrectly pinned repositories: " + ", ".join(failures))


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in ("setup", "check"):
        print("Usage: python3 tools/references.py {setup|check}", file=sys.stderr)
        return 2
    try:
        setup() if sys.argv[1] == "setup" else check()
        return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
