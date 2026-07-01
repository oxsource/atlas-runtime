"""Generates a minimal test .dlc model for SNPE backend unit testing.

Prerequisites:
  - SNPE SDK installed (SNPE_SDK_PATH environment variable)
  - Python 3.8+ with onnx available

Workflow:
  1. Generate an ONNX identity model (reuses CPU backend infrastructure)
  2. Convert ONNX -> .dlc via snpe-onnx-to-dlc

Usage:
  python tests/backend/snpe/gen_test_model.py

Output:
  tests/backend/snpe/test_data/identity_1x3x4x4.dlc
    Input:  'images'  float32  [1, 3, 4, 4]
    Output: 'output'  float32  [1, 3, 4, 4]
"""

import os
import subprocess
import sys


def make_identity_onnx(out_onnx: str) -> None:
    """Generate an ONNX Identity model, reusing the CPU backend script."""
    cpu_script = os.path.join(
        os.path.dirname(__file__), "..", "cpu", "gen_test_model.py"
    )
    subprocess.run(
        [sys.executable, cpu_script], check=True, cwd=os.path.dirname(cpu_script)
    )
    # The CPU script writes to its own test_data/, copy the result here.
    cpu_onnx = os.path.join(
        os.path.dirname(__file__), "..", "cpu", "test_data",
        "identity_1x3x4x4.onnx"
    )
    import shutil
    shutil.copy(cpu_onnx, out_onnx)
    print(f"Copied ONNX: {out_onnx}")


def onnx_to_dlc(onnx_path: str, dlc_path: str) -> None:
    """Convert ONNX to SNPE .dlc via snpe-onnx-to-dlc."""
    snpe_sdk = os.environ.get("SNPE_SDK_PATH", "")
    if not snpe_sdk:
        print("Error: SNPE_SDK_PATH is not set. "
              "Set it to the root of the SNPE SDK installation.",
              file=sys.stderr)
        print("Model conversion skipped — stub tests run without a .dlc file.",
              file=sys.stderr)
        return

    # The converter binary location depends on the host platform.
    # SNPE SDK only ships Linux x86_64 host tools.
    converter = os.path.join(
        snpe_sdk, "bin", "x86_64-linux-clang", "snpe-onnx-to-dlc"
    )
    if not os.path.isfile(converter):
        print(f"Error: Converter not found at {converter}", file=sys.stderr)
        sys.exit(1)

    subprocess.run(
        [converter, "--input_network", onnx_path,
         "--output_path", dlc_path],
        check=True)
    print(f"Saved: {dlc_path}")


if __name__ == "__main__":
    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))))
    test_data_dir = os.path.join(
        repo_root, "tests", "backend", "snpe", "test_data")
    os.makedirs(test_data_dir, exist_ok=True)

    onnx_path = os.path.join(test_data_dir, "identity_1x3x4x4.onnx")
    dlc_path  = os.path.join(test_data_dir, "identity_1x3x4x4.dlc")

    make_identity_onnx(onnx_path)
    onnx_to_dlc(onnx_path, dlc_path)