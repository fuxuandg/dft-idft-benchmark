# IDFT/DFT 模块 benchmark

第一次收到压缩包，请先读根目录的[压缩包说明](压缩包说明.txt)，了解平台要求、解压方式和使用入口。

本工程在K3 Linux上编译运行；Windows可用于阅读、编辑和分析CSV。
v1/v2/v3是可替换的手写RVV IDFT64卡带，oai是使用包内标准SIMDe的OAI基线。

## 从这里开始

| 你想做什么 | 打开哪份文档 |
|---|---|
| 理解make命令、全部参数、输出结果，照着运行或接入v4 | **[使用教程](docs/user-guide.md)** |
| 修改代码接口、适配器、构建、精度规则、数据格式或新点数 | **[技术细节与维护手册](docs/technical-reference.md)** |

第一次使用可在工程根目录执行：

```bash
make help
make check IMPL=v2
```

第二条是“选择v2，编译必要文件，运行固定golden正确性检查”，不测性能。
详细步骤、参考输出和后续计时请读使用教程第1～4节。

## 文档和结果位置

- 当前维护的两份主文档在`docs/`根目录。
- 算法版本差异已合并进技术文档第14节；旧历史说明单独归档在发行包之外。
- 原整理验收记录在`data/validation_20261014/`。
- 2026-10-15教程命令和v4演练证据在`data/documentation_20261015/`。
- 日常性能结果默认追加到`data/current/results.csv`。
- 默认终端输出分区、对齐，交互终端自动高亮；`OUTPUT=raw`恢复旧文本，`COLOR=never`关闭颜色，`V=1`显示完整编译命令。详见使用教程第13节。
- ZIP旁的`.zip.sha256`校验整个压缩包；包内`SHA256SUMS`校验解压后的文件。两者用途不同，发行时ZIP与sidecar应并排提供。

原module.md、tests.md和inventory-and-delivery.md已合并进两份主文档。
现有算法、接口和数值判据保持原样；`make historical`只列历史测量数据。
新点数golden暂不生成。发行包中的`docs/`只保留上面两份主文档。
