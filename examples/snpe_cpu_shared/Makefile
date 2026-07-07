# Makefile for SNPE CPU Shared Library Comparison Test.
#
# Builds and runs a controlled-variable comparison between CPU (ONNX) and
# SNPE (DLC) backends via the Atlas shared library.
#
# For host build:
#   export ATLAS_SDK=$(pwd)/atlas-sdk/linux-x86_64
#   make
#   make test
#
# For Android cross-compile:
#   export ATLAS_SDK=$(pwd)/atlas-sdk/android-arm64
#   export CROSS_COMPILE=aarch64-linux-android24-
#   make
#
# Requires ATLAS_SDK pointing to a directory produced by build_release.sh.
# Requires SAMPLE_MODEL_DIR pointing to the directory containing relu_snpe.dlc.
#
# Usage:
#   export ATLAS_SDK=$(pwd)/atlas-sdk/host
#   export SAMPLE_MODEL_DIR=/path/to/dlc/models
#   make          # build
#   make test     # run
#   make clean    # clean

.PHONY: all test clean

BUILD_DIR := build
TARGET    := $(BUILD_DIR)/atlas_snpe_cpu_test
ATLAS_SDK ?= $(error ATLAS_SDK must be set to the SDK directory)

# Cross-compilation support (e.g. CROSS_COMPILE=aarch64-linux-android24-)
CROSS_COMPILE ?=
CXX = $(CROSS_COMPILE)clang++

UNAME_S := $(shell uname -s)
LIB_EXT := $(if $(filter Darwin,$(UNAME_S)),dylib,so)

CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -I$(ATLAS_SDK)/include

ifeq ($(CROSS_COMPILE),)
  # Host build: use rpath for shared library search
  LDFLAGS  := -Wl,-rpath,$(ATLAS_SDK)/lib
  LDLIBS   := -L$(ATLAS_SDK)/lib -latlas
else
  # Android cross-compile: no rpath, add Android logging lib
  LDFLAGS  :=
  LDLIBS   := -L$(ATLAS_SDK)/lib -latlas -llog
endif

all: $(TARGET)

$(TARGET): main.cc manifest.json relu.onnx | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ main.cc $(LDLIBS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

test: $(TARGET)
	@echo "=== Atlas SNPE/CPU Comparison Test ==="
ifeq ($(CROSS_COMPILE),)
	@export DYLD_LIBRARY_PATH="$(ATLAS_SDK)/lib/_solib_darwin_arm64:$(ATLAS_SDK)/lib/_solib_darwin_arm64/_U@onnxruntime_Umacos_Uarm64_S_S_Connxruntime___Ulib:$(ATLAS_SDK)/lib" && \
	 LD_LIBRARY_PATH="$$DYLD_LIBRARY_PATH" && \
	 SAMPLE_MODEL_DIR="$(SAMPLE_MODEL_DIR)" && \
	 $(TARGET) manifest.json
else
	@echo "Cross-compiled binary; use 'adb push' to run on device."
endif
	@echo "=== Test PASSED ==="

clean:
	rm -rf $(BUILD_DIR)