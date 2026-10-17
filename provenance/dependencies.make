gcc -O2 -std=gnu99 -march=rv64gcv -DNB_ANTENNAS_RX=1 -DNB_ANTENNAS_TX=1 -DMAX_NUM_CCs=1 -DOAI_DISABLE_WRITE_OUTPUT_MACRO -I/home/ubuntu/sionna-rk/ext/openairinterface5g -I/home/ubuntu/sionna-rk/ext/openairinterface5g/openair1 -I/home/ubuntu/sionna-rk/ext/openairinterface5g/openair1/PHY/TOOLS -I/home/ubuntu/sionna-rk/ext/openairinterface5g/targets -I/home/ubuntu/sionna-rk/ext/openairinterface5g/targets/COMMON -I/home/ubuntu/sionna-rk/ext/openairinterface5g/radio/COMMON -I/home/ubuntu/sionna-rk/ext/openairinterface5g/openair2 -I/home/ubuntu/sionna-rk/ext/openairinterface5g/openair2/COMMON -I/home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils -I/home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/T -I/home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/msc -I/home/ubuntu/sionna-rk/ext/openairinterface5g/executables -I/home/ubuntu/sionna-rk/ext/openairinterface5g/nfapi/open-nFAPI/nfapi/public_inc -MM /home/ubuntu/sionna-rk/ext/openairinterface5g/openair1/PHY/TOOLS/oai_dfts.c /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/time_meas.c /home/ubuntu/sionna-rk/ext/openairinterface5g/openair1/SIMULATION/TOOLS/taus.c harness.c compat_stubs.c
oai_dfts.o: \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/openair1/PHY/TOOLS/oai_dfts.c \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/time_meas.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/LOG/log.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/utils.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/assertions.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/platform_types.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/openair1/PHY/TOOLS/../sse_intrin.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/assertions.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/openair1/PHY/TOOLS/tools_defs.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/openair1/PHY/sse_intrin.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/LOG/log.h
time_meas.o: \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/time_meas.c \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/time_meas.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/assertions.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/platform_types.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/config/config_userapi.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/config/config_load_configmodule.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/config/config_paramdesc.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/T/T.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/utils.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/assertions.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/threadPool/notified_fifo.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/threadPool/pthread_utils.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/time_meas.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/assertions.h
taus.o: \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/openair1/SIMULATION/TOOLS/taus.c \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/openair1/SIMULATION/TOOLS/sim.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/openair1/PHY/defs_common.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/radio/COMMON/common_lib.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/openair1/PHY/TOOLS/tools_defs.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/openair1/PHY/sse_intrin.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/assertions.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/platform_types.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/assertions.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/utils.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/LOG/log.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/radio/COMMON/record_player.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/config/config_paramdesc.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/config/config_userapi.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/config/config_load_configmodule.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/T/T.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/threadPool/notified_fifo.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/threadPool/pthread_utils.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/time_meas.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/executables/softmodem-common.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/config/config_load_configmodule.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/openair1/PHY/TOOLS/tools_defs.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/openairinterface5g_limits.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/openair1/PHY/types.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/nfapi/open-nFAPI/nfapi/public_inc/nfapi_interface.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/nfapi/open-nFAPI/nfapi/public_inc/nfapi_common_interface.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/platform_constants.h
harness.o: harness.c \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/openair1/PHY/TOOLS/tools_defs.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/openair1/PHY/sse_intrin.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/assertions.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/platform_types.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/assertions.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/utils.h \
 /home/ubuntu/sionna-rk/ext/openairinterface5g/common/utils/LOG/log.h
compat_stubs.o: compat_stubs.c
