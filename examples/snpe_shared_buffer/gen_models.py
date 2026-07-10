#!/usr/bin/env python3
"""Generates two ReLU .dlc models for the SNPE shared buffer demo.

Both models are simple ReLU identity networks (1x3x4x4 float32 input/output).
They are structurally identical — the demo's purpose is to test shared input
buffer functionality between multiple models.

Prerequisites:
  - SNPE SDK installed (SNPE_SDK_PATH environment variable)
  - Python 3.8+ with onnx available

Workflow:
  1. Generate two ONNX ReLU models (1x3x4x4 float32)
  2. Convert ONNX -> .dlc via snpe-onnx-to-dlc

Usage:
  python3 examples/snpe_shared_buffer/gen_models.py <output_dir>

Example:
  python3 examples/snpe_shared_buffer/gen_models.py /tmp/snpe_models
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
    print(f"  Saved ONNX: {out_path}")


def onnx_to_dlc(onnx_path: str, dlc_path: str) -> None:
    """Convert ONNX to SNPE .dlc via snpe-onnx-to-dlc."""
    snpe_sdk = os.environ.get("SNPE_SDK_PATH", "")
    if not snpe_sdk:
        print("  Warning: SNPE_SDK_PATH is not set. "
              "Skipping .dlc conversion.",
              file=sys.stderr)
        print("  Set SNPE_SDK_PATH to the root of the SNPE SDK installation "
              "and re-run to produce the .dlc file.",
              file=sys.stderr)
        return

    converter = os.path.join(
        snpe_sdk, "bin", "x86_64-linux-clang", "snpe-onnx-to-dlc")
    if not os.path.isfile(converter):
        print(f"  Error: Converter not found at {converter}", file=sys.stderr)
        sys.exit(1)

    subprocess.run(
        [converter, "--input_network", onnx_path,
         "--output_path", dlc_path],
        check=True)
    print(f"  Saved DLC: {dlc_path}")


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <output_dir>")
        sys.exit(1)

    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    print(f"Generating models in: {out_dir}")

    for model_id in ["model_a", "model_b"]:
        print(f"\n--- {model_id} ---")
        onnx_path = os.path.join(out_dir, f"{model_id}.onnx")
        dlc_path = os.path.join(out_dir, f"{model_id}.dlc")

        make_relu_onnx(onnx_path)
        onnx_to_dlc(onnx_path, dlc_path)

    print(f"\nDone. Set SAMPLE_MODEL_DIR={out_dir}")
    print("Then run:\n"
          f"  SAMPLE_MODEL_DIR={out_dir} \\\n"
          "  ./bazel-bin/examples/snpe_shared_buffer/snpe_shared_buffer \\\n"
          "  examples/snpe_shared_buffer/manifest.json")


if __name__ == "__main__":
    main()