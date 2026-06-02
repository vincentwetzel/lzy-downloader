#!/usr/bin/env python3
import os
import sys
import subprocess
import argparse

def main():
    parser = argparse.ArgumentParser(description="Run LzyDownloader C++ tests headlessly.")
    parser.add_argument("--build-dir", default="build", help="Path to the CMake build directory (default: build)")
    parser.add_argument("--config", default="Release", help="Build configuration to test (default: Release)")
    parser.add_argument("--verbose", action="store_true", help="Print verbose test output")
    args = parser.parse_args()

    # Resolve the absolute path to the build directory
    project_root = os.path.dirname(os.path.abspath(__file__))
    build_dir = os.path.join(project_root, args.build_dir)

    if not os.path.isdir(build_dir):
        print(f"Error: Build directory '{build_dir}' does not exist.")
        print("Please build the project first (e.g., 'cmake --preset release' and 'cmake --build build --config Release').")
        sys.exit(1)

    print(f"Running tests from build directory: {build_dir}")
    print(f"Configuration: {args.config}")

    # Build the project to ensure all test executables exist and are up to date
    build_cmd = ["cmake", "--build", ".", "--config", args.config]
    try:
        print(f"Building project: {' '.join(build_cmd)}\n")
        subprocess.run(build_cmd, cwd=build_dir, check=True)
    except subprocess.CalledProcessError as e:
        print(f"\n❌ Build failed with exit code {e.returncode}. Please fix compilation errors first.")
        sys.exit(e.returncode)

    # Set environment variables for headless Qt execution
    env = os.environ.copy()
    # Forces Qt to use the headless offscreen plugin, preventing windows from popping up
    env["QT_QPA_PLATFORM"] = "offscreen"  
    env["QT_DEBUG_PLUGINS"] = "0"         

    # Construct the CTest command
    ctest_cmd = ["ctest", "-C", args.config, "--output-on-failure"]
    if args.verbose:
        ctest_cmd.append("-V")

    try:
        print(f"Executing: {' '.join(ctest_cmd)}\n")
        subprocess.run(ctest_cmd, cwd=build_dir, env=env, check=True)
        print("\n✅ All headless tests completed successfully!")
    except subprocess.CalledProcessError as e:
        print(f"\n❌ Tests failed with exit code {e.returncode}.")
        sys.exit(e.returncode)
    except FileNotFoundError:
        print("Error: 'ctest' command not found. Ensure CMake is installed and added to your system PATH.")
        sys.exit(1)

if __name__ == "__main__":
    main()