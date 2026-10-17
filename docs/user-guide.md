# Benchmark 使用教程

文档版次：2026-10-16。适用对象：本包中的 Makefile、tests/bench.c 和四张现有卡带。
先读本文即可完成使用；实现原理、函数定位、计算公式和工程限制见
[技术细节](technical-reference.md)。本文所有命令均以**工程根目录**为当前目录。

2026-10-16 更新：默认输出已分区、对齐；新增输出参数及发行校验说明见第13节。

## 目录

1. [先理解一条命令](#1-先理解一条命令)
2. [在哪里运行及第一次上手](#2-在哪里运行及第一次上手)
3. [四张卡带如何选择](#3-四张卡带如何选择)
4. [日常命令逐条讲解](#4-日常命令逐条讲解)
5. [全部命令速查](#5-全部命令速查)
6. [全部 Make 参数及生效范围](#6-全部-make-参数及生效范围)
7. [如何读输出与保存结果](#7-如何读输出与保存结果)
8. [一套可照着做的优化对比流程](#8-一套可照着做的优化对比流程)
9. [如何接入新卡带：v4 演练](#9-如何接入新卡带v4-演练)
10. [常见问题及错误输出](#10-常见问题及错误输出)
11. [程序命令行参数及高级入口](#11-程序命令行参数及高级入口)
12. [旧测试、历史数据和完整性检查](#12-旧测试历史数据和完整性检查)
13. [新版终端输出与发行包校验](#13-新版终端输出与发行包校验)

## 1. 先理解一条命令

```bash
make check IMPL=v2
```

逐段拆开：

| 文字 | 含义 |
|---|---|
| `make` | 使用当前目录 Makefile 中的规则，决定如何编译和运行程序 |
| `check` | 目标名称；这里表示运行**固定参考数据的正确性检查** |
| `IMPL=v2` | 给 Makefile 的变量赋值，选择第二版算法实现；IMPL 是 implementation 的缩写 |

这条命令依次做：

1. 查找 v2 对应的源文件、适配器和已编译对象。
2. 第一次运行时编译；之后只重编译需要更新的部分。
3. 打开 `reference/x86_golden.txt`。
4. 取出已有 IDFT64 用例的固定输入，运行 v2。
5. 将结果与文件中的固定输出比较，逐例打印 PASS 或 FAIL。

它**不测量性能，也不追加性能 CSV**。14 个 IDFT 用例全部输出相同才算 v2 通过。
`make bench IMPL=v2` 才是性能测量；`make v2` 则先执行 check，通过后再执行 bench。

`check` 是目标，`IMPL` 是变量。不要写成 `make check --IMPL v2`、`make IMPL v2` 或
把它当成 Git 分支命令。`make check IMPL=v1` 不会 checkout 或覆盖 v2 源码。

## 2. 在哪里运行及第一次上手

### 2.1 平台

- Windows：解压、阅读 Markdown、编辑源码、用 Excel 查看 CSV。
- K3 Linux：编译和运行。v1/v2/v3 要求 RISC-V RVV，当前适配器要求 VLEN=256bit。
- 其他普通 x86 Windows 电脑无法直接运行这些 RVV 内核；本工程也不是网页应用。

包内有 OAI/SIMDe 源码依赖，不需要联网下载它们。目标机仍需提供 GNU make、gcc、
Linux/glibc 开发环境、bash、sha256sum；指定 clang 或 gcc-16 时需事先安装对应编译器。

### 2.2 连接当前 K3

在 Windows PowerShell 中执行：

```powershell
& "C:\Users\wd\Documents\Codex\2026-09-14\new-chat\outputs\connect-k3.ps1"
```

这是当前电脑专用的连接脚本，会刷新板子地址、绑定 WLAN 并校验主机密钥。
另一台电脑需要自己的 SSH 配置，不能假定也有上述路径。

看到远端 Linux 提示符后执行：

```bash
cd ~/idft_benchmark_release_20261014
pwd
ls
make help
```

应能看到 `Makefile`、`README.md`、`modules`、`tests`、`reference` 等。
如果你把包解压在另一个目录，只修改 `cd` 后的路径；下面的命令不依赖目录名。
无参数的 `make` 与 `make help` 相同，只显示帮助，不执行测试。

帮助输出节选：

```text
make v1 | v2 | v3 | oai : correctness gate then benchmark
make check-all         : golden, random, double, ops, pair differential, legacy checks
make bench IMPL=v2 CC=clang CPU=2 ITERS=100000 REPS=7
make matrix            : repeat checks+timing for gcc/gcc-16/clang
```

### 2.3 第一次建议依次运行

```bash
make check IMPL=v2
make math IMPL=v2
make bench IMPL=v2 ITERS=1000 REPS=3 CSV=data/current/first-run.csv
```

第一条检查固定答案，第二条用 double 数学参考独立核对，第三条做一次短测并保存结果。
`ITERS=1000 REPS=3` 适合确认操作，正式比较恢复 `ITERS=100000 REPS=7`。
首次从纯源码编译 OAI 可能需要数分钟，默认隐藏长编译命令（`V=1` 可显示）；不要误以为程序
死机。已有构建缓存时，这几条检查很快。初次使用无须立即执行 check-all 或 matrix。

## 3. 四张卡带如何选择

“卡带”就是一份可独立选择的被测实现，共用接口、输入、检查和计时方法。

| IMPL 值 | 实现 | 支持的手写场景 | 结果要求 |
|---|---|---|---|
| `v1` | 第一版手写 RVV | IDFT64 | 对旧定点结果逐位一致 |
| `v2` | 第二轮手写 RVV 优化 | IDFT64 | 对旧定点结果逐位一致 |
| `v3` | 原生 SoA/radix-4 DIF 重写 | IDFT64 | 正常输入容差检查，饱和压力单独报告 |
| `oai` | 原始 OAI + 包内标准 SIMDe | DFT/IDFT，多种点数 | N64 有独立 golden；其他点数目前只有扩展/烟测入口 |

默认选择 `IMPL=v2`。方向默认 `TRANSFORM=idft`，点数默认 `SIZE=64`。
v1/v2/v3 不提供 DFT 或 1024 点内核，不能只改参数就把算法变成另一种变换。

快捷方式 `make v1`、`make v2`、`make v3`、`make oai` 都是“先 check，再 bench”。
这些命令会重新运行历史实现，不会恢复过去某个日期的环境，也不会重新生成历史 CSV。
不要在 `make v1` 后再写矛盾的 `IMPL=v3`；选择目标对应的卡带即可。

## 4. 日常命令逐条讲解

以下第4节等原有输出示例是旧版 raw 实际日志节选；新版默认使用第13节的整洁格式，
可加 `OUTPUT=raw` 对照旧例。示例省略了长编译命令和部分环境行。时间是参考测量值，随
CPU/负载变化；PASS 数量、参数含义和输出结构才是操作时应该核对的内容。

### 4.1 `make build IMPL=v2`：只编译

```bash
make build IMPL=v2
```

生成可执行文件，不运行正确性检查，不计时，不写结果 CSV。
产物位于 `build/<构建身份>/bench-v2`。哈希目录名与编译器版本、参数有关，不要
将教程中的某个目录名硬写进脚本。缓存有效时可能看到：

```text
make: Nothing to be done for 'build'.
```

这只表示无需重编译，不能据此说算法已经验证通过。

### 4.2 `make check IMPL=v2`：固定 golden 正确性检查

```bash
make check IMPL=v2
```

参考输出：

```text
module=v2 contract=AVX2-fixed-point-bit-exact
transform=idft points=64 scale=1 cpu=2 alignment=64 seed=1 signal=sparse mag=364
vlenb=32
PASS golden fn=idft64 signal=sparse mag=364 scale=1 different=0 max_abs=0 rmse=0.000000 nrmse_pct=0.000000
...
golden summary: checked=14 pass=14 fail=0 report=0 malformed=0
```

`different=0` 表示所有输出元素相同。summary 中 `fail=0`、`malformed=0`，且检查
数量符合预期，才是有效通过。`vlenb=32` 表示向量寄存器为32字节，即256bit。

本目标按文件内各个 CASE 的输入和 scale 执行，会同时覆盖 scale=0/1 的已有用例。
因此 `make check ... MAG=1000 SCALE=0 SEED=2` **不会把 golden 用例改成这些输入**；
环境行可能打印传入参数，但实际逐例信息以 `PASS golden ...` 行为准。
想变更随机输入请使用 quality，想测另一种数据请使用 bench。

### 4.3 `make check IMPL=v3`：重写版的容差检查

```bash
make check IMPL=v3
```

参考输出：

```text
PASS golden fn=idft64 signal=sparse mag=364 scale=1 different=22 max_abs=1 rmse=0.414578 nrmse_pct=0.154026
...
REPORT golden fn=idft64 signal=random mag=32767 scale=1 different=67 max_abs=8125 rmse=1952.964145 nrmse_pct=61.261889
...
golden summary: checked=14 pass=10 fail=0 report=4 malformed=0
```

这里的 PASS 允许规定范围内的差异，不能要求 `different=0`。REPORT 表示这类压力
输入用于观察饱和语义差异，不作为正常精度验收，也不计入 pass。
这个版本的 `10 pass + 4 report` 是已记录的预期构成。
若出现 FAIL，应先排查算法，不要修改 golden 或临时扩大容差让测试通过。

### 4.4 `make quality`：大量随机输入与原实现做差分

```bash
make quality IMPL=v3 CASES=16
```

用于先熟悉输出的小规模检查。正式验证可用默认2000，或：

```bash
make quality IMPL=v3 CASES=20000 SEED=1
```

每档幅度生成 `CASES` 个输入，共扫描364、2048、4095、32767四档，所以 CASES=2000
表示8000次被测输入，不是总共2000次。方向/点数由选定卡带决定。
输出格式如下，其中数值取决于 CASES 和 SEED：

```text
PASS random mag=364 cases=2000 lanes=256000 different=57148 max_abs=2 rmse=0.476675 nrmse_pct=0.226567
PASS random mag=2048 cases=2000 lanes=256000 different=57503 max_abs=2 rmse=0.478421 nrmse_pct=0.040483
REPORT random mag=4095 cases=2000 lanes=256000 different=53051 max_abs=8022 rmse=72.504390 nrmse_pct=3.318443
REPORT random mag=32767 cases=2000 lanes=256000 different=137166 max_abs=8191 rmse=1550.821110 nrmse_pct=50.737980
```

v1/v2 应在四档幅度均与参考逐位一致。v3 的前两档要求有界误差，后两档为压力报告。
quality 固定使用四档随机输入，`MAG` 和 `SIGNAL` 不会改变它；ITERS/REPS也不生效。
结果打印到终端，默认不会写性能 CSV。需要保存时：

```bash
mkdir -p data/current
make quality IMPL=v3 CASES=2000 > data/current/quality-v3.txt 2>&1
```

`>` 将覆盖同名日志；想保留多次结果请使用不同文件名。

### 4.5 `make math`：独立 double 数学参考

```bash
make math IMPL=v2
```

使用8组、幅度364的随机输入，对照直接计算的 double DFT/IDFT，要求 RMS EVM<1%。
参考输出：

```text
PASS independent-double trials=8 mag=364 evm_rms_pct=0.300543 max_abs=2.221416 criterion=1_percent
```

它检查变换方向、输出顺序和数学增益是否合理，与 bit-exact 检查互补。
当前只建立64点的数学增益约定。CASES、MAG、SIGNAL、ITERS、REPS不控制本目标，
试验数8和幅度364固定；SEED、SCALE、TRANSFORM会影响它。

### 4.6 `make bench`：测模块入口延迟

```bash
make bench IMPL=v2 ITERS=1000 REPS=3 CSV=data/current/first-run.csv
```

2026-10-15短测输出：

```text
round=1 ns_per_call=393.833000
round=2 ns_per_call=393.708000
round=3 ns_per_call=399.916000
RESULT module=v2 min_ns=393.708000 median_ns=393.833000 max_ns=399.916000 input_hash=1a0ba47ebf7cd64b
```

每轮包住1000次调用；3轮合计3000次正式调用，前面另有10000次warmup。
`ns_per_call` 是该轮平均每次模块调用的纳秒数，不是单次调用的瞬时时间。
`median_ns` 是3轮平均延迟的中位数，`min_ns/max_ns` 描述轮间波动。

正式测量建议：

```bash
make check IMPL=v2 CC=clang
make bench IMPL=v2 CC=clang CPU=2 ITERS=100000 REPS=7 \
  SIGNAL=sparse MAG=364 SEED=1 SCALE=1 CSV=data/current/v2-clang.csv
```

`bench` 本身不会先做正确性门禁；上面的check必须由你执行，或直接用`make v2`。
一次计时使用固定的一份输入，不是每次调用前重新生成随机输入。
不要将不正确实现的更短时间当成有效优化。

### 4.7 `make v2`：检查和计时的快捷组合

```bash
make v2 CC=clang ITERS=100000 REPS=7 CSV=data/current/v2-clang.csv
```

等价于先`make check IMPL=v2 ...`，成功后再`make bench IMPL=v2 ...`。
如果check失败，后续bench不会执行。输出先有golden summary，再有round和RESULT。
v1/v3/oai快捷目标的行为相同，v3采用自己的精度规则。

### 4.8 `make check-all`：运行保留的验证集合

```bash
make check-all
```

它包含四张卡带的golden、随机差分、独立double，算子测试、v1/v2的400048组差分、
独立自检、旧向量测试、错误输入检查、旧26例golden、旧EVM和binary golden。
最后再检查OAI的DFT64。它不做性能矩阵，也不写性能结果CSV。

末尾/过程中的参考摘要：

```text
PASS: 400048 cases x 128 lanes, scales={0,1,2,255}, aligned+unaligned
correctness: 26 cases, 26 pass, 0 fail, 0 report
correctness (tolerant: ...): 26 cases, 22 pass, 0 fail, 4 report
TIER1 PASS (bit-exact): reference/x86_idft64_scale1_seed1.bin
Tier 2: PASS (both transforms match the double reference within 1% EVM)
```

这里的`...`是省略标记，不是逐字输出。正常退出且没有中途make错误，才表示集合完成。
CASES可以控制其中的统一quality测试；differential仍固定400048组。
该目标的范围固定为现有卡带和N64；不要给它传TRANSFORM=dft或SIZE=1024。
新添加v4不会自动加入check-all，维护方法见技术文档。

### 4.9 `make matrix`：比较三个编译器

```bash
make matrix ITERS=100000 REPS=7 CSV=data/current/compiler-comparison.csv
```

按gcc、gcc-16、clang顺序，对v1/v2/v3分别先检查、后计时，共9个组合。
缺少某编译器时打印`SKIP unavailable clang`等信息，不代表该组合已经通过。
该目标自行指定CC；不要用`make matrix CC=clang`理解为只测Clang。
OAI依赖侧默认继续由gcc编译。首次缺少缓存时构建量会增加，先用单个卡带熟悉流程。

### 4.10 查看历史材料与检查包

```bash
make historical
make package-check
```

前者只列出`data/historical`中的测量文件名，不测量、不显示CSV表格，也不
重新产生过去的成绩。后者逐个检查静态文件哈希，正常输出形如：

```text
reference/x86_golden.txt: OK
tests/bench.c: OK
docs/user-guide.md: OK
```

这些是输出形式示例，行顺序由清单决定。`data/current`和`build`不在静态清单内。
你主动修改源码或文档后出现checksum FAILED是预期的完整性变化，不能通过修改
golden来解决。新版本打包时才重新建立自己的清单。

## 5. 全部命令速查

| 命令 | 作用 | 是否运行模块/计时 | 正常观察点 |
|---|---|---|---|
| `make` / `make help` | 打印短帮助 | 否/否 | 目标说明文字 |
| `make build IMPL=v2` | 仅构建所选模块程序 | 否/否 | 编译命令或Nothing to be done |
| `make check IMPL=v2` | 固定text golden | 是/否 | 14 pass，0 fail |
| `make quality IMPL=v2` | 四幅度随机差分 | 是/否 | 每档PASS或规定的REPORT |
| `make math IMPL=v2` | 独立double数学参考 | 是/否 | independent-double PASS |
| `make bench IMPL=v2` | warmup+批内计时 | 是/是 | round、RESULT、追加CSV |
| `make v1/v2/v3/oai` | 选择相应快捷目标；实际使用`make v2`等 | 是/是 | check成功后RESULT |
| `make check-all` | 现有验证集合 | 是/否 | 所有适用门禁成功，过程无make错误 |
| `make matrix` | 9组合编译器检查和计时 | 是/是 | 每个已安装组合有检查和RESULT |
| `make ops` | RVV算子对照标准SIMDe | 是/否 | 全部算子通过 |
| `make differential` | 固定v1/v2全范围差分 | 是/否 | 400048 cases PASS |
| `make standalone-check` | 固定v1/v2各3组内置golden | 是/否 | 两份3/3 |
| `make legacy-vector-check` | 固定v1/v2早期向量测试 | 是/否 | 逐位完全一致 |
| `make negative-check` | 空参考、非法组合等负向测试 | 是/否 | 每个negative项PASS |
| `make legacy IMPL=v2` | 只构建旧harness | 否/否 | legacy-v2二进制 |
| `make legacy-check IMPL=v2` | 旧完整26例text golden | 是/否 | 26 pass；v3为22 pass+4 report |
| `make legacy-tier1 IMPL=v2` | 固定IDFT binary golden | 是/否 | input/output均0差异 |
| `make legacy-tier2 IMPL=v2` | 旧double EVM振幅扫描 | 是/否 | 两个方向均有<1%结果 |
| `make historical` | 列历史测量数据文件 | 否/否 | data/historical下文件名列表 |
| `make package-check` | 静态哈希完整性 | 否/否 | 成功短汇总；失败保留具体错误 |

`make clean`、`make run`、`make test`、`make golden`、`make v4`不是当前交付版预设的
公共目标。不要从旧报告直接复制这些名称；查本文对应目标。编译目标没有自动
创建golden的功能，固定答案必须有独立来源。

## 6. 全部 Make 参数及生效范围

格式为`make <目标> 名称=值`，名称区分大小写。未填写时使用默认值。

| 参数 | 默认值 | 可选值/范围 | 生效范围与注意事项 |
|---|---|---|---|
| `IMPL` | `v2` | `v1/v2/v3/oai`，或已注册新卡带 | 单模块目标；固定组合目标自行选择卡带 |
| `TRANSFORM` | `idft` | `idft`、`dft`，小写 | 单模块通用目标；RVV三版只支持idft |
| `SIZE` | `64` | 正整数，上限98304，还必须被卡带支持 | 动态缓冲使用2*SIZE；check/math仅有N64规则 |
| `CPU` | `2` | 有效且允许使用的CPU编号；`-1`不锁核 | 通用入口和PIN包装；正式测量建议锁核 |
| `SEED` | `1` | 1～4294967295 | bench/quality/math输入；固定golden输入不随它改变 |
| `SCALE` | `1` | 0～255 | bench/quality/math；现有IDFT64中非零都表示末端>>3 |
| `ITERS` | `100000` | 大于0的整数，程序按uint64读取 | bench每轮调用数；不控制quality或check |
| `REPS` | `7` | 1～10000 | bench轮数 |
| `CASES` | `2000` | 1～1000000 | quality每档幅度输入数；check-all里的quality亦使用它 |
| `MAG` | `364` | 0～32767 | bench输入幅度；MAG=0虽可运行但不适合验证正常信号 |
| `SIGNAL` | `sparse` | `sparse/random/impulse/dc/tone` | 仅bench选择输入类型 |
| `CSV` | `data/current/results.csv` | 可写路径 | bench追加；父目录由make创建，不清空已有文件 |
| `OUTPUT` | `pretty` | pretty、raw | 主runner排版或旧文本格式，见第13节 |
| `COLOR` | `auto` | auto、always、never | 主runner颜色控制，不改CSV |
| `V` | `0` | 0、1 | 1显示完整Make编译/运行命令 |
| `CC` | `gcc` | 已安装gcc、gcc-16、clang等兼容编译器 | 编译候选RVV、统一runner等；matrix自行轮换它 |
| `OAI_CC` | `gcc` | 能编译上游OAI的编译器 | 构建/链接OAI参考；不建议改为clang，见技术文档 |
| `CFLAGS` | `-O2 -std=gnu99` | 完整C编译参数字符串 | 同时影响候选和OAI；含空格时用引号包住 |
| `ARCHFLAGS` | `-march=rv64gcv` | 目标架构参数 | 当前运行目标仍要求RISC-V及相应RVV能力 |

例如：

```bash
make bench IMPL=v2 CC=clang CFLAGS='-O3 -std=gnu99' \
  SIGNAL=random MAG=1024 SEED=2 ITERS=100000 REPS=9 \
  CSV=data/current/v2-clang-O3-random.csv
```

CFLAGS是**替换整串默认值**，所以例子同时保留`-std=gnu99`。
多个实现比较时保持CPU、方向、点数、scale、种子、信号、幅度、ITERS/REPS一致。
不要把不同input_hash或不同编译参数的记录直接视为同一条件。

输入类型：sparse为两端非零的稀疏信号；random为指定范围内的确定性随机；impulse
只让第一个实部非零；dc让所有实虚部分量恒定；tone为固定第5频点的复指数信号。
高幅DC/tone容易触发饱和，含义与相同MAG的随机输入不同。

### 6.1 哪些参数不会生效：快速对照

| 目标 | 实际影响输入/试验规模的参数 | 不要用来控制它的参数 |
|---|---|---|
| check | IMPL、TRANSFORM、SIZE=64、CPU、编译参数 | MAG、SIGNAL、SEED、SCALE、ITERS、REPS、CASES、CSV不改用例/不写CSV |
| quality | IMPL、方向/点数、CPU、SEED、SCALE、CASES | MAG、SIGNAL、ITERS、REPS、CSV |
| math | IMPL、方向、SIZE=64、CPU、SEED、SCALE | MAG、SIGNAL、CASES、ITERS、REPS、CSV |
| bench | 所有通用测量参数 | CASES |
| ops/differential/standalone/vector | CPU、CC/编译参数 | IMPL、SIZE、CASES等不会改变固定测试集合 |
| legacy-* | IMPL、CPU、编译参数 | Makefile不转发通用ARGS，SIZE/SEED/SCALE等不能按统一入口解释 |

## 7. 如何读输出与保存结果

### 7.1 正确性字段

| 字段 | 含义 |
|---|---|
| `different` | 与参考不相同的int16分量数，不是复数点数 |
| `max_abs` | 最大绝对误差；对整数输出可按LSB理解 |
| `rmse` | 均方根误差，单位与输出分量一致 |
| `nrmse_pct` | 误差能量相对参考能量的百分比开方指标，公式见技术文档 |
| `evm_rms_pct` | 相对独立double数学参考的RMS EVM百分数 |
| `malformed` | 参考文件格式、完整性有问题；应为0 |

PASS只表示该门禁通过。REPORT不能当成正常精度通过；程序退出0也可以同时有
REPORT，这是数值契约明确允许的压力观察。

### 7.2 性能字段

`min_ns/median_ns/max_ns`来自各轮平均值。正式比较优先看median，同时检查波动。
本工程测量模块入口，含统一函数调用及模块适配开销，不扣空循环开销。
`input_hash`用于核对输入一致性，不是密码学文件SHA256。
环境行包括实际选定CPU频率、编译器、编译参数、VLEN；ns不是硬件cycle数。

CSV第一行：

```text
unix_time,module,compiler,flags,transform,points,scale,cpu,seed,signal,mag,iters,reps,input_hash,min_ns,median_ns,max_ns
```

`unix_time`为Unix秒数；每次bench追加一行，REPS=7也只写一行汇总。
各轮细节只在终端；需要保留各轮时：

```bash
make bench IMPL=v2 CSV=data/current/run-a.csv > data/current/run-a.txt 2>&1
```

默认目录已存在。改用其他日志目录时先`mkdir -p`，因为shell在执行make之前就会
打开`>`右侧文件。不要把重定向后的“终端没输出”误认为没有运行。

### 7.3 一个可核对的历史基准

2026-10-14同场测量、sparse/MAG364/SEED1/SCALE1、CPU2、100000×7：

| 卡带 | GCC15 | GCC16 | Clang21 |
|---|---:|---:|---:|
| v1 | 617.621ns | 604.789ns | 584.173ns |
| v2 | 394.500ns | 381.690ns | 349.273ns |
| v3 | 236.698ns | 236.585ns | 247.284ns |

这是参考量级，不是每次运行必须达到的数值。原始证据保存在
`data/validation_20261014/`。当前新运行写`data/current/`，旧历史为`data/historical/`。

## 8. 一套可照着做的优化对比流程

```bash
# 1. 先确认原基准在本机有效
make check IMPL=v2 CC=gcc
make quality IMPL=v2 CC=gcc CASES=2000
make math IMPL=v2 CC=gcc

# 2. 固定条件，保存基准
make bench IMPL=v2 CC=gcc ITERS=100000 REPS=7 \
  SIGNAL=sparse MAG=364 SEED=1 SCALE=1 CSV=data/current/baseline.csv

# 3. 接入新卡带v4（下一节），先查正确性
make check IMPL=v4 CC=gcc
make quality IMPL=v4 CC=gcc CASES=2000
make math IMPL=v4 CC=gcc

# 4. 相同条件测候选
make bench IMPL=v4 CC=gcc ITERS=100000 REPS=7 \
  SIGNAL=sparse MAG=364 SEED=1 SCALE=1 CSV=data/current/candidate.csv
```

先比较同一编译器下的算法，再测试换编译器是否有额外收益。更短的代码或更少的
指令不一定更快，需要实际测量。改变了饱和/舍入规则时，应先建立适用的精度契约，
不能只把APPROX标记改为1就宣称可以替代生产模块。

## 9. 如何接入新卡带：v4 演练

### 9.0 先明确卡带的交付形式与边界

本工程中的“卡带”是**可编译的算法源码模块**，不是压缩包、独立可执行程序，
也不要求先制作成动态库。卡带负责“一次变换”，benchmark负责生成输入、读取
参考答案、检查正确性、计时和保存结果。替换算法时应保持测试方法与计时边界稳定。

一张卡带在逻辑上包含三部分，并不要求全部文件都放进自己的子目录：

| 部分 | 必需内容 | 当前存放方式 |
|---|---|---|
| 算法内核 | 执行一次完整IDFT/DFT的源码及所需私有头文件 | `modules/v4/`等独立目录 |
| 适配器 | 接入统一接口，明确支持的点数、方向和硬件条件 | 可以共用`modules/rvv_adapter.c`，也可提供自定义适配器 |
| 注册信息 | 源文件、入口符号、适配器、点数、方向、精度契约 | `modules/catalog.mk` |

**仅把一个.c文件放进modules不会自动注册卡带。** 对新的IDFT64，推荐最小布局：

```text
modules/
├── catalog.mk
├── rvv_adapter.c
└── v4/
    ├── idft64_rvv.c       必需：内核源码
    ├── idft64_rvv.h       推荐：入口声明；也可将声明保留在适配器中
    └── rvv_ops.h          可选：内部辅助函数，不需要就不建
```

当前普通RVV构建模板对应**一个内核.c文件加若干头文件**。目录名和文件名不是
固定标准，只要注册路径正确即可；入口函数名也可改变，但参数签名须符合适配器。

#### 输入输出协议

复用当前通用RVV适配器时，内核需提供以下形式的函数（不是main）：

```c
#include <stdint.h>

void idft64_rvv(int16_t *input, int16_t *output, unsigned char scale);
```

| 项 | 当前约定 |
|---|---|
| 数据类型 | 有符号16位整数`int16_t`，复数实部/虚部交错 |
| 输入排列 | `[Re(x0), Im(x0), Re(x1), Im(x1), ...]` |
| 输出排列 | `[Re(y0), Im(y0), Re(y1), Im(y1), ...]` |
| 缓冲长度 | N点变换，输入和输出各`2*N`个int16，即各`4*N`字节；N64时各256字节 |
| 缓冲关系 | 当前主runner提供分离的输入、输出缓冲，分别64字节对齐；不承诺原地调用 |
| 输入使用 | 应保持输入内容不变，支持对同一输入反复调用；旧ABI未加const不代表可任意覆写 |
| 返回条件 | 同步完成一次变换，返回前写完全部有效输出；不依赖上次调用残留数据 |
| scale | 遵循被替换实现的定点缩放约定，不能作为任意倍数解释 |
| 通用适配器硬件条件 | 当前要求RVV、`vlenb=32`即VLEN=256bit；它不会自动适配其他VLEN |

本节v4复制的是IDFT64：scale=0不执行末端右移3位，非零执行该缩放。
这一条不是未来所有点数、DFT和自定义卡带的通用缩放公式。改变归一化方式时，
须明确数值契约和参考增益；不能只放宽误差阈值来掩盖缩放不一致。

#### 内核能做什么，不能做什么？

内部可以使用SoA、不同的FFT分解、RVV寄存器布局、私有查表和辅助函数，不必保留
AVX2内部布局。但跨卡带的输入输出协议必须一致。每次调用必需的数据重排、
AoS/SoA转换和临时工作应属于run路径并计时，不能移到测试程序中隐藏成本。
真正可复用的一次性初始化可放setup，并在文档说明其成本及使用前提。

内核不应包含main、输入生成、参考文件读取、结果打印、CPU绑核、计时、统计、
CSV写入或不受控制的后台任务。这些由benchmark统一负责。尤其不要在run里打印
日志或读取测试答案，否则会干扰性能或破坏测试独立性。

接入前请确认：入口签名匹配、支持条件明确、不支持时失败、输入输出长度正确、
无越界访问、无测试框架职责混入、数值契约明确。再按下面的步骤注册和验证。

### 9.1 第一步：准备代码文件夹

本节已在K3的临时工程副本执行验证。演练先复制v2以证明接线正确，**此时v4与v2
是同一算法，不代表新的优化成果**。建议在自己的工程副本练习。

```bash
test ! -e modules/v4 && cp -a modules/v2 modules/v4
```

若v4已存在，上式不会覆盖它，请检查已有内容。
此时有`modules/v4/idft64_rvv.c`、`rvv_ops.h`和`idft64_rvv.h`。
现有内核的公开函数形式是：

```c
void idft64_rvv(int16_t *x, int16_t *y, unsigned char scale);
```

x/y分别是输入和输出，均是128个int16的交错复数缓冲。默认独立缓冲、64字节对齐。
scale=0不做末端>>3，scale非零做>>3。函数需要同步写完全部输出；不要包含main、
读取文件、打印日志、生成输入、计时或不受控制的后台任务。

### 9.2 第二步：注册卡带

打开`modules/catalog.mk`，在文件末尾追加以下**完整配置**：

```make
CARTRIDGES += v4
SOURCE_v4 := modules/v4/idft64_rvv.c
ENTRY_v4 := idft64_rvv
SYMBOL_v4 := kernel_v4
ADAPTER_v4 := modules/rvv_adapter.c
POINTS_v4 := 64
INVERSE_v4 := 1
APPROX_v4 := 0
```

| 项 | 意义 |
|---|---|
| CARTRIDGES | 允许IMPL=v4，并生成其编译/链接规则 |
| SOURCE | 内核源文件路径，当前模板一张卡带对应一个主要.c文件 |
| ENTRY | 源文件里实际定义的函数名 |
| SYMBOL | 链接时私有名称；各卡带必须不同，避免符号冲突 |
| ADAPTER | 如何把内核接到统一模块接口 |
| POINTS | 此通用适配器允许的点数 |
| INVERSE | 1表示IDFT，0表示DFT |
| APPROX | 0采用exact；1采用当前内置的v3式容差策略，并非任意浮点/新算法策略 |

构建时`-Didft64_rvv=kernel_v4`负责改名，所以不需要为了避免冲突去修改所有函数调用。
不要让v4继续使用`SYMBOL_v4 := kernel_v2`。

### 9.3 第三步：检查与计时

```bash
make build IMPL=v4
make check IMPL=v4
make quality IMPL=v4 CASES=16
make math IMPL=v4
make bench IMPL=v4 ITERS=1000 REPS=3 CSV=data/current/v4-demo.csv
```

本次演练输出：

```text
module=v4 contract=AVX2-fixed-point-bit-exact
...
golden summary: checked=14 pass=14 fail=0 report=0 malformed=0
RESULT module=v4 min_ns=393.708000 median_ns=393.750000 max_ns=393.875000 input_hash=1a0ba47ebf7cd64b
```

接通之后才开始修改v4算法，保留v2做基准。每次修改后重新check，再做quality/math，
最后bench。若原函数名不是idft64_rvv，把ENTRY改为真实函数名，其参数签名仍须匹配。

### 9.4 可选：增加 `make v4` 快捷命令

仅在catalog里注册后，`make check IMPL=v4`可用，但`make v4`还没有目标。
如需快捷方式，在Makefile现有快捷目标处把：

```make
v1 v2 v3 oai:
```

改成：

```make
v1 v2 v3 v4 oai:
```

其下面两行带Tab的递归make规则保留，并将`.PHONY`列表加入v4。
若还希望check-all/matrix覆盖v4，需要修改它们各自的版本循环；注册不会自动扩大
这些固定验收集合。

### 9.5 什么时候需要自定义适配器

如果内核使用float、分开的实虚数组、不同VLEN、运行时多个点数或特有初始化，
应提供自定义适配器，不能假定通用rvv_adapter已支持。内核内部类型可以不同，
但接到当前runner时仍须通过适配器提供交错int16输入输出，计入必要转换成本；
若要把外部ABI也改为float，还需扩展runner和参考检查，而不只是改适配器。

统一模块描述符在`include/bench_module.h`定义：

```c
typedef struct {
    const char *name;
    const char *contract;
    int (*setup)(size_t points, int inverse);
    void (*run)(int16_t *input, int16_t *output, unsigned char scale);
    int approximate;
} bench_module;
```

自定义适配器应导出`const bench_module selected_module`，其中：

- setup在计时之外选择方向、点数并进行必要初始化；支持时返回0，不支持返回非零。
- run每次执行一次完整变换，是被测路径，不在内部自行计时。
- contract说明数值约定；approximate选择现有精度检查策略，不是任意误差的通行证。
- 不支持的方向或点数必须明确报错，不可偷偷回退到OAI或另一张卡带代跑。

`modules/oai_adapter.c`是多点数、多方向接入的现有例子。函数定位、构建规则和
参考增益的修改细节见技术文档第2、4、6～8节。

### 9.6 扩展到新点数、多源文件和自动测试集合时的限制

| 你要做的扩展 | 除注册外还要处理什么 |
|---|---|
| 一个新固定点数，入口签名不变 | 实现真正对应点数的内核，设置POINTS并确认适配器硬件限制适合 |
| 同一卡带运行时支持多个点数/方向 | 自定义setup/run和dispatch，不是只改一个POINTS变量 |
| 一张卡带拆成多个独立.c文件 | 扩展Makefile对象列表、依赖和链接规则；不能直接把多个路径塞进SOURCE |
| 依赖新库或新资源 | 补齐构建/链接、来源与许可证说明，保证发行包依赖闭包 |
| 增加`make v4`快捷目标 | 按9.4修改快捷目标和.PHONY |
| 希望check-all或matrix自动测v4 | 修改各自固定版本循环；catalog注册不会自动加入这些集合 |
| 新增512/1024/2048等点数的正确性验收 | 建立相应参考数据、参考增益及数值门槛；当前统一golden/math只确立N64契约 |

仅把POINTS改为1024不会产生1024点算法；编译成功或能跑bench也不等于新点数
已通过正确性验收。最小交付标准仍是“内核源码 + 适配器 + 注册信息”，扩展时
继续保持外部协议和测试尺度稳定，避免换卡带时连检查标准一起不透明地改变。

## 10. 常见问题及错误输出

### 10.1 选了不存在的卡带

```bash
make check IMPL=v9
```

典型输出：

```text
Makefile:...: *** Unsupported IMPL=v9; available: v1 v2 v3 oai.  Stop.
```

先检查拼写和catalog注册。这里的行号会随文档版本变化。

### 10.2 把v2当成DFT运行

```bash
make check IMPL=v2 TRANSFORM=dft
```

实测关键输出：

```text
unsupported cartridge=v2 transform=dft points=64
make: *** [Makefile:...: check] Error 2
```

需要DFT64时用`make check IMPL=oai TRANSFORM=dft`。要开发RVV DFT，应接入新卡带。

### 10.3 想检查1024点

```bash
make check IMPL=oai SIZE=1024
```

程序报告：

```text
golden suite currently supports N64 only; no new goldens generated
```

说明尚未提供这个点数的固定裁判。`make bench IMPL=oai SIZE=1024 ITERS=100`可以
调用现有算法，但不是正确性验收。不要把同一个OAI函数与自己做差分当成独立参考。

### 10.4 `sched_setaffinity: Invalid argument`

所选核心不存在或不在当前允许的CPU集合中。可先执行`taskset -pc $$`查看当前shell
允许的核心。调整CPU到合法编号；只为功能检查可用`CPU=-1`，正式性能测量应锁核。
权限或环境问题不会被当作正常测试通过。

### 10.5 修改代码后哈希检查失败

`package-check`用于核对发布快照，修改文件后应失败。完成自己版本的验证后重新
打包/生成清单，做法见技术文档。不要直接用重新生成golden或覆盖原始参考来消除失败。

### 10.6 两种退出码不要混淆

统一C程序：0=适用门禁通过/计时成功；1=数值FAIL；2=参数、文件或不支持等错误。
通过make运行时，make通常将子命令失败统一返回2，日志末尾的`Error 1/2`才是子进程
状态。想观察shell状态，在命令之后立即执行`echo $?`；不要隔了别的命令再查。

### 10.7 时间变慢、CSV里出现多行

CSV是追加，不是覆盖。检查两行的编译器、flags、input_hash、方向/点数和轮数是否
相同；再看CPU频率和负载。单次最好值不能替代多轮中位数。短测的三轮数值也不要
和正式七轮结果直接当作严格同场比较。

## 11. 程序命令行参数及高级入口

平时优先用Make目标。若要选择另一个golden文件，Makefile没有`GOLDEN=...`公共变量，
需使用实际二进制的`--golden`参数。不要随意选中旧构建目录里的程序。

在K3的bash中获取当前配置对应路径：

```bash
make build IMPL=v2 CC=gcc
BIN=$(make -s --eval='print-bin:;@echo $(BIN)' print-bin IMPL=v2 CC=gcc)
"$BIN" --help
"$BIN" --mode golden --cpu 2 --golden reference/x86_golden.txt
```

`--eval`只临时增加一个打印路径的make目标，不修改Makefile。BUILD的变化机制见
技术文档。直接运行时`--csv`的父目录需要自己建立，Make bench才会自动创建。

| 程序参数 | 默认值 | 可选值 | 说明 |
|---|---|---|---|
| `--mode` | bench | bench/golden/random/math | 分别对应make bench/check/quality/math |
| `--transform` | idft | idft/dft | 卡带必须支持 |
| `--size` | 64 | 正整数且模块支持 | 上限98304 |
| `--cpu` | 2 | 合法CPU或-1 | -1不锁核 |
| `--iters` | 100000 | >0 | 仅bench |
| `--reps` | 7 | 1～10000 | 仅bench |
| `--seed` | 1 | 1～4294967295 | 生成输入时使用 |
| `--scale` | 1 | 0～255 | golden按文件内scale |
| `--mag` | 364 | 0～32767 | 仅bench选择输入幅度 |
| `--signal` | sparse | sparse/random/dc/impulse/tone | 仅bench |
| `--cases` | 2000 | 1～1000000 | 仅random |
| `--golden` | reference/x86_golden.txt | 文件路径 | 仅golden；解析格式见技术文档 |
| `--csv` | 无 | 文件路径 | 仅bench；直接运行默认不写CSV |
| `--help` | 无 | 不接值 | 打印参数说明并退出 |

IMPL/CC/CFLAGS等是构建参数，没有对应的运行时`--impl`或`--cc`。换卡带要构建并
选择另一个二进制，不能通过bench-v2后加`--impl v3`切换。

## 12. 旧测试、历史数据和完整性检查

旧入口为保留已有验证资产而提供，建议先熟悉统一入口再使用。

| 命令 | 检查什么 | 参考输出节选/限制 |
|---|---|---|
| `make ops` | 24项RVV算子/组合对照标准SIMDe | `adds_epi16 (饱和加) OK`，末尾“全部算子通过” |
| `make differential` | v1与v2满范围、四种scale、对齐/非对齐 | `PASS: 400048 cases x 128 lanes, scales={0,1,2,255}, aligned+unaligned` |
| `make standalone-check` | v1/v2分别检查内置3例 | 两份`3/3 用例通过` |
| `make legacy-vector-check` | v1/v2稀疏、随机、冲激、DC、极值 | 末尾“逐位完全一致（手写 RVV 版正确）” |
| `make negative-check` | 测试错误应当报错 | `PASS negative --size 1024`表示成功拒绝错误组合，不表示实现了1024点 |
| `make legacy IMPL=v2` | 构建旧harness | 不运行、不计时 |
| `make legacy-check IMPL=v2` | 旧26例完整golden，含DFT和IDFT | `26 cases, 26 pass, 0 fail, 0 report` |
| `make legacy-check IMPL=v3` | 旧26例，IDFT按容差，DFT exact | `26 cases, 22 pass, 0 fail, 4 report` |
| `make legacy-tier1 IMPL=v2` | 512字节binary固定输入/输出 | `input : 0/128 differ`、`output : 0/128 differ`、`TIER1 PASS` |
| `make legacy-tier2 IMPL=v2` | 旧double EVM幅度扫描 | `Tier 2: PASS (both transforms match the double reference within 1% EVM)` |

legacy-tier1坚持bit-exact，因此**不要用v3跑完后期待通过**。check-all只对v1/v2/oai
执行它。legacy-tier2和新math的参考方式/聚合判据不同，两个百分数不能混为一个指标。
旧harness的很多参数没有通过Make包装转发，日常新测量使用统一入口。

发行包内`docs/`仅有本教程和技术文档；仍有用的算法解释、优化差异和实验结论已合并
到技术文档第14节。15份旧历史说明及其标题索引移到包外的
`idft_benchmark_history_docs_20261015.zip`，无需下载该归档也能使用和维护benchmark。
归档位置与哈希见`provenance/documentation-archive.txt`。归档中的旧命令、路径和
patched-SIMDe路线不覆盖本教程，当前没有运行修改SIMDe的B线卡带。

第一次读完本文后，建议先成功执行check、math、短bench，再做v4复制演练。需要修改
接口、测试判据、构建机制或新点数支持时，再查阅[技术细节](technical-reference.md)。

## 13. 新版终端输出与发行包校验

### 13.1 输出控制

主入口 `check`、`quality`、`math`、`bench` 和调用它们的卡带快捷命令默认按
“环境 → 测试表格/测量轮次 → 汇总”分区显示。颜色：PASS绿色、FAIL红色、
REPORT黄色、标题和性能中位数青色。REPORT仅为压力报告，不能当作通过。
历史 `legacy-*`、算子和双版本差分程序保留原有正文布局，不统一改变历史日志格式。

| 参数 | 默认 | 可选值与作用 |
|---|---|---|
| `OUTPUT` | `pretty` | `pretty` 分区对齐；`raw` 保留旧的 key=value 文本格式 |
| `COLOR` | `auto` | `auto` 仅交互终端使用颜色；`always` 强制；`never` 禁用 |
| `V` | `0` | `0` 隐藏Make执行命令；`1` 显示完整编译、链接和运行命令 |

```bash
make check IMPL=v2
make check IMPL=v3 COLOR=never
make check IMPL=v2 OUTPUT=raw
make build IMPL=v2 V=1
make check IMPL=v2 > check.log
```

最后一条自动去除颜色，但仍保留分区和对齐。设置环境变量 `NO_COLOR=1` 也会
禁止自动颜色；显式 `COLOR=always` 优先。`OUTPUT=raw` 始终无颜色。
非终端或 `TERM=dumb` 时自动颜色关闭，文字 PASS/FAIL/REPORT 仍明确可读。
要让 `less` 显示强制保留的颜色，可用 `COLOR=always` 并配合 `less -R`。

默认golden结果示意（省略环境和中间用例；颜色未在Markdown中显示）：

```text
== Golden correctness ==
  STATUS FN      SIGNAL      MAG SCALE   DIFF MAX ABS       RMSE    NRMSE %
  PASS   idft64  sparse      364     1      0       0   0.000000   0.000000
  ...

  [PASS] Checked: 14 | Pass: 14 | Fail: 0 | Report: 0 | Malformed: 0
```

性能结果会单独列出 Min、Median、Max，单位为 `ns / call`，并打印CSV追加路径。
本次只改变呈现，不改变测量区间、数值门槛、退出码或CSV列。CSV应作为自动分析
入口；已有脚本若解析旧终端输出，请增加 `OUTPUT=raw`。`V=1`控制Make命令回显，
与runner的raw/pretty是两个不同层次的选项。

### 13.2 为什么有ZIP外的校验文件？

交付时建议成对分发 `xxx.zip` 和 `xxx.zip.sha256`。后者校验整个压缩包，
用于发现下载/复制中的损坏，在解压前使用，不需要复制到工程目录才能运行测试。
在Linux上进入两者所在目录，执行（文件名换成你实际拿到的版本）：

```bash
sha256sum -c idft_benchmark_release_20261016_output.zip.sha256
```

成功显示 `idft_benchmark_release_20261016_output.zip: OK`。
Windows PowerShell可执行 `Get-FileHash -Algorithm SHA256 .\文件名.zip`，
将结果与旁边 `.sha256` 文本的64位十六进制值比较。

ZIP内的 `SHA256SUMS` 则是源码、参考数据等各个静态文件的清单，解压后运行
`make package-check`，成功只显示 `[PASS] Release file checksums verified.`，
失败会保留sha256sum的具体错误和非零退出码。想逐项看OK可直接运行
`sha256sum -c SHA256SUMS`。

ZIP不能把“自己的最终校验值”装进自己：加入该值会改变ZIP内容，使最终校验值
随之改变，因此外部sidecar与包内清单应分别保留。SHA-256校验不是数字签名，
不能单独证明发布者身份。修改源码后包内校验不通过是正常现象，不代表算法错误。
