"""Platform packaging helpers used by the native release builder."""

import os
import re
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path


GREEN = "\033[32m"
RED = "\033[31m"
YELLOW = "\033[33m"


def find_vcpkg_qmake(build_dir):
    """Return qmake belonging to an optional local vcpkg Qt build."""
    candidates = []
    for triplet_dir in (build_dir / "vcpkg_installed").glob("*/"):
        qt_tools = triplet_dir / "tools"
        for qt_dir_name in ("Qt*", "qt*"):
            candidates.extend(qt_tools.glob(f"{qt_dir_name}/bin/qmake"))
            candidates.extend(qt_tools.glob(f"{qt_dir_name}/bin/qmake6"))

    return next(
        (candidate for candidate in candidates
         if candidate.is_file() and os.access(candidate, os.X_OK)),
        None,
    )


def hide_unused_linux_sql_drivers(qmake_bin, log):
    """Temporarily hide Qt SQL drivers that this SQLite-only app cannot use."""
    if not qmake_bin:
        return []

    query = subprocess.run(
        [str(qmake_bin), "-query", "QT_INSTALL_PLUGINS"],
        capture_output=True,
        text=True,
        check=False,
    )
    if query.returncode != 0 or not query.stdout.strip():
        log("Warning: Could not query Qt's plugin directory; keeping SQL drivers unchanged.", YELLOW)
        return []

    driver_dir = Path(query.stdout.strip()) / "sqldrivers"
    if not driver_dir.is_dir():
        return []

    hidden = []
    hidden_dir = driver_dir.parent / ".lzy-disabled-sqldrivers"
    try:
        hidden_dir.mkdir(exist_ok=True)
    except OSError as error:
        log(f"Warning: Could not prepare temporary Qt SQL driver directory: {error}", YELLOW)
        return []

    for driver in sorted(driver_dir.glob("libqsql*.so")):
        if driver.name == "libqsqlite.so":
            continue
        hidden_driver = hidden_dir / driver.name
        try:
            driver.rename(hidden_driver)
        except OSError as error:
            log(f"Warning: Could not hide unused Qt SQL driver {driver.name}: {error}", YELLOW)
            continue
        hidden.append((driver, hidden_driver))

    if hidden:
        log(
            "Temporarily excluding unused Qt SQL drivers from AppImage deployment: "
            + ", ".join(original.name for original, _ in hidden),
            GREEN,
        )
    elif hidden_dir.exists():
        try:
            hidden_dir.rmdir()
        except OSError:
            pass
    return hidden


def restore_linux_sql_drivers(hidden, log):
    """Restore Qt SQL drivers after linuxdeploy has finished scanning them."""
    hidden_dirs = set()
    for original, hidden_driver in hidden:
        try:
            hidden_driver.rename(original)
            hidden_dirs.add(hidden_driver.parent)
        except OSError as error:
            log(f"Warning: Could not restore Qt SQL driver {original.name}: {error}", YELLOW)
    for hidden_dir in hidden_dirs:
        try:
            hidden_dir.rmdir()
        except OSError:
            pass


def package_linux(app_version, build_dir, log, run_command):
    """Create the Linux AppImage from the already-built application target."""
    appdir = build_dir / "AppDir"
    if appdir.exists():
        shutil.rmtree(appdir)

    # Prevent linuxdeploy from traversing Windows mounts when invoked under WSL.
    path_env = os.environ.get("PATH", "")
    os.environ["PATH"] = ":".join(
        entry for entry in path_env.split(":") if not entry.startswith("/mnt/")
    )

    built_exe = build_dir / "Release" / "LzyDownloader"
    if not built_exe.exists():
        exes = [
            executable for executable in build_dir.glob("**/LzyDownloader")
            if executable.is_file() and os.access(executable, os.X_OK)
        ]
        if exes:
            built_exe = exes[0]
        else:
            log("Error: Could not locate compiled LzyDownloader executable", RED)
            sys.exit(1)

    tooling_dir = build_dir / "tooling"
    tooling_dir.mkdir(parents=True, exist_ok=True)
    ld_path = tooling_dir / "linuxdeploy"
    ld_plugin_path = tooling_dir / "linuxdeploy-plugin-qt"
    if not ld_path.exists():
        urllib.request.urlretrieve(
            "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage",
            ld_path,
        )
        ld_path.chmod(0o755)
    if not ld_plugin_path.exists():
        urllib.request.urlretrieve(
            "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage",
            ld_plugin_path,
        )
        ld_plugin_path.chmod(0o755)

    # linuxdeploy-plugin-qt must query the same Qt installation that built the
    # executable. The release workflow exposes the prebuilt Qt SDK; the vcpkg
    # lookup remains for optional local/source builds.
    qmake_bin = find_vcpkg_qmake(build_dir)
    if qmake_bin is not None:
        os.environ["QMAKE"] = str(qmake_bin.resolve())
    else:
        qmake_bin = shutil.which("qmake6") or shutil.which("qmake")
        if qmake_bin:
            os.environ["QMAKE"] = qmake_bin
        elif Path("/usr/lib/qt6/bin/qmake").exists():
            os.environ["QMAKE"] = "/usr/lib/qt6/bin/qmake"
        else:
            os.environ["QT_SELECT"] = "qt6"

    icon_path = Path("src/resources/icon.png")
    resized_icon = build_dir / "app-icon.png"
    icon_resized = False
    try:
        try:
            from PIL import Image
        except ImportError:
            log("Pillow not found; installing it for AppImage icon preparation.", YELLOW)
            subprocess.run(
                [sys.executable, "-m", "pip", "install", "Pillow"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            from PIL import Image
        with Image.open(icon_path) as img:
            img.resize((512, 512), Image.Resampling.LANCZOS).save(resized_icon)
        icon_resized = True
    except Exception:
        pass

    if not icon_resized and shutil.which("ffmpeg"):
        try:
            subprocess.run(
                ["ffmpeg", "-y", "-i", str(icon_path), "-vf", "scale=512:512", str(resized_icon)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            icon_resized = resized_icon.exists()
        except Exception:
            pass

    if not icon_resized and shutil.which("convert"):
        try:
            subprocess.run(
                ["convert", str(icon_path), "-resize", "512x512", str(resized_icon)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            icon_resized = resized_icon.exists()
        except Exception:
            pass

    if not icon_resized:
        try:
            shutil.copy(icon_path, resized_icon)
        except Exception as error:
            log(f"Warning: Failed to copy icon fallback to app-icon.png: {error}", YELLOW)

    linux_desktop = build_dir / "LzyDownloader.desktop"
    desktop_content = Path("src/ui/LzyDownloader.desktop").read_text(encoding="utf-8")
    desktop_content = re.sub(
        r"^Icon=.*$", f"Icon={resized_icon.stem}", desktop_content, flags=re.MULTILINE
    )
    linux_desktop.write_text(desktop_content, encoding="utf-8")

    ldd_result = subprocess.run(
        ["ldd", str(built_exe)], capture_output=True, text=True, check=False
    )
    dynamic_qt = "libQt6" in ldd_result.stdout
    linuxdeploy_args = [
        str(ld_path.resolve()),
        "--appdir", str(appdir),
        "-e", str(built_exe),
        "-d", str(linux_desktop),
        "-i", str(resized_icon),
        "--exclude-library", "libdbus-1.so.3",
    ]
    if dynamic_qt:
        os.environ["EXTRA_QT_MODULES"] = "sql"
        linuxdeploy_args.extend(["--plugin", "qt"])
        log("Detected dynamic Qt; enabling linuxdeploy-plugin-qt.", GREEN)
    else:
        os.environ.pop("EXTRA_QT_MODULES", None)
        log("Detected statically linked Qt; skipping linuxdeploy-plugin-qt.", GREEN)

    linuxdeploy_args.extend(["--output", "appimage"])
    os.environ["PATH"] = str(tooling_dir.resolve()) + os.pathsep + os.environ.get("PATH", "")
    hidden_sql_drivers = hide_unused_linux_sql_drivers(os.environ.get("QMAKE"), log) if dynamic_qt else []
    try:
        run_command(linuxdeploy_args, cwd=build_dir)
    finally:
        restore_linux_sql_drivers(hidden_sql_drivers, log)

    generated_appimage = build_dir / "LzyDownloader-x86_64.AppImage"
    if generated_appimage.exists():
        target_appimage = build_dir / f"LzyDownloader-{app_version}-x86_64.AppImage"
        shutil.move(str(generated_appimage), str(target_appimage))
        log(f"\n=== Linux Build Success: {target_appimage} ===", GREEN)
