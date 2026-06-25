"""Generates a minimal ONNX Identity model for unit testing.

Output: tests/backend/cpu/test_data/identity_1x3x4x4.onnx
  Input:  'images'  float32  [1, 3, 4, 4]
  Output: 'output'  float32  [1, 3, 4, 4]
"""

import os
import sys

import onnx
from onnx import TensorProto, helper


def make_identity_model(input_name: str,
                         output_name: str,
                         shape: list,
                         out_path: str) -> None:
    node = helper.make_node("Identity", inputs=[input_name],
                             outputs=[output_name])

    input_vi = helper.make_tensor_value_info(
        input_name, TensorProto.FLOAT, shape)
    output_vi = helper.make_tensor_value_info(
        output_name, TensorProto.FLOAT, shape)

    graph = helper.make_graph(
        [node], "identity_graph", [input_vi], [output_vi])

    model = helper.make_model(
        graph,
        opset_imports=[helper.make_opsetid("", 11)],
        ir_version=7)

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    onnx.save(model, out_path)
    print(f"Saved: {out_path}")


if __name__ == "__main__":
    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))))
    out = os.path.join(
        repo_root,
        "tests", "backend", "cpu", "test_data", "identity_1x3x4x4.onnx")
    make_identity_model("images", "output", [1, 3, 4, 4], out)
