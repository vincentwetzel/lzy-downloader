#!/usr/bin/env python3
import os
import sys
import argparse
import platform
import plistlib
import subprocess
import re
import shutil
from pathlib import Path

from tools.release_packaging import package_linux

# ANSI Colors
CYAN = "\033[36m"
YELLOW = "\033[33m"
GREEN = "\033[32m"
RED = "\033[31m"
RESET = "\033[0m"

def log(msg, color=RESET):
    use_color = sys.stdout.isatty() and os.environ.get("TERM", "") != "dumb"
    print(f"{color}{msg}{RESET}" if use_color else msg)

def run_command(cmd, shell=False, cwd=None):
    log(f"Executing: {' '.join(cmd) if isinstance(cmd, list) else cmd}", YELLOW)
    result = subprocess.run(cmd, shell=shell, cwd=cwd)
    if result.returncode != 0:
        log(f"Command failed with exit code {result.returncode}", RED)
        sys.exit(result.returncode)


def find_qt_tool(tool_name):
    """Locate a Qt deployment tool from PATH or the configured Qt installation."""
    path_candidate = shutil.which(tool_name)
    if path_candidate:
        return Path(path_candidate)

    qt_dir = os.environ.get("Qt6_DIR") or os.environ.get("QTDIR") or os.environ.get("QT_DIR")
    if not qt_dir:
        return None

    qt_dir_path = Path(qt_dir)
    candidates = [qt_dir_path / "bin" / tool_name]
    if qt_dir_path.name == "Qt6" and qt_dir_path.parent.name == "cmake":
        candidates.append(qt_dir_path.parents[2] / "bin" / tool_name)

    return next((candidate for candidate in candidates if candidate.is_file()), None)


def find_configured_qt_prefix():
    """Return the Qt SDK prefix exposed by install-qt-action or a local build."""
    for variable_name in ("QT_ROOT_DIR", "Qt6_DIR", "QTDIR", "QT_DIR"):
        configured_path = os.environ.get(variable_name)
        if not configured_path:
            continue

        path = Path(configured_path)
        if (path / "lib" / "cmake" / "Qt6" / "Qt6Config.cmake").exists():
            return path
        if path.name == "Qt6" and path.parent.name == "cmake":
            return path.parents[2]

    return None


def create_macos_icon(source_icon, destination_icon):
    """Convert the release PNG into an ICNS file required by a macOS app bundle."""
    iconset_dir = destination_icon.with_suffix(".iconset")
    if iconset_dir.exists():
        shutil.rmtree(iconset_dir)
    iconset_dir.mkdir(parents=True)

    icon_sizes = {
        "icon_16x16.png": 16,
        "icon_16x16@2x.png": 32,
        "icon_32x32.png": 32,
        "icon_32x32@2x.png": 64,
        "icon_128x128.png": 128,
        "icon_128x128@2x.png": 256,
        "icon_256x256.png": 256,
        "icon_256x256@2x.png": 512,
        "icon_512x512.png": 512,
        "icon_512x512@2x.png": 1024,
    }
    for file_name, size in icon_sizes.items():
        run_command([
            "sips", "-z", str(size), str(size), str(source_icon),
            "--out", str(iconset_dir / file_name),
        ])
    run_command(["iconutil", "-c", "icns", str(iconset_dir), "-o", str(destination_icon)])


def parse_arguments():
    """Parse release-builder options without changing the default host build."""
    parser = argparse.ArgumentParser(
        description="Build and package LzyDownloader for the current host OS."
    )
    parser.add_argument(
        "--target",
        choices=("auto", "windows", "linux", "macos"),
        default="auto",
        help="Target packaging path (default: auto-detect the host OS).",
    )
    return parser.parse_args()


def resolve_target_platform(target_name):
    """Resolve and validate the requested native release platform."""
    host_platform = platform.system()
    platform_names = {
        "Windows": "windows",
        "Linux": "linux",
        "Darwin": "macos",
    }
    host_name = platform_names.get(host_platform)
    if host_name is None:
        log(f"Error: Unsupported host release platform: {host_platform}", RED)
        sys.exit(1)

    resolved_name = host_name if target_name == "auto" else target_name
    if resolved_name != host_name:
        log(
            f"Error: --target {resolved_name} requires a native {resolved_name} host; "
            f"this machine is {host_name}. Cross-platform release builds are not supported.",
            RED,
        )
        sys.exit(1)
    return resolved_name


def parse_semantic_version(version):
    """Return a comparable release-version tuple for a strict X.Y.Z value."""
    return tuple(int(part) for part in version.split("."))


def validate_release_version(app_version):
    """Reject accidental rebuilds of an already-tagged release."""
    if os.environ.get("LZY_ALLOW_VERSION_REBUILD") == "1":
        log("Version monotonicity check explicitly bypassed.", YELLOW)
        return

    tag_ref = os.environ.get("GITHUB_REF", "")
    if tag_ref.startswith("refs/tags/"):
        expected_ref = f"refs/tags/v{app_version}"
        if tag_ref != expected_ref:
            log(
                f"Error: CI tag {tag_ref.removeprefix('refs/tags/')} does not "
                f"match CMake version {app_version}.",
                RED,
            )
            sys.exit(1)
        # The tagged release is expected to equal the newest tag. The
        # monotonicity rule applies to untagged local builds only.
        return
    local_tag_result = subprocess.run(
        ["git", "describe", "--tags", "--exact-match"],
        capture_output=True,
        text=True,
        check=False,
    )
    local_tag = local_tag_result.stdout.strip()
    if local_tag:
        expected_tag = f"v{app_version}"
        if local_tag != expected_tag:
            log(
                f"Error: checked-out tag {local_tag} does not match CMake "
                f"version {app_version}.",
                RED,
            )
            sys.exit(1)
        log(f"Exact local release tag {local_tag} matches CMake version.", GREEN)
        return
    elif os.environ.get("GITHUB_EVENT_NAME") == "workflow_dispatch":
        log("Manual workflow validation: skipping tag monotonicity check.", YELLOW)
        return

    git_result = subprocess.run(
        ["git", "tag", "--list", "v[0-9]*"],
        capture_output=True,
        text=True,
        check=False,
    )
    if git_result.returncode != 0:
        log("Warning: Could not inspect Git tags; continuing without monotonicity validation.", YELLOW)
        return

    tag_versions = []
    for tag in git_result.stdout.splitlines():
        match = re.fullmatch(r"v([0-9]+\.[0-9]+\.[0-9]+)", tag.strip())
        if match:
            tag_versions.append((parse_semantic_version(match.group(1)), tag.strip()))

    if not tag_versions:
        return

    newest_version, newest_tag = max(tag_versions)
    if parse_semantic_version(app_version) <= newest_version:
        log(
            f"Error: CMake version {app_version} is not newer than the latest "
            f"release tag {newest_tag}. Bump the release version first, or set "
            "LZY_ALLOW_VERSION_REBUILD=1 for an intentional rebuild.",
            RED,
        )
        sys.exit(1)

def main():
    log("=== LzyDownloader Unified Release Builder ===", CYAN)
    args = parse_arguments()
    target_platform = resolve_target_platform(args.target)
    log(f"Release target: {target_platform}", GREEN)

    # 1. Parse Version from CMakeLists.txt
    cmake_path = Path("CMakeLists.txt")
    if not cmake_path.exists():
        log("Error: CMakeLists.txt not found!", RED)
        sys.exit(1)

    content = cmake_path.read_text(encoding="utf-8")
    match = re.search(
        r'project\s*\(\s*LzyDownloader\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)',
        content,
        flags=re.IGNORECASE,
    )
    if not match:
        log("Error: Could not parse version from CMakeLists.txt", RED)
        sys.exit(1)

    app_version = match.group(1)
    log(f"Detected Application Version: {app_version}", GREEN)
    validate_release_version(app_version)

    build_dir = Path("build-release").resolve()

    # 2. Clean old release build cache
    log("\n[0/4] Cleaning old build cache...", YELLOW)
    if build_dir.exists():
        shutil.rmtree(build_dir)

    # 3. Update Extractor Lists
    log("\n[1/4] Refreshing Extractor Lists...", YELLOW)
    run_command([sys.executable, "tools/update_yt-dlp_extractors.py"])
    run_command([sys.executable, "tools/update_gallery-dl_extractors.py"])

    # 4. Configure CMake
    log("\n[2/4] Configuring CMake (Release)...", YELLOW)
    cmake_args = ["cmake", "-B", str(build_dir), "-DCMAKE_BUILD_TYPE=Release"]

    use_vcpkg = os.environ.get("LZY_USE_VCPKG", "1").strip().lower() not in {
        "0", "false", "no", "off"
    }
    if target_platform in ("windows", "linux") and use_vcpkg:
        vcpkg_root = os.environ.get("VCPKG_ROOT", "E:/vcpkg")
        toolchain = Path(vcpkg_root) / "scripts/buildsystems/vcpkg.cmake"
        if toolchain.exists():
            cmake_args.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain.as_posix()}")
            release_triplet = {
                "windows": "x64-windows-release",
                "linux": "x64-linux-release",
            }[target_platform]
            overlay_triplets = Path("triplets").resolve()
            cmake_args.extend([
                f"-DVCPKG_TARGET_TRIPLET={release_triplet}",
                f"-DVCPKG_OVERLAY_TRIPLETS={overlay_triplets.as_posix()}",
            ])
        else:
            log("vcpkg was requested but its toolchain was not found; using configured Qt.", YELLOW)
    elif target_platform in ("windows", "linux"):
        qt_prefix = find_configured_qt_prefix()
        if qt_prefix is not None:
            cmake_args.append(f"-DCMAKE_PREFIX_PATH={qt_prefix.as_posix()}")
            log(f"Using prebuilt Qt SDK at {qt_prefix}", GREEN)
        else:
            log("No Qt SDK prefix was exposed; relying on CMake's default Qt search paths.", YELLOW)

    compiler_launcher = os.environ.get("LZY_CXX_COMPILER_LAUNCHER", "").strip()
    if compiler_launcher:
        cmake_args.append(f"-DCMAKE_CXX_COMPILER_LAUNCHER={compiler_launcher}")
        log(f"Using C++ compiler launcher: {compiler_launcher}", GREEN)

    if target_platform == "linux" and shutil.which("ninja"):
        cmake_args.extend(["-G", "Ninja"])
        log("Using Ninja for the Linux release build.", GREEN)

    run_command(cmake_args)

    # 5. Build C++ Application
    log("\n[3/4] Compiling Application...", YELLOW)
    parallel_jobs = max(1, os.cpu_count() or 1)
    build_args = [
        "cmake", "--build", str(build_dir),
        "--target", "LzyDownloader",
        "--config", "Release",
        "--parallel", str(parallel_jobs),
    ]
    log(f"Building with up to {parallel_jobs} parallel jobs.", GREEN)
    run_command(build_args)

    # 6. Packaging & Verification
    log("\n[4/4] Verifying and Packaging Release...", YELLOW)
    if target_platform == "windows":
        built_exe = build_dir / "Release" / "LzyDownloader.exe"
        if not built_exe.exists():
            log(f"Error: Executable not found at {built_exe}", RED)
            sys.exit(1)

        # Verify Windows Metadata version matches
        try:
            cmd = f"(Get-Item '{built_exe}').VersionInfo.ProductVersion"
            built_version = subprocess.check_output(["powershell", "-Command", cmd], text=True).strip()
            if built_version != app_version:
                log(f"Error: Version mismatch! CMake is {app_version}, but binary is {built_version}", RED)
                sys.exit(1)
            log(f"Verified executable version: {built_version}", GREEN)
        except Exception as e:
            log(f"Warning: Could not verify built executable version metadata: {e}", YELLOW)

        # Compile NSIS Installer. Prefer PATH so package-manager installs work,
        # then retain the standard Windows installation path for local builds.
        nsis_candidates = [
            Path(shutil.which("makensis") or ""),
            Path("C:/Program Files (x86)/NSIS/makensis.exe"),
        ]
        nsis_path = next((candidate for candidate in nsis_candidates if candidate.is_file()), None)
        if nsis_path is not None:
            run_command([
                str(nsis_path),
                f"/DAPP_VERSION={app_version}",
                f"/DRELEASE_BUILD_DIR={build_dir}\\Release",
                "LzyDownloader.nsi"
            ])
            log(f"\n=== Windows Build Success: LzyDownloader-Setup-{app_version}.exe ===", GREEN)
        else:
            log("Error: NSIS compiler not found on PATH or at C:/Program Files (x86)/NSIS/makensis.exe. Packaging aborted.", RED)
            sys.exit(1)

    elif target_platform == "linux":
        package_linux(app_version, build_dir, log, run_command)

    elif target_platform == "macos":
        app_candidates = [
            app for app in build_dir.glob("**/LzyDownloader.app")
            if app.is_dir()
        ]
        if not app_candidates:
            log("Error: Could not locate the compiled LzyDownloader.app bundle", RED)
            sys.exit(1)
        app_bundle = app_candidates[0]

        macdeployqt = find_qt_tool("macdeployqt")
        if macdeployqt is None:
            log("Error: macdeployqt was not found in PATH or the configured Qt installation.", RED)
            sys.exit(1)
        run_command([str(macdeployqt), str(app_bundle), "-always-overwrite"])

        resources_dir = app_bundle / "Contents" / "Resources"
        resources_dir.mkdir(parents=True, exist_ok=True)
        bundle_icon = resources_dir / "LzyDownloader.icns"
        create_macos_icon(Path("src/resources/icon.png"), bundle_icon)

        info_plist_path = app_bundle / "Contents" / "Info.plist"
        try:
            with info_plist_path.open("rb") as plist_file:
                info_plist = plistlib.load(plist_file)
            info_plist["CFBundleIconFile"] = bundle_icon.name
            with info_plist_path.open("wb") as plist_file:
                plistlib.dump(info_plist, plist_file)
        except (OSError, plistlib.InvalidFileException) as error:
            log(f"Error: Failed to set the macOS bundle icon: {error}", RED)
            sys.exit(1)

        release_arch = os.environ.get("LZY_RELEASE_ARCH", platform.machine()).lower()
        architecture_aliases = {"amd64": "x86_64", "x64": "x86_64", "aarch64": "arm64"}
        release_arch = architecture_aliases.get(release_arch, release_arch)
        if release_arch not in ("x86_64", "arm64"):
            log(f"Error: Unsupported macOS release architecture: {release_arch}", RED)
            sys.exit(1)

        target_dmg = Path(f"LzyDownloader-{app_version}-macos-{release_arch}.dmg")
        if target_dmg.exists():
            target_dmg.unlink()
        run_command([
            "hdiutil", "create", "-volname", "LzyDownloader",
            "-srcfolder", str(app_bundle), "-ov", "-format", "UDZO", str(target_dmg),
        ])
        log(f"\n=== macOS Build Success: {target_dmg} ===", GREEN)

    else:
        log(f"Error: Unsupported release target: {target_platform}", RED)
        sys.exit(1)

if __name__ == "__main__":
    main()
