# Benchmark 技术细节与维护手册

版次：2026-10-16。运行命令、参数取值、输出解读和v4演练见
[使用教程](user-guide.md)。本文按“修改需求 → 文件/接口 → 约束”的顺序描述当前
实现，不将后续扩展计划当成已经实现的功能。

## 目录

1. [工程结构与修改入口](#1-工程结构与修改入口)
2. [模块ABI和生命周期](#2-模块abi和生命周期)
3. [卡带注册与适配器](#3-卡带注册与适配器)
4. [Makefile构建机制](#4-makefile构建机制)
5. [统一测试台函数与状态](#5-统一测试台函数与状态)
6. [golden格式和数值判据](#6-golden格式和数值判据)
7. [随机与独立数学参考](#7-随机与独立数学参考)
8. [输入、计时与CSV接口](#8-输入计时与csv接口)
9. [旧验证程序接口](#9-旧验证程序接口)
10. [扩展与修改配方](#10-扩展与修改配方)
11. [依赖、溯源和原工程关系](#11-依赖溯源和原工程关系)
12. [发布打包与文档维护](#12-发布打包与文档维护)
13. [现有边界与验收证据](#13-现有边界与验收证据)
14. [三版IDFT算法与历史优化结论](#14-三版idft算法与历史优化结论)
15. [终端呈现接口](#15-终端呈现接口2026-10-16)

## 1. 工程结构与修改入口

```text
Makefile                     构建、快捷目标、验证集合和变量转发
README.md                    两份主文档的导航
include/bench_module.h        模块描述符ABI
modules/catalog.mk           卡带源文件、符号、适配器和属性注册
modules/rvv_adapter.c         单点数、int16同步RVV内核的通用适配器
modules/oai_adapter.c         OAI点数表选择、初始化与dispatch
modules/v1/                  第一版原样内核和头文件
modules/v2/                  第二轮原样内核和头文件
modules/v3/                  原生DIF内核和头文件
tests/bench.c                当前统一CLI、输入、golden/random/math/bench
tests/differential_test.c     固定v1/v2满范围差分
tests/ops/                   原RVV算子测试和当时的算子头文件
tests/legacy/                完整旧harness、向量测试、独立自检、OAI链接桩
reference/                   既有text/binary golden，不由被测实现生成
third_party/oai/             冻结OAI构建依赖及许可
third_party/simde-standard/  标准SIMDe0.8.4完整头文件树
data/current/                日常追加测量，可变，不属于静态SHA清单
data/validation_20261014/     整理版验收快照
data/historical/             整理前历史CSV
docs/user-guide.md           使用教程
docs/technical-reference.md 本文
provenance/                  Git引用、依赖闭包、差异补丁、文档索引
tools/package.ps1            Windows/PowerShell7打包脚本
SHA256SUMS                   发布静态文件校验清单
build/                      编译缓存，不进入交付ZIP
```

| 要做什么 | 优先查看/修改哪里 | 还必须考虑什么 |
|---|---|---|
| 替换IDFT算法 | modules/<new>/、catalog.mk | ABI、精度契约、头文件路径 |
| 模块初始化/不同数据类型 | 新adapter及bench_module.h | 初始化是否计时、转换成本和状态 |
| 添加新点数 | 内核、adapter、golden/math规则 | 动态缓冲仅解决容量，不等于算法和参考已支持 |
| 改随机输入 | tests/bench.c::input、random_check | seed、benchmark输入hash、不同模式独立重置rng |
| 改精度判据 | golden_check、random_check、math_check | 三者规则不同，应分别维护 |
| 改计时方式 | benchmark、now_ns | warmup、屏障、间接调用、输出消费、CSV |
| 改编译器或编译选项 | Make变量CC/OAI_CC/CFLAGS/ARCHFLAGS | cache身份、OAI编译兼容性 |
| 加自动验收集合 | Makefile::check-all/matrix等循环 | 注册卡带不自动加入这些固定循环 |
| 改结果字段 | benchmark的CSV头和写入格式 | 日志消费者和旧CSV格式兼容 |
| 改依赖版本 | third_party、INC/DEFS、provenance | 许可、补丁、基线golden重新验收 |

## 2. 模块ABI和生命周期

### 2.1 公开描述符

定义文件：`include/bench_module.h`。

```c
typedef struct {
  const char *name;
  const char *contract;
  int (*setup)(size_t points, int inverse);
  void (*run)(int16_t *input, int16_t *output, unsigned char scale);
  int approximate;
} bench_module;

extern const bench_module selected_module;
extern const bench_module reference_module;
```

| 字段/符号 | 用途 | 约束 |
|---|---|---|
| name | 终端与CSV中的模块名 | 持久、非空字符串；建议避免逗号/换行以兼容当前CSV |
| contract | 人可读的数值契约 | 仅日志标签，代码不会解析此字符串决定验收 |
| setup(points,inverse) | 选择点数、方向、准备状态 | inverse=1为IDFT、0为DFT；0成功，非0拒绝 |
| run(input,output,scale) | 同步执行变换 | 返回前写完输出；无返回码和N参数 |
| approximate | 选择当前内置误差策略 | 0=exact，非0走v3式策略；不是任意自定义容差配置 |
| selected_module | 当前构建选择的候选 | 链接进程中恰好一份定义 |
| reference_module | 冻结OAI参考 | 所有统一程序均链接，用于random和支持性检查 |

`contract`写“精确”不会强制精确；实际分支依据approximate。新增不同精度策略时，
应扩展描述符和判据，不能只换文字。

### 2.2 内存契约

- 输入和输出是独立缓冲，均64字节对齐；不把原地变换作为当前共同契约。
- 每个复数由两个有符号int16组成：`input[2*k]=Re`，`input[2*k+1]=Im`。
- 每个缓冲实际分配`2*N*sizeof(int16_t)`字节。
- run没有N参数。需要N的实现应在setup内检查并保存运行配置，或编译成单点数内核。
- 输入指针类型不是const，但测试的公平性依赖候选保持输入不变。当前runner不会
  给每次调用复制输入，也没有自动检查输入是否被候选写坏。
- 输出指针不允许越界，必须写完本次2*N个分量；统一runner未提供自动红区检测。
- 不支持异步完成：后台GPU/线程提交后立即返回会测到提交成本而非完整变换。
- run内不要包含测试日志、随机生成、参考计算或每次不必要的初始化。

输入可能被破坏的候选不能直接共用当前reference流程，需要先扩展输入保护或副本
机制，并明确这部分复制是否属于被测成本。

### 2.3 scale

run接收unsigned char，CLI允许0～255。三份IDFT64内核中scale是布尔开关：

```text
scale == 0：关闭末端算术右移3位
scale != 0：执行末端算术右移3位
```

因此SCALE=2和SCALE=255也不是分别右移2/255位。这个约定继承原IDFT64接口；其他
点数或其他模块的scale含义必须单独声明，不能无条件套用N64增益公式。

### 2.4 生命周期和线程限制

```text
解析CLI → 参数检查/锁核 → selected.setup(N,dir)
                        → reference.setup(N,dir)
                        → 打印环境 → 所选测试模式
bench模式：生成一份输入 → hash → warmup10000次 → 每轮ITERS次run → 统计/输出CSV
```

reference.setup目前每次统一程序都调用，即使模式仅bench/golden。新卡带支持的
N/方向还需被当前OAI参考接受；完全独立于OAI的新变换类型需要改main的这项限制。
描述符及其adapter有进程全局状态；当前是单进程单线程、一次配置。不能直接并发
在同一进程配置两种点数并假定互不影响。

## 3. 卡带注册与适配器

### 3.1 catalog字段

`modules/catalog.mk`的现有项：

```make
CARTRIDGES := v1 v2 v3 oai
SOURCE_v2 := modules/v2/idft64_rvv.c
ENTRY_v2 := idft64_rvv
SYMBOL_v2 := kernel_v2
ADAPTER_v2 := modules/rvv_adapter.c
POINTS_v2 := 64
INVERSE_v2 := 1
APPROX_v2 := 0
```

Makefile对`filter-out oai`后的每项展开RVV_RULES，创建kernel、adapter、bench和
legacy构建规则。主源文件编译时传入`-D<ENTRY>=<SYMBOL>`，将相同公开内核名转为
不同链接符号。头文件中的对应声明也会被同一宏替换。

`SOURCE`当前模板按一个主要.c文件处理；多个源文件需扩展对象列表和链接规则。
kernel自动依赖`modules/<name>/*.h`，同时使用-MMD/-MP记录编译器实际头依赖。
adapter模板仅显式依赖本身和bench_module.h，没有完整的外部头依赖自动规则。

当前统一程序最终链接：

```text
runner.o + adapter-<impl>.o + kernel-<impl>.o + reference.o
         + oai_dfts.o + time_meas.o + taus.o + stubs.o + libm + pthread
```

oai卡带不使用普通RVV_RULES：`oai-selected.o`同时定义selected/reference两份描述符，
链接时不再加reference.o，避免重复定义reference_module。

### 3.2 通用rvv_adapter.c

接入的内核必须匹配：

```c
extern void KERNEL_SYMBOL(int16_t *, int16_t *, unsigned char);
```

setup同时要求：

```c
inverse == MODULE_INVERSE && points == MODULE_POINTS && vlenb == 32
```

传入的MODULE_*宏由catalog转换得到。vlenb检查固定为32字节，说明此适配器服务于
本轮K3/VLEN256内核。支持其他VLEN、多个点数或其他输入类型，应提供新adapter。
普通adapter的run直接指向KERNEL_SYMBOL，无额外逐点循环。

### 3.3 oai_adapter.c

关键状态和函数：

| 项 | 含义 |
|---|---|
| index_selected | 上游DFT/IDFT表索引，uint8_t |
| inverse_selected | 当前方向 |
| tables_ready | 此进程是否已初始化大点数旋转表 |
| setup | 根据FOREACH_DFTSZ/FOREACH_IDFTSZ宏生成switch，拒绝不支持尺寸 |
| run | 按方向调用dft_implementation或idft_implementation |

不硬写IDFT64和DFT64的相同索引，因为它们在上游表中分别是0和5。非64点首次
setup调用dfts_autoinit，发生在计时外。DFT12是8并行变换的特殊ABI，适配器主动
拒绝将其作为当前单变换接口暴露。

approximate=0的reference描述符不意味着它是无限精度：它是冻结的原定点计算图。
random检查用它做实现等价参考，math和x86 golden提供另一层独立验证。

## 4. Makefile构建机制

### 4.1 构建身份与缓存

```text
BUILD_ID = SHA256(CC及其版本 + OAI_CC及其版本 + CFLAGS + ARCHFLAGS)的前12位
BUILD = build/<BUILD_ID>

BASE_ID = SHA256(OAI_CC及其版本 + CFLAGS + ARCHFLAGS + SIMDE_NO_NATIVE)的前12位
BASE_BUILD = build/base-<BASE_ID>
```

候选编译器切换会产生不同BUILD，避免用旧gcc对象冒充clang。OAI参考只依赖自己的
编译器和共同参数，所以BASE_BUILD可以跨候选CC共享，减少首次完整OAI构建次数。
这种身份不包含源码内容；源文件更新仍由make时间戳与.d依赖判断。
它也不等于整个工具链可复现镜像，链接器/系统库升级仍可能影响结果。

### 4.2 主要构建变量

| 变量 | 用途 |
|---|---|
| CC | 候选内核、统一runner、部分固定测试的编译器 |
| OAI_CC | OAI参考、OAI adapter和最终链接，默认gcc |
| CFLAGS | 默认-O2 -std=gnu99，同时作用于候选与OAI |
| ARCHFLAGS | 默认-march=rv64gcv |
| INC | include目录；包内SIMDe在系统目录前 |
| DEFS | OAI最小配置、write_output守卫和SIMDE_NO_NATIVE |
| BASEOBJ | oai_dfts/time_meas/taus/stubs四个共同对象 |
| REF | 不含SELECT_OAI的OAI适配器对象 |
| META | 将CC字符串和flags写入runner的BUILD_CC/BUILD_FLAGS宏 |
| ARGS | 转发方向、点数、CPU、scale和seed给统一CLI |
| PIN | CPU=-1时为空，否则为taskset -c <CPU>，用于固定旧测试 |

部分上游tools_defs.h使用运行时变量调用要求立即数的SIMDe shift，Clang会拒绝。
因此支持“候选CC=clang，OAI_CC=gcc”的混合构建。单独改CC不会把OAI也切成Clang。

### 4.3 目标到程序的映射

| Make目标 | 程序/转发 |
|---|---|
| check | bench-<IMPL> --mode golden $(ARGS) |
| quality | bench-<IMPL> --mode random $(ARGS) --cases $(CASES) |
| math | bench-<IMPL> --mode math $(ARGS) |
| bench | bench-<IMPL> --mode bench $(ARGS)加iters/reps/signal/mag/csv |
| v1/v2/v3/oai | 递归调用check、bench，并固定IMPL为目标名 |
| legacy-* | legacy-<IMPL>加对应旧CLI，不使用统一ARGS |
| differential | 私有kernel_v1/kernel_v2与固定测试主程序链接 |

check-all和matrix的卡带循环当前写死v1/v2/v3/oai或v1/v2/v3；catalog新增卡带只
生成构建规则，不自动改变这些集合。快捷目标列表也独立于catalog。

### 4.4 元数据变更与重新构建

目前catalog.mk/Makefile本身不是每个adapter对象的显式依赖。因此：

- 新增v4会创建全新的对象路径，首次构建正常。
- 只修改已有卡带的POINTS/INVERSE/APPROX等注册属性，不能假定旧adapter必然重建。
- 修改已有注册项后可`touch modules/rvv_adapter.c`再构建，使共享适配器重编译；
  自定义adapter则触碰对应源文件。它改变时间戳，不改算法内容。
- `make -B build IMPL=...`会强制所有依赖重编译，包括耗时OAI；仅在明确需要时使用。

若要进一步改进此机制，应把catalog、Makefile或生成的配置指纹作为对象依赖。
本文记录现状，不宣称当前对所有配置变更都已有自动内容追踪。

## 5. 统一测试台函数与状态

核心文件：`tests/bench.c`。修改时建议依次读main、对应模式、其辅助函数。

### 5.1 options

```c
typedef struct {
  size_t n;
  int inverse, cpu, scale, mag, reps, cases;
  uint32_t seed;
  uint64_t iters;
  const char *signal, *mode, *golden, *csv;
} options;
```

默认值在main内的options聚合初始化中声明，不是从catalog读取。Makefile也有一套
默认值，两者应一起维护。若只改Make默认，直接运行二进制仍使用C默认。
默认csv在C中为NULL；make bench通过--csv提供路径，因此直接执行程序默认不写CSV。

### 5.2 函数接口索引

以下均为当前文件static内部接口，外部卡带不要直接链接调用：

| 函数 | 输入/输出与职责 |
|---|---|
| random32(void) → uint32_t | 更新全局rng的xorshift32，返回下一值 |
| now_ns(void) → uint64_t | CLOCK_MONOTONIC_RAW纳秒；失败perror并exit(2) |
| aligned_buffer(bytes) → void* | posix_memalign(64)+清零；失败exit(2)，调用方free |
| input(x,options*) | 按当前rng和signal/mag/n写入2*N输入，不负责初始化rng |
| compare(a,b,lanes) → errors | 累计差异数、最大误差、误差平方和、参考能量 |
| nrmse(errors) → double | 百分数NRMSE，零能量特殊处理 |
| pin(cpu) → int | cpu<0不锁核；否则sched_setaffinity，失败-1 |
| environment(options*) | 打印描述符、系统、编译器、参数、目标CPU频率和vlenb |
| parse_words(line,out,n) → int | 从X/Y行解析恰好n个int16，非法范围/多余内容拒绝 |
| golden_check(options*) → int | 解析固定参考文件，逐例按契约判断并汇总 |
| random_check(options*) → int | 四档幅度、大量输入，与reference.run差分 |
| math_check(options*) → int | N64、8组低幅输入，对照独立double变换 |
| cmp_double(a,b) → int | qsort比较器 |
| benchmark(options*) → int | 单输入、warmup、批量计时、排序统计和CSV |
| help(void) | 统一CLI说明 |
| number(str) → uint64_t | 严格解析非负十进制数，非法输入exit(2) |
| main(argc,argv) → int | CLI解析、基础验证、setup、环境、模式分派 |

`errors`结构为`{ int max; size_t differing; double squared, energy; }`。
模块适配器不会收到options结构，测试台和算法通过bench_module隔离。

### 5.3 返回与参数检查

统一CLI约定0=成功，1=数值失败，2=参数/参考/不支持/环境错误。make通常把子命令
失败统一返回2，排错需同时看日志末尾Error后的子进程状态。

main检查N为1～98304、iters>0；reps为1～10000、cases为1～1000000、seed为非零
uint32、mag为0～32767、scale为0～255，cpu为-1或CPU_SETSIZE范围内非负编号。
实际CPU还需被sched_setaffinity接受。信号和方向只接受明确的小写值。
这些边界是防误用，不是对巨大ITERS/CASES耗时的自动预算管理。

## 6. golden格式和数值判据

### 6.1 文本参考

`reference/x86_golden.txt`，来自原x86 OAI输出。示意：

```text
# comment
NCASES 26
CASE idft64 sparse 364 1
X <128个int16十进制数>
Y <128个int16十进制数>
CASE dft64 random 2048 1
X <128个int16十进制数>
Y <128个int16十进制数>
```

尖括号为格式示意，实际文件必须是数值。CASE依次包含函数名、信号名、幅度、scale。
状态机CASE→X→Y，读取固定X直接运行候选，避免输入重建差异被误判为算法问题。
parse_words要求每行恰好128个合法int16，不能少、多或越界。

方向为idft时只执行idft64用例，dft时只执行dft64用例；仍解析并统计整个文件的CASE
头数，与NCASES核对。遇到不完整状态、格式问题、读取错误或未选中任何用例返回2。
当前明确限定N64，读取128元素是这一固定golden格式的一部分，不是通用runner缓冲。
文件内有14例IDFT和12例DFT，总数26。

### 6.2 二进制参考

```text
reference/x86_idft64_scale1_seed1.bin
reference/x86_dft64_scale1_seed1.bin
```

每个512字节：前128个int16为输入、后128个int16为输出。由legacy-tier1入口解释。
当前Make的legacy-tier1固定选IDFT文件，DFT文件仍保留。data/historical/x86中另有
256字节旧文件，不是完整Tier1输入输出对，不能替换这些裁判。

### 6.3 误差公式

对L个int16分量，`d_i=got_i-reference_i`：

```text
different = count(d_i != 0)
max_abs   = max(abs(d_i))
SSE       = sum(d_i^2)
Eref      = sum(reference_i^2)
RMSE      = sqrt(SSE / L)
NRMSE%    = 100 * sqrt(SSE / Eref)
```

Eref=0且SSE=0时NRMSE=0，Eref=0但SSE>0时为无穷大。不是逐分量相对误差平均，
接近零的某个分量不会单独导致比例爆炸。

### 6.4 exact与approximate

| 模式 | exact | approximate当前内置策略 |
|---|---|---|
| golden | max_abs=0 | 正常scale非零max_abs<=2，scale0<=16，且NRMSE<0.5% |
| random | 全四幅度max_abs=0 | 364/2048：scale非零<=4、scale0<=32且NRMSE<0.5%；高两档REPORT |
| math | double RMS EVM<1% | 相同double标准 |

golden中的压力判定是`random && mag==32767`，或`(dc || tone) && mag==4096`。
只有approximate卡带将这些用例标为REPORT；exact仍严格比较。压力判断依赖CASE
标签，是当前套件的明确分类，不是自动探测中间饱和。

summary分别累计pass/fail/report，不能把report累加到pass。v3为14例中10pass、
4report，0fail；这不等于14例都在正常误差容限内。random没有逐例饱和标志，4095
只是经验幅度，不能推导所有该幅度向量都无饱和。

修改新算法的数值契约时至少要同时检查golden_check、random_check、math_check，
还需决定legacy入口是否适用。approximate=1并不提供可配置的任意阈值。

## 7. 随机与独立数学参考

### 7.1 random_check

固定幅度数组为`{364,2048,4095,32767}`；每档运行options.cases次。
每档rng初始化为`SEED ^ MAG`，结果若为0则改为1。输入函数固定random，不读取
用户选择的SIGNAL。候选先运行，参考后运行，对同一输入逐分量比较，跨所有case
累加SSE/Eref。总lane数为`2*N*CASES`。

固定幅度数组不由Make的MAG控制。要允许自定义幅度列表，应新增CLI/配置解析并
同时记录到结果中，不应悄悄改变现有CASES含义。

如果IMPL=oai，候选和reference实际是同一冻结算法。该差分只证明接线/运行一致，
不能独立证明OAI数学正确；它的独立裁判是x86 golden和double。

### 7.2 math_check

目前限定N64。固定8组随机、分量幅度364，rng从SEED初始化；每组直接双重循环：

```text
IDFT: sum x[n] * exp(+j*2*pi*k*n/N)
DFT : sum x[n] * exp(-j*2*pi*k*n/N)
```

对比增益：

| N64方向 | scale=0 | scale非零 |
|---|---:|---:|
| IDFT | 1 | 1/8 |
| DFT | 1/4 | 1/8 |

这继承本模块定点增益，不是标准IDFT的1/N归一化。累加实部与虚部误差平方，RMS
EVM%=100*sqrt(总误差能量/总double参考能量)，要求严格小于1%。max_abs可为小数，
因为参考是double。

新点数的增益不能仅依赖sqrt(N)猜测，必须明确建立契约后再启用math。
对高幅/饱和输出与不裁剪的double比较，需要另行定义评估方式；当前低幅度方案
主要用来检查数学方向、系数、输出顺序和增益。

## 8. 输入、计时与CSV接口

### 8.1 input函数

| signal | 生成规则 |
|---|---|
| random | 每个分量均匀取整数[-MAG,+MAG]，伪随机可复现 |
| impulse | x[0]=MAG，其余0 |
| dc | 所有实部/虚部分量均为MAG |
| tone | 第5频点复指数，cos/sin乘MAG后lrint为int16 |
| sparse | 两端非零带；N64时非零分量索引2..35、92..127，值±MAG |

三种模式各自设置rng：bench为`0x12345678 ^ MAG ^ (SEED-1)`，random为SEED^MAG，
math为SEED。因此相同SEED不表示bench和quality会生成相同输入；input_hash专用于
核对bench之间的输入一致性。golden的X则从文件读取，不调用此生成器。

### 8.2 benchmark时序

1. 分配两个64B对齐缓冲和reps个double统计数组。
2. 生成一份固定输入，计算64位FNV式输入指纹。
3. warmup10000次selected_module.run。
4. 每轮读一次start，调用ITERS次run，读一次stop。
5. 在计时外消费一个输出分量到volatile sink，存本轮ns/call。
6. qsort各轮均值，统计min、median、max。
7. 打印RESULT，选择性追加CSV。

now_ns为CLOCK_MONOTONIC_RAW，使用编译器memory屏障。这里不是rdcycle/rdtime计数，
不能直接称为核心cycles。每次run通过描述符间接调用，OAI内部还包含dispatch，
这些模块入口成本被计入。输入生成、初始化、参考运算、校验、打印和文件IO不计入。
不在计时循环内变换输入、校验或触碰sink，避免给每次调用人为增加索引成本。

warmup10000目前固定，无WARMUP变量。bench单独运行不先检查数值；v1/v2/v3/oai
快捷目标才以递归make串联check和bench。

### 8.3 CSV结构

| 列 | 类型/单位 |
|---|---|
| unix_time | time(NULL)，Unix秒数 |
| module | selected_module.name |
| compiler | BUILD_CC，候选CC和版本字符串 |
| flags | CFLAGS与ARCHFLAGS |
| transform | idft/dft |
| points | N |
| scale | 实际unsigned char开关值 |
| cpu | 目标核心，-1不锁核 |
| seed | uint32种子 |
| signal | 输入种类 |
| mag | 分量幅度 |
| iters | 每轮调用数 |
| reps | 轮数 |
| input_hash | 16位十六进制的64位指纹 |
| min_ns/median_ns/max_ns | 每轮平均ns/call的统计值 |

文件以a+打开，空文件才写表头，每次bench写一行，轮次只打印终端。make bench创建
CSV父目录；直接二进制不负责创建目录。CSV没有逐样本输出，也没有把OAI_CC、CPU
频率、内核版本、VLEN都写进每行；这些信息在环境日志和provenance中。需要严格
长期流水线时建议进一步把这部分环境纳入结构化结果。

## 9. 旧验证程序接口

### 9.1 tests/legacy/harness.c

保留原N64测试，外部连接idft64/dft64、官方dispatch，BENCH_USE_RVV=1时IDFT改调
对应私有kernel_v1/v2/v3，DFT仍走OAI。不能将它当作已经修复成动态多点数的统一程序。

主要CLI：

| 旧CLI | 作用 |
|---|---|
| --check FILE | 26例文本参考，重建输入并比较 |
| --check-tolerant FILE | 原生IDFT容差，DFT仍exact，压力REPORT独立累计 |
| --check-tier1 FILE | binary输入输出对的逐位校验 |
| --tier2 | 旧double EVM幅度扫描，两个方向都检查 |
| --quality-random N | 旧实现随机差分入口（当前make quality使用新runner） |
| --fn、--iters、--reps等 | 旧计时参数，不是新runner的同一套CLI |

旧文本参考包含自生成输入校验，而新golden直接读取文件X。两者互补。
旧Tier2对不同幅度扫描，判定“存在一个幅度满足平均逐复数相对误差<1%”；新math
是固定幅度、全能量RMS EVM，两者不是相同聚合指标。
历史harness还保留写参考入口，但发布流程不使用，不得从候选实现重建独立裁判。
旧代码的输入/seed等历史限制不作为当前通用CLI能力宣传。

### 9.2 固定独立程序

| 文件 | 链接接口 | 固定测试范围 |
|---|---|---|
| tests/differential_test.c | idft64_rvv_orig和idft64_rvv，由-D映射kernel_v1/v2 | 4个scale×(12模式+100000随机)=400048组，偏移2B交替 |
| tests/ops/rvv_ops_test.c | RVV算子头与标准SIMDe intrinsic | 10项×边界/随机+4项lane操作，共24项 |
| tests/legacy/main.c | idft64_rvv映射kernel_v1或v2 | golden.h内3例，--check跳过旧计时 |
| tests/legacy/rvv_idft64_test.c | idft64与相应RVV符号 | 稀疏、多个幅度、满幅、冲激、DC等向量 |
| tests/legacy/compat_stubs.c | OAI缺失的日志/配置/随机/退出符号 | 只为离线DSP测试链接，不是完整OAI运行环境 |

ops使用独立保留的早期算子头，不意味着v2的每个新复合helper也被逐个unit-test；
v2由整体golden和大差分覆盖。differential不支持CASES变量，其规模写在固定测试内。

compat_stubs提供exit_function、config_get、config_check_unknown_cmdlineopt、
fill_random、日志符号等。其中g_log使用零填充内存关闭dump，适合当前冻结代码；
升级上游日志结构或启用日志功能时必须重新核对，不能作为通用日志ABI实现复用。

## 10. 扩展与修改配方

### 10.1 同接口新IDFT64优化

按使用教程复制v2为v4，注册独立符号，沿用通用adapter。先不改算法，确认接线，
再做优化。保持v2不动。若是新增.c且依赖自己目录里的头，当前模板可直接使用。
仅注册不会产生make v4快捷目标，也不会加入matrix/check-all；需要显式修改列表。

### 10.2 自定义adapter模板

示意一个带初始化状态的int16模块（名字和函数为示意，需由实际内核提供）：

```c
#include "bench_module.h"
#include "my_kernel.h"
static size_t configured_n;

static int setup(size_t n, int inverse) {
    if (!inverse || n != 64) return -1;
    if (my_kernel_prepare(n) != 0) return -1;
    configured_n = n;
    return 0;
}
static void run(int16_t *x, int16_t *y, unsigned char scale) {
    my_kernel_execute(configured_n, x, y, scale);
}
const bench_module selected_module = {
    "my_impl", "document the real numerical contract", setup, run, 0
};
```

adapter只定义selected_module，reference由公共OAI对象提供。内核函数要按catalog
实际私有符号调用，或者让编译宏KERNEL_SYMBOL参与声明/调用；示意函数本身不在包内。
使用普通RVV_RULES时主源必须提供ENTRY对应函数；若多文件/特殊依赖需调整链接规则。

### 10.3 float或SoA内核

外部ABI仍是交错int16时，adapter可负责转换。但若转换放run内，它属于被测模块
入口成本；若只想测核心float计算，需要新增“预处理后数据”的接口和独立公平比较
方案。不要在setup转换某个固定输入后无视run传入指针，那样golden/random会失效。
异步设备需要明确同步边界，当前同步接口不负责自动等待。

### 10.4 新点数或方向

1. 实现该N/方向的算法；动态缓冲并不替你实现算法。
2. adapter拒绝其余组合，多点数时保存配置或查函数表。
3. 检查reference_module也支持该组合，必要时扩展参考接口。
4. 建立scale、增益、舍入、饱和、布局和溢出的数值契约。
5. 从独立来源提供对应golden，再扩展解析/文件选择。
6. 更新math增益与试验方案，给不同点数合理验证成本。
7. 添加测试与性能记录，更新两份文档和发布清单。

OAI的512/1024/2048接口已经随包存在并曾烟测，但没有本轮新增golden。原上游尺寸
列表可在third_party/oai/openair1/PHY/TOOLS/tools_defs.h的FOREACH_*宏查看。
本轮不提前构造这些点数的固定答案。

### 10.5 改容差

先写出允许变化的原因和目标输入范围，再改golden/random/math策略。最好把当前
approximate布尔量演进为显式policy或回调，使各卡带具有自己的阈值、压力规则和
数学增益；在此之前所有approximate=1都共享v3策略。只修改contract字符串不生效。
不要按某次失败结果事后扩大阈值，并把已超契约输入描述为“正确性通过”。

### 10.6 修改输出

新增CSV列时同时修改表头和fprintf格式，明确旧文件是否兼容。当前追加不会核对
已有CSV表头版本，切勿把新旧列数写入同一文件。新增环境字段也要定义单位和来源。
需要原始输出向量时新增独立导出模式，在计时之外写文件，不能在每次run内打印。

## 11. 依赖、溯源和原工程关系

### 11.1 原目录与分支

| 来源 | 角色/处理 |
|---|---|
| ~/dft_test | 主仓库，导出4个分支，未改原工作区 |
| ~/idft64_rvv | 与rvv-v1内核同hash，去重 |
| ~/idft64_rvv_round2_20260915 | 与rvv-v2内核同hash，提取差分和报告 |
| ~/backup_dft_premerge_20260918.tar.gz | 合并前快照，保留目录证据 |
| FFT优化尝试.zip | 补齐早期算子测试、golden、x86/K3数据及文档 |
| ~/backups/simde-0.8.4-clean-20260912.tar.gz | 标准头文件来源 |
| ~/sionna-rk/ext/openairinterface5g | 冻结OAI构建依赖来源 |

```text
main 8a2333e → e6afef2加入RVV → rvv-v1 0a061e4
            → rvv-v2 8df83e6 → rvv-v3-native d33ee09
```

main是无手写RVV的原基线，v1支持混合Clang，v2为bit-exact优化，v3新增原生内核
和容差验证。v3仍包含v2源码，交付时按模块分开，不要求用户checkout不同分支。

sionna-rk的main/k3-rv64-cpu-only在探查时都指0abdce3；course/demapper-rvv属于
另一课程工作树，DFT相关差异检查未发现本次新增卡带。OAI为detached HEAD
207aac94d13e714abac8062ebf869805f2acd6dc，镜像只有2025.w34标签引用。
来源证据在provenance的git-history、git-refs、*-branches、oai-mirror-refs等文件。

探查以家目录相关工程、所有主仓库分支、独立目录、备份和参考包为范围，没有将
系统/cache或无关课程工程当作IDFT模块资产。

### 11.2 OAI闭包与标准SIMDe

旧工程通过绝对/家目录路径引用OAI，新包依据编译器依赖生成取得27条路径，含别名，
归并复制实际源码/头文件及LICENSE/NOTICE。完整oai_dfts.c保留当前上游多点数实现。
实际复制文件见provenance/oai-copied-files.txt，原路径见oai-dependency-paths.txt。

依赖闭包相对上游HEAD的本地修改记录在oai-dependency-all-changes.patch：

- oai_dfts.c仅在最外层架构guard增加__riscv，无算法主体改动；
- time_meas.h增加RISC-V类型与CLOCK_MONOTONIC_RAW适配，避免用户态rdcycle SIGILL。

SIMDe为0.8.4 clean备份的完整头文件树，不是系统中修改过的avx2.h/sse2.h。具体hash
证据见simde-standard-vs-installed.txt。编译显式SIMDE_NO_NATIVE，选择标准SIMDe的
可移植语义路径；gcc可能自动向量化C表达式，不能把它称作绝对无向量指令的汇编。
各文件保留原版权/许可头，OAI许可与NOTICE随包。

系统SIMDe和原OAI工作树没有被修改。编译器、make、glibc/Linux头文件属于工具链
前提，不复制操作系统。未来新的库依赖仍需在加入时冻结版本和许可，无法预先打包
未定义算法的一切可能依赖。

### 11.3 排除与历史材料

修改SIMDe以启用RVV快路径的B线不提供运行卡带，按用户选择排除；原summary和
报告仍保留供追溯。v1/v2/v3自身不依赖SIMDe，均可运行。
重复独立源码、*.o、旧可执行文件、大反汇编、聊天归档和早期临时探针不进入精简包。
它们在原来源位置保留。历史中文说明已移至包外归档，索引与正文一起归档。

ZIP内容直接hash证据见zip-hash-evidence.txt。独立第一版/第二版与分支同hash：

```text
v1 idft64_rvv.c: f31f2f9891409f2db3098377eda0eddd57aeb4309a159ff65ea014b9fde10c27
v2 idft64_rvv.c: 703a3693fc117f361d3e13e61210486352e7dbe12ccf0d175734fa3ccc732c76
golden text:    7712275d033456b64b4d2db4d7fb748decf338ac83aae842b3b5865e8b6179f7
```

## 12. 发布打包与文档维护

### 12.1 两份主文档

README只保留导航。使用教程负责命令、参数、输出、常见错误和卡带入门；本文负责
代码接口、公式、构建与修改。原module.md/tests.md/inventory-and-delivery.md的
有效信息已合并，当前docs根不再并列维护这三份说明。
发行包内docs只包含这两份主文档。原15份历史说明已移出发行包，其仍有用的算法
要点合并到本文第14节；来源/版本和验收结论已合并到第11/13节。独立历史归档只是
溯源备份，不是运行、阅读或维护当前发行版的必需依赖。

### 12.2 静态清单

SHA256SUMS覆盖发布源码、说明、固定参考、冻结依赖、历史与验收快照，不覆盖
build和data/current。运行新测量不应使package-check失效；修改静态文件则应失效。
清单本身不包含自身哈希，整个ZIP另有侧置.sha256校验。

在Windows的PowerShell7环境中，从解压工程执行：

```powershell
pwsh -File .\tools\package.ps1 -OutputZip D:\output\my-benchmark.zip
```

预先建立D:\output，替换为自己的路径。脚本使用.NET GetRelativePath等API，不要
假定Windows PowerShell5.1兼容。OutputZip必须尚不存在；脚本拒绝覆盖已有文件。
不要省略OutputZip而意外指向旧版固定名称。

脚本重新生成UTF-8无BOM、LF分隔的清单，创建UTF-8路径ZIP，排除build；data/current
虽然不在静态清单，仍会进入ZIP。如不希望发布自己的日常记录，先在发布副本中整理
这些内容。整个打包动作不要与源文件正在修改/程序正写CSV并行。

### 12.3 发布检查

1. 明确源码与数值契约，执行适当门禁。
2. 更新教程、技术文档及示例输出来源。
3. 冻结本次证据到独立data/validation_<date>，不要覆盖原始验收日志。
4. 打包并在K3解压副本中执行package-check。
5. 使用已验证的相同代码/缓存做入口烟测；若依赖或代码变化，则另做相应构建验证。
6. 核对传输前后ZIP SHA256；保留旧版本以便回退。

## 13. 现有边界与验收证据

### 13.1 当前能力边界

- v1/v2/v3只提供IDFT64，当前通用adapter只接受VLEN256。
- golden/math明确只有N64契约；OAI多点数bench可运行不等于已有独立正确性裁判。
- 默认CPU2是本次K3配置，其他机器需自行选择有效CPU。
- 模块输入不变性、越界、线程并发、异步完成不由当前runner全面自动验证。
- catalog更改已有元数据时需留意缓存；csv不会自动检查已有表头版本。
- fixed-vs-approx只提供两类现有policy，不是任意数值语义插件系统。
- check-all/matrix/快捷目标是显式集合，注册新卡带后需维护相应列表。

### 13.2 2026-10-14整理验收

实际make check-all通过：v1/v2/oai的IDFT14例exact、OAI DFT12例exact，v3为10pass+
4report，RVV算子24项、v1/v2的400048组差分（51206144 lane）、两份3例独立自检、
早期向量、旧26例、旧Tier1和Tier2、错误输入门禁均完成。
新的double IDFT RMS EVM：v1/v2为0.300543%，v3为0.319649%；DFT为0.438242%。
v3四幅度2000组差分：前两档最大差2LSB；4095/32767压力NRMSE约3.318443%/
50.737980%，按REPORT保留。
OAI的DFT/IDFT512、1024、2048完成接口/初始化烟测，未生成新golden。
日志、CSV和原环境在data/validation_20261014及provenance。

整理中修正的历史测试问题：压力report单独计数、DFT不使用IDFT容差、早期向量
max-error三元表达式优先级错误；新入口采用真实seed、实际目标CPU频率、动态2*N
缓冲和严格参考解析，不把固定2.2GHz换算值冒充实测硬件周期。

2026-10-14同场性能（CPU2，100000×7，相同input_hash）：

| 卡带 | GCC15 | GCC16 | Clang21 |
|---|---:|---:|---:|
| v1 | 617.621ns | 604.789ns | 584.173ns |
| v2 | 394.500ns | 381.690ns | 349.273ns |
| v3 | 236.698ns | 236.585ns | 247.284ns |

v3两种gcc差距小于0.1%，按基本持平解释。标准SIMDe/OAI的IDFT64约7782.925ns，
DFT64约7839.471ns，使用10000×3；不要与其他日期/输入/轮次直接计算严格同场收益。

### 13.3 2026-10-15文档核对

源码与上一交付版一致；本轮核对了help、v2/v3 golden、短quality、math、短bench、
不支持方向报错，并在/tmp隔离副本复制v2为v4、追加catalog配置，实际完成v4的
golden和短bench。它验证教程接入步骤，不作为新增优化算法成果。
本轮未大规模重新构建OAI、未重新运行完整性能矩阵；输出节选注明来自原验收或
本轮短测，避免把文档更新描述成新的性能优化实验。

## 14. 三版IDFT算法与历史优化结论

本节合并旧算法解说和优化报告中仍影响维护的部分。旧环境地址、对话背景、过时
命令与重复入门问答不再进入发行包。原始材料见包外归档，其哈希记录在
`provenance/documentation-archive.txt`。历史性能不能与第13节新测试台数字直接
拼接计算同场加速比。

### 14.1 共同数学与定点约束

64点IDFT使用64=4×4×4的radix-4分解；输入64个交错int16复数，Q15旋转因子
由编译期常量表提供。蝶形使用饱和加减；饱和是非线性的，`sat(sat(a+b)+c)`
不一定等于`sat(a+sat(b+c))`。改变结合顺序、定标位置或分解，可能改变大幅输入。
所以v1/v2坚持逐位一致，v3需要另设精度与压力政策。

### 14.2 v1：映射原AVX2计算图

文件为`modules/v1/idft64_rvv.c`和`rvv_ops.h`。VLEN256的一条e16m1含16个int16，
即8个交错复数；64个复数占8条向量。数据流：

```text
输入permute → AVX2风格unpack → 两组16点IDFT
            → 64点旋转因子+radix-4合并 → 可选>>3 → 输出
```

rvv_ops模拟双源unpack、两个128bit lane、符号变换和复数交换。RVV普通gather只有
一个数据源，通用双源操作需要额外gather/merge。v1便于对照原计算图，代价是布局兼容。

### 14.3 v2：保留计算图，减少搬运与复乘开销

文件为`modules/v2/idft64_rvv.c`和`rvv_ops.h`。五项改动：

| 改动 | 原理与维护注意点 |
|---|---|
| 固定输入索引 | 使用[0,1,8,9,4,5,12,13,2,3,10,11,6,7,14,15]，8条输入共用 |
| sign+swap融合 | 先符号后交换等价于交换后乘[+1,-1]；保留int16最小值取负的原回绕行为 |
| slide转置 | 用vslideup/vslidedown拼固定128bit半段，替代extract/insert的gather/merge链 |
| 固定双源gather | 预计算低索引和0xCCCC/0xF0F0掩码，删除索引计算、比较与显式merge |
| packed-int32复乘 | 原位reinterpret，将相邻re/im视为packed int32，移位提取、e32乘加、vnclip窄化 |

`vnclip(...,15,RDN)`不加舍入量，匹配原算术右移15和int16饱和；不能随意更换VXRM。
旋转表含实/虚乘加所需的符号编码，维护时以实际表和helper为准，不凭变量名重构。

2026-09-15短基准累积结果：616.87→600.73→569.40→525.72→476.95→381.63ns。
当轮用400048组差分验证逐位一致。试过的双gather+merge略慢；手工成组调度得到
相同二进制；预加载全部索引增加寄存器压力且退化，均未保留。不能只按源码里的
加载或算术次数估计收益，应检查寄存器、反汇编和实际时间。

### 14.4 v3：原生SoA三层radix-4 DIF

文件为`modules/v3/idft64_rvv_native.c`。vlseg2e16把交错输入拆为实部/虚部向量，
每层并行16个蝶形。逆蝶形数学关系：

```text
ac=a+c; bd=b+d; da=a-c; db=b-d
y0=ac+bd; y2=ac-bd; y1=da+j*db; y3=da-j*db
```

代码在相应int16节点做饱和运算，前两层对非零分支乘Q15因子，最后一层无非平凡
旋转系数。实虚分离复乘为`real=ar*wr+ai*(-wi)`、`imag=ar*wi+ai*wr`，用widening
multiply/MAC及vnclip，免去packed提取和重新交错。

前两层按`dst[4*n+branch]=y_branch[n]`跨距写入临时数组，让下一层四组输入连续。
临时数组共512字节、64B对齐；末层做固定4×4 lane转置，再用vsseg2e16交错写回。
这些栈访存属于算法实现成本，包含在计时中。

历史strided访存版本约311.8ns，segment load/store版约232.6ns。indexed scatter
虽删除末端gather，却退化到约331.2ns，因此最终使用固定gather。该结果反映X100
访存执行特性，换硬件时应重新测量。

### 14.5 选择实现与继续优化

v2适合要求旧定点输出逐位一致的场景；v3正常幅度误差较小，高幅可能因饱和路径
不同产生大差异，替换应由真实链路要求决定。幅度2048随机验证通过不是所有该
幅度向量无饱和的数学证明。
v2历史上Clang受益于内联和减少栈溢出；v3原生数据流中GCC更合适，GCC15/16在
本次测试近乎持平。不同版本的编译器结论不能互相外推。
继续优化时保留基准卡带，在新卡带上修改布局或算术，先数学参考/差分/压力报告，
再以相同输入和计时规则评价。修改SIMDe的B线只属历史背景，当前不维护运行目标。

## 15. 终端呈现接口（2026-10-16）

`tests/bench.c`的`pretty`全局开关仅控制stdout展示；`use_color`由命令行选项、
`isatty(STDOUT_FILENO)`、TERM和NO_COLOR共同决定。`section`负责空行/青色标题，
`status_color`按PASS/FAIL/REPORT选择绿/红/黄，所有样式即时复位。
`--output pretty|raw`、`--color auto|always|never`由main解析并拒绝未知值（退出2）。
raw分支保留原环境行、逐例行、summary和RESULT格式，CSV的schema和写入分支不变。

Make以`OUTPUT`、`COLOR`转发到ARGS，默认pretty/auto；`V=1`取消`.SILENT`，
恢复编译/运行命令回显；递归make不再输出Entering/Leaving directory。
旧测试二进制不接收新ARGS，保持其原始接口。所有格式化位于批次计时区间之外；
计时器、热身10000次、每轮调用次数及排序/中位数计算不变。
`package-check`只将sha256sum改为`--quiet -c`，校验成功后打印短汇总，失败时
make停止，不会误打印PASS。包外`.zip.sha256`校验归档本身，包内SHA256SUMS
校验静态成员；重新打包必须先重新生成成员清单、再计算最终ZIP的sidecar。
