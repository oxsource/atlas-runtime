# Makefile for testing Atlas SDK integration with dynamic linking.
#
# Requires ATLAS_SDK pointing to a directory produced by build_release.sh.
#
# Usage:
#   export ATLAS_SDK=$(pwd)/atlas-sdk/host
#   make          # build
#   make test     # run
#   make clean    # clean

.PHONY: all test clean

BUILD_DIR := build
TARGET    := $(BUILD_DIR)/atlas_test
ATLAS_SDK ?= $(error ATLAS_SDK must be set to the SDK directory)

UNAME_S := $(shell uname -s)
LIB_EXT := $(if $(filter Darwin,$(UNAME_S)),dylib,so)

CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -I$(ATLAS_SDK)/include
LDFLAGS  := -Wl,-rpath,$(ATLAS_SDK)/lib
LDLIBS   := -L$(ATLAS_SDK)/lib -latlas

all: $(TARGET)

$(TARGET): main.cc manifest.json identity_1x3x4x4.onnx | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ main.cc $(LDLIBS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

test: $(TARGET)
	@echo "=== Running Atlas integration test ==="
	@export DYLD_LIBRARY_PATH="$(ATLAS_SDK)/lib/_solib_darwin_arm64:$(ATLAS_SDK)/lib/_solib_darwin_arm64/_U@onnxruntime_Umacos_Uarm64_S_S_Connxruntime___Ulib:$(ATLAS_SDK)/lib" && \
	 LD_LIBRARY_PATH="$$DYLD_LIBRARY_PATH" && \
	 $(TARGET) manifest.json identity_1x3x4x4.onnx
	@echo "=== Test PASSED ==="

clean:
	rm -rf $(BUILD_DIR)