#!/usr/bin/env python3
"""Build and run the C++ test suite with timestamped, resumable diagnostics."""

import argparse
import json
import os
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path


TEST_LINE_RE = re.compile(
    r"^\s*\d+/\d+\s+Test\s+#\d+:\s+(\S+)\s+\.+\s+"
    r"(Passed|Failed|Timeout|Not Run|Skipped)\b",
    re.IGNORECASE | re.MULTILINE,
)
LIST_LINE_RE = re.compile(r"^\s*Test\s+#\d+:\s+(\S+)", re.MULTILINE)
FAILED_LIST_RE = re.compile(r"^\s*\d+\s+-\s+(\S+)\s+\(.*\)$", re.MULTILINE)
CTEST_START_RE = re.compile(r"^Start\s+\d+:\s+(\S+)\s*$", re.MULTILINE)


def timestamp() -> str:
    return datetime.now().astimezone().isoformat(timespec="milliseconds")


def log(message: str = "") -> None:
    print(f"[{timestamp()}] {message}", flush=True)


def run_command(command, cwd, env=None):
    """Run a command while preserving live output and returning (code, output)."""
    log(f"$ {' '.join(str(part) for part in command)}")
    try:
        process = subprocess.Popen(
            command,
            cwd=cwd,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
    except FileNotFoundError as error:
        log(f"ERROR: command not found: {error.filename}")
        return 127, ""

    output = []
    assert process.stdout is not None
    for line in process.stdout:
        line = line.rstrip("\r\n")
        output.append(line)
        log(line)
    return process.wait(), "\n".join(output)


def run_direct_test_diagnostics(build_dir: Path, config: str, names, env) -> None:
    """Run failed QtTest executables directly to expose loader/runtime errors."""
    for name in sorted(set(names)):
        executable = build_dir / config / f"{name}.exe"
        if not executable.exists():
            # Single-config generators place the executable directly in the
            # build directory. Keep the Windows suffix in this fallback so
            # failed tests can still be run for loader/runtime diagnostics.
            executable = build_dir / f"{name}.exe"
        if not executable.exists():
            log(f"Diagnostic executable not found: {executable}")
            continue
        log(f"$ {executable} -v2")
        run_command([str(executable), "-v2"], build_dir, env)


def cmake_build_command(build_dir: Path, config: str):
    """Build with a vcpkg setting that matches the existing CMake configure."""
    command = ["cmake", "--build", ".", "--config", config]
    cache_path = build_dir / "CMakeCache.txt"
    try:
        cache = cache_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return command

    generator = re.search(r"^CMAKE_GENERATOR:INTERNAL=(.*)$", cache, re.MULTILINE)
    if generator and generator.group(1).startswith("Visual Studio"):
        uses_vcpkg_toolchain = bool(re.search(
            r"^CMAKE_TOOLCHAIN_FILE:[^=]*=.*vcpkg[\\/]scripts[\\/]buildsystems[\\/]vcpkg\\.cmake$",
            cache,
            re.MULTILINE | re.IGNORECASE,
        ))
        manifest_mode = bool(re.search(
            r"^VCPKG_MANIFEST_MODE:[^=]*=ON$", cache, re.MULTILINE | re.IGNORECASE
        ))
        if uses_vcpkg_toolchain or manifest_mode:
            command.extend(["--", "/m:1", "/p:VcpkgEnableManifest=true"])
        else:
            command.extend(["--", "/m:1", "/p:VcpkgEnabled=false"])
    return command


def configure_build(project_root: Path, build_dir: Path, config: str) -> int:
    """Configure a missing build tree using the repository's CMake settings."""
    if build_dir.exists() and not build_dir.is_dir():
        log(f"ERROR: build path is not a directory: {build_dir}")
        return 1
    cache_path = build_dir / "CMakeCache.txt"
    if cache_path.is_file():
        try:
            cache = cache_path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            cache = ""
        generator = re.search(r"^CMAKE_GENERATOR:INTERNAL=(.+)$", cache, re.MULTILINE)
        has_project = re.search(r"^CMAKE_PROJECT_NAME:STATIC=.+$", cache, re.MULTILINE)
        has_compiler = re.search(r"^CMAKE_CXX_COMPILER:FILEPATH=.+$", cache, re.MULTILINE)
        missing_make_program = re.search(
            r"^CMAKE_MAKE_PROGRAM:FILEPATH=.*NOTFOUND$", cache, re.MULTILINE
        )
        # Visual Studio generators do not cache CMAKE_CXX_COMPILER, so use
        # their generator/project markers instead of needlessly reconfiguring
        # an otherwise valid multi-config build tree.
        cache_is_configured = bool(
            generator
            and has_project
            and not missing_make_program
            and (generator.group(1).startswith("Visual Studio") or has_compiler)
        )
        if cache_is_configured:
            return 0

    build_dir_name = os.path.relpath(build_dir, project_root).replace("\\", "/")
    direct_configure = [
        "cmake",
        "-S", str(project_root),
        "-B", str(build_dir),
        "-DCMAKE_BUILD_TYPE=" + config,
        "-DVCPKG_MANIFEST_MODE=ON",
        "-DVCPKG_OVERLAY_PORTS=" + str(project_root / "ports"),
    ]
    preset_configure = build_dir_name == "build" and config.lower() == "release"
    if preset_configure:
        command = ["cmake", "--preset", "release"]
    else:
        command = direct_configure
    if cache_path.exists():
        command.insert(1, "--fresh")

    log(f"Build directory is not configured; configuring it now: {build_dir}")
    configure_code, _ = run_command(command, project_root)
    if configure_code == 0:
        return 0

    # A preset may require Ninja even when CMake can use another generator
    # available on the host. Give local Windows and Unix installations a
    # portable fallback without changing the checked-in preset.
    if preset_configure:
        log("Preset configuration failed; retrying with CMake's default generator.")
        fallback = ["cmake", "--fresh", *direct_configure[1:]]
        configure_code, _ = run_command(fallback, project_root)
    return configure_code


def load_suspects(path: Path):
    if not path.exists():
        return []
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        suspects = data.get("suspects", []) if isinstance(data, dict) else data
        if not isinstance(suspects, list) or not all(isinstance(item, str) for item in suspects):
            raise ValueError("expected a list of test names")
        return suspects
    except (OSError, json.JSONDecodeError, ValueError) as error:
        log(f"ERROR: cannot read suspects cache '{path}': {error}")
        raise SystemExit(2) from error


def save_suspects(path: Path, names) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "format": 1,
        "updated_at": timestamp(),
        "suspects": sorted(set(names)),
    }
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def parse_test_list(output: str):
    return LIST_LINE_RE.findall(output)


def parse_results(output: str):
    results = {name: status.title() for name, status in TEST_LINE_RE.findall(output)}
    for name in FAILED_LIST_RE.findall(output):
        results[name] = "Failed"
    return results


def has_qt_test_failure_details(output: str, names) -> bool:
    """Return true only when each failed target has visible QtTest details."""
    starts = list(CTEST_START_RE.finditer(output))
    if starts:
        sections = {}
        for index, start in enumerate(starts):
            end = starts[index + 1].start() if index + 1 < len(starts) else len(output)
            sections[start.group(1)] = output[start.end():end]
        return all(
            "FAIL!" in sections.get(name, "") or "Totals:" in sections.get(name, "")
            for name in names
        )

    # Non-verbose CTest does not delimit output by target. A single failed
    # target can still be identified safely from --output-on-failure output;
    # multiple failures remain ambiguous and trigger the diagnostic rerun.
    return len(names) == 1 and ("FAIL!" in output or "Totals:" in output)


def print_summary(results, selected, cache_path, build_failed=False):
    counts = {status: sum(value == status for value in results.values())
              for status in ("Passed", "Failed", "Timeout", "Not Run", "Skipped")}
    unknown = [name for name in selected if name not in results]
    if unknown:
        counts["Not Run"] += len(unknown)

    log("SUITE SUMMARY")
    log(f"  Selected: {len(selected)}")
    log(f"  Passed: {counts['Passed']}")
    log(f"  Failed: {counts['Failed']}")
    log(f"  Timeout: {counts['Timeout']}")
    log(f"  Not run: {counts['Not Run']}")
    log(f"  Skipped: {counts['Skipped']}")
    for status in ("Passed", "Failed", "Timeout", "Not Run", "Skipped"):
        names = sorted(name for name, value in results.items() if value == status)
        if names:
            log(f"  {status} tests: {', '.join(names)}")
    if unknown:
        log(f"  Not run tests: {', '.join(unknown)}")
    if build_failed:
        log("  Result: BUILD FAILED (tests were not started)")
    else:
        log(f"  Result: {'PASS' if not any(counts[key] for key in ('Failed', 'Timeout', 'Not Run')) else 'FAIL'}")
    log(f"  Suspects cache: {cache_path}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run LzyDownloader C++ tests headlessly.")
    parser.add_argument("--build-dir", default="build", help="CMake build directory (default: build)")
    parser.add_argument("--config", default="Release", help="Build configuration (default: Release)")
    parser.add_argument("--verbose", action="store_true", help="Print verbose CTest output")
    parser.add_argument(
        "--no-build",
        action="store_true",
        help="Skip configure/build and run the existing CTest build tree",
    )
    parser.add_argument("--suspects", action="store_true", help="Run only tests in the previous-failure cache")
    parser.add_argument("--suspects-file", help="Override the suspects cache path")
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parent.parent
    build_dir = (project_root / args.build_dir).resolve()
    cache_path = Path(args.suspects_file).resolve() if args.suspects_file else build_dir / ".lzy-test-suspects.json"

    if args.no_build:
        if not build_dir.is_dir() or not (build_dir / "CTestTestfile.cmake").is_file():
            log(f"ERROR: no configured CTest build tree found at {build_dir}.")
            print_summary({}, [], cache_path, build_failed=True)
            return 2
        log(f"Skipping configure/build (--no-build); using existing tree: {build_dir}")
    else:
        configure_code = configure_build(project_root, build_dir, args.config)
        if configure_code != 0:
            log(f"CONFIGURE FAILED with exit code {configure_code}; no tests were started.")
            print_summary({}, [], cache_path, build_failed=True)
            return configure_code

        log(f"Build directory: {build_dir}")
        log(f"Configuration: {args.config}")
        build_code, _ = run_command(cmake_build_command(build_dir, args.config), build_dir)
        if build_code != 0:
            log(f"BUILD FAILED with exit code {build_code}; no tests were started.")
            print_summary({}, [], cache_path, build_failed=True)
            return build_code

    env = os.environ.copy()
    # Windows test targets deploy qminimal; qoffscreen is not consistently
    # shipped by the Qt packages used by this project.
    env["QT_QPA_PLATFORM"] = "minimal"
    env["QT_DEBUG_PLUGINS"] = "0"
    list_code, list_output = run_command(["ctest", "-N", "-C", args.config], build_dir, env)
    if list_code != 0:
        log(f"ERROR: unable to enumerate CTest tests (exit code {list_code}).")
        print_summary({}, [], cache_path, build_failed=True)
        return list_code

    available = parse_test_list(list_output)
    selected = available
    if args.suspects:
        requested = load_suspects(cache_path)
        selected = list(dict.fromkeys(name for name in requested if name in available))
        stale = sorted(set(requested) - set(available))
        log(f"Suspects mode: {len(selected)} cached test(s) selected.")
        if stale:
            log(f"Ignoring {len(stale)} stale suspect name(s): {', '.join(stale)}")
        if not selected:
            log("No suspects remain; the suite is clean.")
            print_summary({}, [], cache_path)
            return 0

    ctest = ["ctest", "-C", args.config, "--output-on-failure", "--no-tests=error"]
    if args.verbose:
        ctest.append("-V")
    if selected != available:
        ctest.extend(["-R", "^(" + "|".join(selected) + ")$"])
    # Qt GUI/process tests share runtime state (plugin loading, QSettings,
    # and helper-process caches). Running them concurrently makes failures
    # order-dependent on Windows and can leave the diagnostic rerun with a
    # contaminated process state.
    ctest.extend(["-j", "1"])
    test_code, test_output = run_command(ctest, build_dir, env)
    results = parse_results(test_output)
    failed = [
        name for name, status in results.items()
        if status in {"Failed", "Timeout", "Not Run"}
    ]
    failed.extend(name for name in selected if name not in results)
    if test_code != 0 and failed:
        # The main pass is already serial and --output-on-failure normally
        # includes the QtTest assertion and Totals lines. Avoid rerunning the
        # entire failed executable in that common case; reserve diagnostics
        # for crashes, timeouts, loader failures, and other opaque exits.
        has_opaque_failure = any(
            name not in results or results[name] in {"Timeout", "Not Run"}
            for name in failed
        ) or not has_qt_test_failure_details(test_output, failed)
        if not has_opaque_failure:
            log("CTest already reported QtTest failure details; skipping duplicate diagnostics.")
        else:
            diagnostic_ctest = [
                "ctest", "-C", args.config, "--output-on-failure", "-V", "-j", "1",
                "-R", "^(" + "|".join(sorted(set(failed))) + ")$",
            ]
            log("Failure output was opaque; rerunning failed tests for diagnostics.")
            run_command(diagnostic_ctest, build_dir, env)
            log("Running failed test executables directly for runtime diagnostics.")
            run_direct_test_diagnostics(build_dir, args.config, failed, env)
    try:
        save_suspects(cache_path, failed)
    except OSError as error:
        log(f"ERROR: cannot update suspects cache '{cache_path}': {error}")
        return 2
    print_summary(results, selected, cache_path)
    return test_code


if __name__ == "__main__":
    sys.exit(main())
