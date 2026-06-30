#pragma once

#include <memory>
#include <string>

#include "atlas/atlas_export.h"
#include "atlas/model_handle.h"
#include "atlas/types.h"

namespace atlas {
namespace core { class ModelManager; class ManifestParser; }

namespace api {

// Top-level facade exposed to application code.
//
// Typical usage:
//
//   atlas::api::AtlasRuntime rt;
//   rt.Init("manifest.json");
//   auto handle = rt.GetModel("detector");
//   if (handle.IsValid()) {
//       std::vector<atlas::utils::Tensor> outputs;
//       handle.Run(image_tensor, &outputs);
//   }
//   rt.Release();
class ATLAS_API AtlasRuntime {
 public:
    AtlasRuntime();
    ~AtlasRuntime();

    // Parses |manifest_path|, creates shared backend contexts, and loads
    // all eager models.
    // @return kOk on success; kFileNotFound / kParseError / kBackendNotFound
    //         on failure.
    utils::ErrorCode Init(const std::string& manifest_path);

    // Returns a ModelHandle for |model_id|.
    // Returns an invalid handle (IsValid() == false) if the id is not found
    // or Init() has not been called.
    ModelHandle GetModel(const std::string& model_id);

    // Unloads all models and releases all resources.
    // Init() may be called again after Release().
    void Release();

    bool IsInitialized() const { return initialized_; }

 private:
    // PIMPL: internal parser and manager are hidden from the public header.
    std::unique_ptr<core::ManifestParser> parser_;
    std::unique_ptr<core::ModelManager>   manager_;
    bool                                  initialized_ = false;
};

}  // namespace api
}  // namespace atlas
