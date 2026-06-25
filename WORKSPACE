workspace(name = "atlas")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# ---------------------------------------------------------------------------
# nlohmann/json  v3.11.3  (header-only)
# To obtain sha256:
#   curl -L -o json.tar.gz https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
#   shasum -a 256 json.tar.gz
# ---------------------------------------------------------------------------
http_archive(
    name = "nlohmann_json",
    url = "https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz",
    sha256 = "0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406",
    strip_prefix = "json-3.11.3",
    build_file = "//third_party:nlohmann_json.BUILD",
)

# ---------------------------------------------------------------------------
# GoogleTest  v1.12.1  (last release without mandatory Abseil dependency)
# To obtain sha256:
#   curl -L -o gtest.tar.gz https://github.com/google/googletest/archive/refs/tags/release-1.12.1.tar.gz
#   shasum -a 256 gtest.tar.gz
# ---------------------------------------------------------------------------
http_archive(
    name = "googletest",
    url = "https://github.com/google/googletest/archive/refs/tags/release-1.12.1.tar.gz",
    sha256 = "81964fe578e9bd7c94dfdb09c8e4d6e6759e19967e397dbea48d1c10e45d0df2",
    strip_prefix = "googletest-release-1.12.1",
)
