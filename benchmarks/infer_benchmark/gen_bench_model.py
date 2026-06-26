"""Generates a benchmark Identity ONNX model of a given spatial size.

Usage:
    python3 gen_bench_model.py <output_dir> [size]

    size: spatial dimension H=W (default: 224)

Example:
    python3 benchmarks/infer_benchmark/gen_bench_model.py /tmp/atlas_bench_models 224
"""

import os
import sys

import onnx
from onnx import TensorProto, helper


def make_identity_model(size: int, out_dir: str) -> None:
    shape = [1, 3, size, size]
    node = helper.make_node("Identity", inputs=["images"], outputs=["out"])
    input_vi  = helper.make_tensor_value_info("images", TensorProto.FLOAT, shape)
    output_vi = helper.make_tensor_value_info("out",    TensorProto.FLOAT, shape)
    graph = helper.make_graph([node], "bench_graph", [input_vi], [output_vi])
    model = helper.make_model(
        graph,
        opset_imports=[helper.make_opsetid("", 11)],
        ir_version=7)
    path = os.path.join(out_dir, f"bench_{size}x{size}.onnx")
    onnx.save(model, path)
    print(f"Saved: {path}")


if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "/tmp/atlas_bench_models"
    size    = int(sys.argv[2]) if len(sys.argv) > 2 else 224
    os.makedirs(out_dir, exist_ok=True)
    make_identity_model(size, out_dir)
    print(f"Done. Set BENCH_MODEL_DIR={out_dir} and BENCH_SIZE={size}")
