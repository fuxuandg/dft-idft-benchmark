# 冻结依赖

- oai/：27个实际源码/头文件及LICENSE、NOTICE.md，来自K3上的OAI工作树。
  包内oai_dfts.c与上游HEAD的差异仅为RISC-V架构guard；time_meas.h的RISC-V
  时钟适配在provenance/oai-portability.patch中记录。其余复制文件如有本地差异
  由来源清单和冻结文件hash溯源。
- simde-standard/simde/：完整SIMDe 0.8.4头文件树，来自已保存clean备份。
  各文件保留原版权和SPDX/MIT或其他许可声明，不删除相关头部。
  标准版本与系统版本avx2.h/sse2.h hash不同，具体证据见provenance。

标准库、Linux头文件、编译器及make由运行环境提供。构建命令把包内SIMDe include
置于系统搜索路径之前，并显式SIMDE_NO_NATIVE，标准头文件本身没有实验补丁。
这一选择冻结了SIMDe可移植路径，编译器的自动向量化仍可能影响最终汇编。
