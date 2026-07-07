"""Generates ReLU models for the SNPE CPU shared library comparison test.

Generates:
  - relu.onnx -> examples/snpe_cpu_shared/  (used by CPU backend)
  - relu_snpe.dlc -> <output_dir>/           (used by SNPE backend)

Prerequisites:
  - SNPE SDK installed (SNPE_SDK_PATH environment variable) for .dlc conversion
  - Python 3.8+ with onnx available

Usage:
  python3 examples/snpe_cpu_shared/gen_models.py <output_dir>

Example:
  python3 examples/snpe_cpu_shared/gen_models.py /tmp/snpe_sample_models
"""

import os
import shutil
import subprocess
import sys

import onnx
from onnx import TensorProto, helper


def make_relu_onnx(out_path: str) -> None:
    """Generate an ONNX ReLU model: 1x3x4x4 float32."""
    shape = [1, 3, 4, 4]
    node = helper.make_node("Relu",
                             inputs=["images"],
                             outputs=["output"])
    input_vi = helper.make_tensor_value_info(
        "images", TensorProto.FLOAT, shape)
    output_vi = helper.make_tensor_value_info(
        "output", TensorProto.FLOAT, shape)
    graph = helper.make_graph(
        [node], "relu_graph", [input_vi], [output_vi])
    model = helper.make_model(
        graph,
        opset_imports=[helper.make_opsetid("", 11)],
        ir_version=7)
    onnx.save(model, out_path)
    print(f"Saved ONNX: {out_path}")


def onnx_to_dlc(onnx_path: str, dlc_path: str) -> None:
    """Convert ONNX to SNPE .dlc via snpe-onnx-to-dlc."""
    snpe_sdk = os.environ.get("SNPE_SDK_PATH", "")
    if not snpe_sdk:
        print("Warning: SNPE_SDK_PATH is not set. "
              "Skipping .dlc conversion.",
              file=sys.stderr)
        print("Set SNPE_SDK_PATH to the root of the SNPE SDK installation "
              "and re-run to produce the .dlc file.",
              file=sys.stderr)
        return

    converter = os.path.join(
        snpe_sdk, "bin", "x86_64-linux-clang", "snpe-onnx-to-dlc")
    if not os.path.isfile(converter):
        print(f"Error: Converter not found at {converter}", file=sys.stderr)
        sys.exit(1)

    subprocess.run(
        [converter, "--input_network", onnx_path,
         "--output_path", dlc_path],
        check=True)
    print(f"Saved DLC: {dlc_path}")


if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "/tmp/snpe_sample_models"
    os.makedirs(out_dir, exist_ok=True)

    # Determine the examples directory (script may be run from workspace root)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    example_onnx = os.path.join(script_dir, "relu.onnx")

    # ONNX in example dir
    make_relu_onnx(example_onnx)

    # DLC in output dir
    dlc_path = os.path.join(out_dir, "relu_snpe.dlc")
    onnx_to_dlc(example_onnx, dlc_path)

    print(f"Done. Set SAMPLE_MODEL_DIR={out_dir}")