"""Generates detector.onnx and classifier.onnx for the two_model_pipeline sample.

Usage:
    python3 gen_models.py <output_dir>

Example:
    python3 examples/two_model_pipeline/gen_models.py /tmp/atlas_sample_models
"""

import os
import sys

import onnx
from onnx import TensorProto, helper


def make_identity_model(model_id: str,
                         input_name: str,
                         output_name: str,
                         shape: list,
                         out_dir: str) -> None:
    node = helper.make_node("Identity",
                             inputs=[input_name],
                             outputs=[output_name])
    input_vi  = helper.make_tensor_value_info(
        input_name,  TensorProto.FLOAT, shape)
    output_vi = helper.make_tensor_value_info(
        output_name, TensorProto.FLOAT, shape)
    graph = helper.make_graph(
        [node], f"{model_id}_graph", [input_vi], [output_vi])
    model = helper.make_model(
        graph,
        opset_imports=[helper.make_opsetid("", 11)],
        ir_version=7)
    path = os.path.join(out_dir, f"{model_id}.onnx")
    onnx.save(model, path)
    print(f"Saved: {path}")


if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "/tmp/atlas_sample_models"
    os.makedirs(out_dir, exist_ok=True)
    # detector: identity 1×3×32×32, has normalize in manifest
    make_identity_model("detector",   "images", "features", [1, 3, 32, 32], out_dir)
    # classifier: identity 1×3×32×32, no normalize in manifest
    make_identity_model("classifier", "images", "scores",   [1, 3, 32, 32], out_dir)
    print("Done. Set SAMPLE_MODEL_DIR=" + out_dir)
