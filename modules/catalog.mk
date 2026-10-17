# A new cartridge registers one source, public symbol and numerical contract.
# Keep size support in its adapter: unsupported combinations must fail setup.
CARTRIDGES := v1 v2 v3 oai
SOURCE_v1 := modules/v1/idft64_rvv.c
SOURCE_v2 := modules/v2/idft64_rvv.c
SOURCE_v3 := modules/v3/idft64_rvv_native.c
SYMBOL_v1 := kernel_v1
SYMBOL_v2 := kernel_v2
SYMBOL_v3 := kernel_v3
ENTRY_v1 := idft64_rvv
ENTRY_v2 := idft64_rvv
ENTRY_v3 := idft64_rvv
ADAPTER_v1 := modules/rvv_adapter.c
ADAPTER_v2 := modules/rvv_adapter.c
ADAPTER_v3 := modules/rvv_adapter.c
POINTS_v1 := 64
POINTS_v2 := 64
POINTS_v3 := 64
INVERSE_v1 := 1
INVERSE_v2 := 1
INVERSE_v3 := 1
APPROX_v1 := 0
APPROX_v2 := 0
APPROX_v3 := 1
