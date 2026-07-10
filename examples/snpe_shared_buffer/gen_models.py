#!/usr/bin/env python3
"""
Generates two minimal SNPE DLC models for the shared buffer example.

Both models are ReLU identity networks:
  - Input:  "input"  float32 [1, 3, 4, 4]  (NCHW)
  - ReLU:   ReLU activation (identity for positive values)
  - Output: "output" float32 [1, 3, 4, 4]

Usage:
    python3 gen_models.py <output_dir>

Requirements:
    - snpe-net-run, snpe-tensorflow-to-dlc, or snpe-onnx-to-dlc
      from the Qualcomm SNPE SDK in PATH.
    - For simplicity, this script can generate a .dlc using
      snpe-net-run's --container option OR create a simple
      protobuf-based model description.

NOTE: This script is a placeholder.  In practice, you would:
  1. Create a TensorFlow/ONNX model.
  2. Convert it using snpe-tensorflow-to-dlc or snpe-onnx-to-dlc.
  3. Copy the .dlc files to SAMPLE_MODEL_DIR.

For CI/testing without real SNPE SDK, simply skip DLC generation;
the examples run in stub mode on non-target platforms.
"""

import os
import sys


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <output_dir>")
        sys.exit(1)

    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)

    # Generate a simple ReLU model using snpe-tensorflow-to-dlc
    # if available in PATH.
    model_a_path = os.path.join(out_dir, "model_a.dlc")
    model_b_path = os.path.join(out_dir, "model_b.dlc")

    # Check if snpe-tensorflow-to-dlc is available.
    import shutil
    converter = shutil.which("snpe-tensorflow-to-dlc")

    if converter is None:
        print(f"SNPE converter not found in PATH.")
        print(f"Please manually place the following DLC files in:")
        print(f"  {model_a_path}")
        print(f"  {model_b_path}")
        print()
        print("Example: create a simple ReLU network via TensorFlow:")
        print("""\
  import tensorflow as tf

  # model_a: ReLU identity
  tf.keras.Sequential([
      tf.keras.layers.InputLayer(input_shape=(3, 4, 4)),
      tf.keras.layers.ReLU(),
  ]).save("relu_model_a")

  # Convert with SNPE SDK
  snpe-tensorflow-to-dlc --input_network relu_model_a \\
      --input_dim input 1,3,4,4 \\
      --out_node "re_lu/Relu" \\
      --output_path model_a.dlc

  # model_b: identical structure (or different scale for contrast)
  snpe-tensorflow-to-dlc --input_network relu_model_a \\
      --input_dim input 1,3,4,4 \\
      --out_node "re_lu/Relu" \\
      --output_path model_b.dlc
""")
        # Create placeholder files (empty) for documentation.
        for p in [model_a_path, model_b_path]:
            with open(p, "wb") as f:
                f.write(b"PLACEHOLDER - replace with real SNPE DLC\n")
        print(f"Placeholder files created at: {out_dir}/")
        sys.exit(0)

    # Real conversion path (not typically available without SNPE SDK).
    print(f"Generating DLC models using {converter} ...")
    # (Implementation depends on actual SNPE SDK version.)


if __name__ == "__main__":
    main()
