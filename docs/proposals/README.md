# Proposals

本目录存放 Atlas 项目的 Feature 提议文档。

## 命名规则

```
NNN-short-kebab-title.md
```

- `NNN`：三位递增编号，从 `001` 开始，不回收、不重排
- `short-kebab-title`：英文小写短标题，连字符分隔

## 提议流程

详见 `docs/feature_spec.md`。

## 已有提议

| 编号 | 标题 | 状态 |
|------|------|------|
| [001](001-pipeline-manifest-config.md) | Manifest 自由配置 Pipeline | 已采纳 |
| [002](002-snpe-backend.md) | SNPE 后端接入 | 已采纳 |
| [003](003-platform-build-refinement.md) | 精细化平台构建方案 | 已采纳 |
| [004](004-android-platform-support.md) | Android 平台构建支持 | 已采纳 |
| [005](005-release-script-and-makefile-test.md) | 脚本编译输出与 Makefile 集成测试 | 已采纳 |
| [006](006-model-config-public-api.md) | ModelHandle 暴露 ModelConfig 关键字段 | 已采纳 |
| [007](007-third-party-dir-restructuring.md) | 三方库目录按类型分文件夹管理 | 已采纳 |
| [008](008-logger-implementation.md) | 日志输出管理系统 | 已采纳 |
| [009](009-backend-version-interface.md) | IBackend 后端独立版本号接口 | 已采纳 |
| [010](010-locale-safe-string-parse-utils.md) | 本地化安全的字符串解析工具 | 已采纳 |
| [011](011-snpe-cpu-shared-library-comparison-test.md) | SNPE CPU 共享库对比测试 | 已采纳 |
| [012](012-snpe-backend-inference-optimization.md) | SNPE 后端推理性能优化 | 已采纳 |
| [013](013-backend-profiling.md) | 后端性能分析装饰器 | 已采纳 |
| [014](014-backend-interface-symmetry-and-span.md) | IBackend 接口对称性与 Span 统一类型 | 已完成 |
