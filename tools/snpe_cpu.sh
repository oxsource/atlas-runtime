#!/bin/bash
# Usage:
#   source tools/spne_cpu.sh env    # setup SNPE environment variables (must source)
#   bash tools/spne_cpu.sh gen      # generate sample DLC models
#   bash tools/spne_cpu.sh run      # build and run snpe_cpu example

set -euo pipefail

case "${1:-}" in
  env)
    export SNPE_ROOT=/opt/qcom/sdk/snpe-1.50.0.2622
    export SNPE_SDK_PATH=$SNPE_ROOT
    export PYTHONPATH=$SNPE_ROOT/lib/python
    export LD_LIBRARY_PATH=$SNPE_ROOT/lib/x86_64-linux-clang:$CONDA_PREFIX/lib:$LD_LIBRARY_PATH
    echo "SNPE environment variables set."
    ;;
  gen)
    python examples/snpe_cpu/gen_models.py /tmp/snpe_sample_models_v1
    ;;
  run)
    bazel build //examples/snpe_cpu:snpe_cpu
    export SAMPLE_MODEL_DIR=/tmp/snpe_sample_models_v1
    ./bazel-bin/examples/snpe_cpu/snpe_cpu examples/snpe_cpu/manifest.json
    ;;
  *)
    echo "Usage: $0 {env|gen|run}"
    echo "  env  - setup SNPE environment (use 'source')"
    echo "  gen  - generate sample DLC models"
    echo "  run  - build and run snpe_cpu example"
    exit 1
    ;;
esac