#pragma once

// Re-exports the internal PipelineNodeFactory and ATLAS_REGISTER_PIPELINE_NODE
// macro for external custom pipeline node registration.
//
// Usage:
//   #include "atlas/pipeline_registry.h"
//
//   ATLAS_REGISTER_PIPELINE_NODE("mycompany::my_node", MyNode)
//
#include "src/pipeline/pipeline_node_factory.h"
