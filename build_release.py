#!/usr/bin/env python3
import os
import sys
import platform
import subprocess
import re
import shutil
import urllib.request
from pathlib import Path

# ANSI Colors
CYAN = "\033[36m"
YELLOW = "\033[33m"
GREEN = "\033[32m"
RED = "\033[31m"
RESET = "\033[0m"

def log(msg, color=RESET):
    print(f"{color}{msg}{RESET}")

def run_command(cmd, shell=False):
    log(f"Executing: {' '.join(cmd) if isinstance(cmd, list) else cmd}", YELLOW)
    result = subprocess.run(cmd, shell=shell)
    if result.returncode != 0:
        log(f"Command failed with exit code {result.returncode}", RED)
        sys.exit(result.returncode)

def main():
    if platform.system() == "Windows":
        os.system("")  # Enable ANSI color sequences in Windows Console

    log("=== LzyDownloader Unified Release Builder ===", CYAN)

    # 1. Parse Version from CMakeLists.txt
    cmake_path = Path("CMakeLists.txt")
    if not cmake_path.exists():
        log("Error: CMakeLists.txt not found!", RED)
        sys.exit(1)

    content = cmake_path.read_text(encoding="utf-8")
    match = re.search(r'project\(LzyDownloader VERSION ([0-9]+\.[0-9]+\.[0-9]+)', content)
    if not match:
        log("Error: Could not parse version from CMakeLists.txt", RED)
        sys.exit(1)

    app_version = match.group(1)
    log(f"Detected Application Version: {app_version}", GREEN)

    build_dir = Path("build-release")

    # 2. Clean old release build cache
    log("\n[0/4] Cleaning old build cache...", YELLOW)
    if build_dir.exists():
        shutil.rmtree(build_dir)

    # 3. Update Extractor Lists
    log("\n[1/4] Refreshing Extractor Lists...", YELLOW)
    run_command([sys.executable, "./update_yt-dlp_extractors.py"])
    run_command([sys.executable, "./update_gallery-dl_extractors.py"])

    # 4. Configure CMake
    log("\n[2/4] Configuring CMake (Release)...", YELLOW)
    cmake_args = ["cmake", "-B", str(build_dir), "-DCMAKE_BUILD_TYPE=Release"]

    system_platform = platform.system()
    if system_platform == "Windows":
        vcpkg_root = os.environ.get("VCPKG_ROOT", "E:/vcpkg")
        toolchain = Path(vcpkg_root) / "scripts/buildsystems/vcpkg.cmake"
        if toolchain.exists():
            cmake_args.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain.as_posix()}")
    else:
        vcpkg_root = os.environ.get("VCPKG_ROOT", "/usr/local/share/vcpkg")
        toolchain = Path(vcpkg_root) / "scripts/buildsystems/vcpkg.cmake"
        if toolchain.exists():
            cmake_args.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain.as_posix()}")

    run_command(cmake_args)

    # 5. Build C++ Application
    log("\n[3/4] Compiling Application...", YELLOW)
    build_args = ["cmake", "--build", str(build_dir), "--config", "Release"]
    if system_platform != "Windows":
        import multiprocessing
        build_args.extend(["--parallel", str(multiprocessing.cpu_count())])
    run_command(build_args)

    # 6. Packaging & Verification
    log("\n[4/4] Verifying and Packaging Release...", YELLOW)
    if system_platform == "Windows":
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

        # Compile NSIS Installer
        nsis_path = Path("C:/Program Files (x86)/NSIS/makensis.exe")
        if nsis_path.exists():
            run_command([
                str(nsis_path),
                f"/DAPP_VERSION={app_version}",
                f"/DRELEASE_BUILD_DIR={build_dir}\\Release",
                "LzyDownloader.nsi"
            ])
            log(f"\n=== Windows Build Success: LzyDownloader-Setup-{app_version}.exe ===", GREEN)
        else:
            log(f"Error: NSIS compiler not found at {nsis_path}. Packaging aborted.", RED)
            sys.exit(1)

    elif system_platform == "Linux":
        appdir = build_dir / "AppDir"
        if appdir.exists():
            shutil.rmtree(appdir)

        # Clean PATH to remove Windows mounts (e.g., /mnt/c/...) under WSL.
        # This prevents linuxdeploy from crashing with a Permission Denied filesystem_error
        path_env = os.environ.get("PATH", "")
        filtered_paths = [p for p in path_env.split(":") if not p.startswith("/mnt/")]
        os.environ["PATH"] = ":".join(filtered_paths)

        # Find build artifact (accounting for flexible path locations)
        built_exe = build_dir / "Release" / "LzyDownloader"
        if not built_exe.exists():
            exes = [e for e in build_dir.glob("**/LzyDownloader") if e.is_file() and os.access(e, os.X_OK)]
            if exes:
                built_exe = exes[0]
            else:
                log("Error: Could not locate compiled LzyDownloader executable", RED)
                sys.exit(1)

        # Grab AppImage dependencies
        ld_path = Path("linuxdeploy")
        ld_plugin_path = Path("linuxdeploy-plugin-qt")
        if not ld_path.exists():
            urllib.request.urlretrieve("https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage", ld_path)
            ld_path.chmod(0o755)
        if not ld_plugin_path.exists():
            urllib.request.urlretrieve("https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage", ld_plugin_path)
            ld_plugin_path.chmod(0o755)

        # Force linuxdeploy-plugin-qt to use the correct Qt6 qmake binary, avoiding qtchooser bugs on Ubuntu
        qmake_bin = shutil.which("qmake6")
        if qmake_bin:
            os.environ["QMAKE"] = qmake_bin
        elif Path("/usr/lib/qt6/bin/qmake").exists():
            os.environ["QMAKE"] = "/usr/lib/qt6/bin/qmake"
        else:
            os.environ["QT_SELECT"] = "qt6"

        # Generate a temporary 512x512 icon for linuxdeploy to avoid the 1024px limit
        icon_path = Path("src/resources/icon.png")
        resized_icon = build_dir / "app-icon.png"
        icon_resized = False

        # Tier 1: Try PIL (Pillow), auto-installing if missing
        try:
            try:
                from PIL import Image
            except ImportError:
                log("Pillow not found. Attempting to install it inside the virtual environment...", YELLOW)
                subprocess.run([sys.executable, "-m", "pip", "install", "Pillow"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                from PIL import Image
            with Image.open(icon_path) as img:
                img.resize((512, 512), Image.Resampling.LANCZOS).save(resized_icon)
            icon_resized = True
        except Exception:
            pass

        # Tier 2: Try FFmpeg fallback
        if not icon_resized and shutil.which("ffmpeg"):
            try:
                subprocess.run(
                    ["ffmpeg", "-y", "-i", str(icon_path), "-vf", "scale=512:512", str(resized_icon)],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
                )
                icon_resized = resized_icon.exists()
            except Exception:
                pass

        # Tier 3: Try ImageMagick fallback
        if not icon_resized and shutil.which("convert"):
            try:
                subprocess.run(
                    ["convert", str(icon_path), "-resize", "512x512", str(resized_icon)],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
                )
                icon_resized = resized_icon.exists()
            except Exception:
                pass

        if not icon_resized:
            try:
                shutil.copy(icon_path, resized_icon)
            except Exception as e:
                log(f"Warning: Failed to copy icon fallback to app-icon.png: {e}", YELLOW)

        deploy_icon = resized_icon
        linux_desktop = build_dir / "LzyDownloader.desktop"
        desktop_content = Path("src/ui/LzyDownloader.desktop").read_text(encoding="utf-8")
        desktop_content = re.sub(r"^Icon=.*$", f"Icon={deploy_icon.stem}", desktop_content, flags=re.MULTILINE)
        linux_desktop.write_text(desktop_content, encoding="utf-8")

        os.environ["EXTRA_QT_PLUGINS"] = "sqldrivers/libqsqlite.so"
        run_command([
            "./linuxdeploy",
            "--appdir", str(appdir),
            "-e", str(built_exe),
            "-d", str(linux_desktop),
            "-i", str(deploy_icon),
            "--plugin", "qt",
            "--output", "appimage"
        ])

        generated_appimage = Path("LzyDownloader-x86_64.AppImage")
        if generated_appimage.exists():
            target_appimage = f"LzyDownloader-{app_version}-x86_64.AppImage"
            shutil.move(generated_appimage, target_appimage)
            log(f"\n=== Linux Build Success: {target_appimage} ===", GREEN)

if __name__ == "__main__":
    main()
