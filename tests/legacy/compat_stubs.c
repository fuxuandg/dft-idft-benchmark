/* compat_stubs.c -- missing externals for oai_dfts.c's standalone (-DMR_MAIN) build.
 *
 * Why this file exists (2026-09-13, stage 1):
 *   The upstream standalone target in openair1/PHY/TOOLS/Makefile links only
 *   oai_dfts.c + time_meas.c + taus.c, but in this OAI snapshot that is not
 *   enough.  Three groups of symbols are missing:
 *
 *   1) exit_function / uniqCfg / T_stdout
 *      -> provided by OAI's own common/utils/minimal_stub.c, which the CMake
 *         build links as "minimal_lib" but the hand-written Makefile forgets.
 *      -> Same stubs are also inlined in the ARM twin: oai_dfts_neon.c:7294.
 *
 *   2) config_get / config_check_unknown_cmdlineopt
 *      -> normally from common/config/config_userapi.c (a heavy config module).
 *         They are only referenced from time_meas.c:init_meas(), which main()
 *         never calls, so a stub is enough.  oai_dfts_neon.c:7296 does the same.
 *
 *   3) fill_random
 *      -> defined in openair1/SIMULATION/TOOLS/rangen_double.c; the aarch64
 *         target links that file, the x86 target does not.
 *         This one IS really called: set_taus_seed(0) takes the random-seed
 *         branch, so the implementation below must be functionally identical
 *         to the upstream one.  It is copied verbatim in behaviour:
 *         openair1/SIMULATION/TOOLS/rangen_double.c:32.
 *
 * NOTE: oai_dfts.c itself is NOT modified.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

/* ---------- 1) minimal_lib equivalents (common/utils/minimal_stub.c) ---------- */
#ifndef T_TRACER
int T_stdout;
#endif

struct configmodule_interface_s;
struct configmodule_interface_s *uniqCfg = NULL;

void exit_function(const char *file, const char *function, const int line,
                   const char *s, const int assert) {
  if (assert) {
    abort();
  } else {
    exit(EXIT_SUCCESS);
  }
}

/* ---------- 2) config module stubs (mirror of oai_dfts_neon.c:7294) ---------- */
/* call site (time_meas.c:282): config_get(config_get_if(), params, num, SECTION) */
int config_get(void *cfgif, void *params, int numparams, const char *section) {
  (void)cfgif; (void)params; (void)numparams; (void)section;
  return 0;
}

int config_check_unknown_cmdlineopt(char *prefix) {
  (void)prefix;
  return 0;
}

/* ---------- 3) fill_random (behaviourally identical to rangen_double.c:32) ---------- */
void fill_random(void *buf, size_t sz) {
  const char *fn = "/dev/urandom";
  FILE *f = fopen(fn, "rb");
  if (f == NULL) {
    fprintf(stderr, "could not open %s for seed generation: %d %s\n",
            fn, errno, strerror(errno));
    abort();
  }
  size_t rc = fread(buf, sz, 1, f);
  if (rc < 1) {
    fprintf(stderr, "could not read %s for seed generation: %d %s\n",
            fn, errno, strerror(errno));
    abort();
  }
  fclose(f);
}

/* taus.c calls taus()/set_taus_seed() but there is no taus.h in this tree;
 * oai_dfts.c only gets implicit declarations.  Nothing to add here.
 */

/* ---------- 4) log stubs, needed WITHOUT -DMR_MAIN (added 2026-09-13) ----------
 *
 * WHY THIS GROUP EXISTS
 *   The official dispatch entry points
 *       dft_implementation()  (oai_dfts.c:7698)
 *       idft_implementation() (oai_dfts.c:7717)
 *   sit inside `#ifndef MR_MAIN` (7696 .. 7733).  So -DMR_MAIN and the
 *   dispatcher are MUTUALLY EXCLUSIVE -- and the standalone build defines
 *   MR_MAIN while the real library does not.  Since the benchmark must go
 *   through the dispatcher (it is what the pipeline uses), oai_dfts.c is
 *   compiled WITHOUT MR_MAIN, exactly like the library.
 *
 *   Two consequences:
 *     a) there is no main() at all (main is inside `#ifdef MR_MAIN`), so the
 *        -Dmain=oai_orig_main rename is no longer needed;
 *     b) the `#ifndef MR_MAIN` blocks inside the transforms come back:
 *            if (LOG_DUMPFLAG(DEBUG_DFT)) { LOG_M("dft128.m", ...); }
 *        These need a valid g_log to dereference and two functions to link.
 *
 *   LOG_DUMPFLAG(D) is `g_log->dump_mask.D`, and log_t is only ever reached
 *   through that pointer here.  Pointing g_log at a large ZERO-FILLED buffer
 *   makes every dump flag false, so none of those blocks ever executes; the
 *   exact layout of log_t therefore does not matter, only that the buffer is
 *   bigger than the struct.  The two functions exist purely to satisfy the
 *   linker.  */
static unsigned char g_log_zero[8192] __attribute__((aligned(64)));  /* .bss => all zero */
void *g_log = g_log_zero;

int32_t write_file_matlab(const char *fname, const char *vname, void *data,
                          int length, int dec, unsigned int format, int multiVec) {
  (void)fname; (void)vname; (void)data; (void)length;
  (void)dec; (void)format; (void)multiVec;
  return 0;
}

void logRecord_mt(const char *file, const char *func, int line, int comp,
                  int level, const char *format, ...) {
  (void)file; (void)func; (void)line; (void)comp; (void)level; (void)format;
}
