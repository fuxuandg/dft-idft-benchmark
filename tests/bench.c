#define _GNU_SOURCE
#include "bench_module.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/utsname.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef BUILD_CC
#define BUILD_CC "unknown"
#endif
#ifndef BUILD_FLAGS
#define BUILD_FLAGS "unknown"
#endif
typedef struct {
  size_t n;
  int inverse, cpu, scale, mag, reps, cases;
  uint32_t seed;
  uint64_t iters;
  const char *signal, *mode, *golden, *csv;
} options;
static uint32_t rng;
static volatile int16_t sink;
/* Presentation is outside timed regions; raw output and CSV remain stable. */
static int pretty = 1, use_color;
static const char *style(const char *code) { return use_color ? code : ""; }
static void section(const char *title) {
  if(pretty) printf("\n%s== %s ==%s\n",style("\033[1;36m"),title,style("\033[0m"));
}
static const char *status_color(const char *status) {
  return style(!strcmp(status,"PASS") ? "\033[1;32m" :
               !strcmp(status,"REPORT") ? "\033[1;33m" : "\033[1;31m");
}
static uint32_t random32(void) {
  uint32_t r = rng;
  r ^= r << 13; r ^= r >> 17; r ^= r << 5;
  return rng = r;
}
static uint64_t now_ns(void) {
  struct timespec t;
  if (clock_gettime(CLOCK_MONOTONIC_RAW, &t)) { perror("clock_gettime"); exit(2); }
  return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static void *aligned_buffer(size_t bytes) {
  void *p = NULL;
  if (posix_memalign(&p, 64, bytes)) { fprintf(stderr,"aligned allocation failed\n"); exit(2); }
  memset(p, 0, bytes);
  return p;
}
static void input(int16_t *x, const options *o) {
  memset(x,0,2*o->n*sizeof(*x));
  for (size_t j=0;j<o->n;j++) {
    if (!strcmp(o->signal,"random")) {
      x[2*j]=(int16_t)((int)(random32()%(2u*(unsigned)o->mag+1))-o->mag);
      x[2*j+1]=(int16_t)((int)(random32()%(2u*(unsigned)o->mag+1))-o->mag);
    } else if (!strcmp(o->signal,"dc")) x[2*j]=x[2*j+1]=(int16_t)o->mag;
    else if (!strcmp(o->signal,"impulse")) { if(j==0) x[0]=(int16_t)o->mag; }
    else if (!strcmp(o->signal,"tone")) {
      double phi=2*M_PI*5*(double)j/(double)o->n;
      x[2*j]=(int16_t)lrint(o->mag*cos(phi)); x[2*j+1]=(int16_t)lrint(o->mag*sin(phi));
    } else if (!strcmp(o->signal,"sparse")) {
      /* The historical N64 shape, generalized to equally sized edge bands.
       * At default seed/mag this reproduces the original benchmark input. */
      for(int component=0;component<2;component++) {
        size_t k=2*j+(size_t)component, lanes=2*o->n;
        size_t band=lanes*36/128;
        if ((k>=2 && k<band) || k>=lanes-band)
          x[k]=(random32()&1)?(int16_t)-o->mag:(int16_t)o->mag;
      }
    }
  }
}
typedef struct { int max; size_t differing; double squared, energy; } errors;
static errors compare(const int16_t *a,const int16_t *b,size_t lanes) {
  errors e={0};
  for(size_t i=0;i<lanes;i++) {
    int d=(int)a[i]-(int)b[i];
    if(d) e.differing++;
    if(abs(d)>e.max)e.max=abs(d);
    e.squared+=(double)d*d; e.energy+=(double)b[i]*b[i];
  }
  return e;
}
static double nrmse(errors e) { return e.energy ? 100*sqrt(e.squared/e.energy) : (e.squared ? INFINITY : 0); }
static int pin(int cpu) {
  if(cpu<0)return 0;
  if(cpu>=CPU_SETSIZE){fprintf(stderr,"CPU index out of range\n");return -1;}
  cpu_set_t set;CPU_ZERO(&set);CPU_SET(cpu,&set);
  if(sched_setaffinity(0,sizeof(set),&set)){perror("sched_setaffinity");return -1;}
  return 0;
}
static void environment(const options *o) {
  struct utsname u;uname(&u);
  if(pretty) {
    section("IDFT / DFT benchmark");
    printf("  Module      : %s%s%s\n  Contract    : %s\n",style("\033[1m"),selected_module.name,style("\033[0m"),selected_module.contract);
    printf("  Mode        : %s\n  Transform   : %s | N=%zu | scale=%d\n",o->mode,o->inverse?"IDFT":"DFT",o->n,o->scale);
    printf("  Compiler    : %s\n  Flags       : %s\n  System      : %s %s (%s)\n",BUILD_CC,BUILD_FLAGS,u.sysname,u.release,u.machine);
    printf("  CPU         : %d (-1 = unpinned) | alignment=64 bytes\n  Input       : seed=%"PRIu32" | signal=%s | mag=%d\n",o->cpu,o->seed,o->signal,o->mag);
  } else {
  printf("module=%s contract=%s\n",selected_module.name,selected_module.contract);
  printf("system=%s %s arch=%s compiler=%s flags=%s\n",u.sysname,u.release,u.machine,BUILD_CC,BUILD_FLAGS);
  printf("transform=%s points=%zu scale=%d cpu=%d alignment=64 seed=%"PRIu32" signal=%s mag=%d\n",
         o->inverse?"idft":"dft",o->n,o->scale,o->cpu,o->seed,o->signal,o->mag);
  }
  if(o->cpu>=0) {
    char p[160],value[128];
    snprintf(p,sizeof(p),"/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq",o->cpu);
    FILE *f=fopen(p,"r");if(f){if(fgets(value,sizeof(value),f))printf(pretty?"  CPU kHz     : %s":"cpu_frequency_khz=%s",value);fclose(f);}
  }
#ifdef __riscv
  unsigned long vlenb;__asm__ volatile("csrr %0, vlenb":"=r"(vlenb));
  printf(pretty?"  RVV         : vlenb=%lu bytes\n":"vlenb=%lu\n",vlenb);
#endif
}
static int parse_words(char *line,int16_t *out,size_t n) {
  char *p=line+1;
  for(size_t k=0;k<n;k++) {
    while(*p==' '||*p=='\t')p++;
    errno=0;char *end;long v=strtol(p,&end,10);
    if(end==p||errno||v<INT16_MIN||v>INT16_MAX)return -1;
    out[k]=(int16_t)v;p=end;
  }
  while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')p++;
  return *p ? -1 : 0;
}
static int golden_check(const options *o) {
  if(o->n!=64){fprintf(stderr,"golden suite currently supports N64 only; no new goldens generated\n");return 2;}
  FILE *f=fopen(o->golden,"r");if(!f){perror(o->golden);return 2;}
  int16_t *x=aligned_buffer(128*sizeof(*x)),*want=aligned_buffer(128*sizeof(*x)),*got=aligned_buffer(128*sizeof(*x));
  char *line=NULL;size_t capacity=0;char fn[32],signal[32];int mag=0,scale=0,state=0,selected=0;
  int count=0,headers=0,declared=-1,pass=0,fail=0,report=0,malformed=0;
  section("Golden correctness");
  if(pretty) printf("  %-6s %-7s %-8s %6s %5s %6s %7s %10s %10s\n","STATUS","FN","SIGNAL","MAG","SCALE","DIFF","MAX ABS","RMSE","NRMSE %");
  while(getline(&line,&capacity,f)>=0) {
    if(line[0]=='#'||line[0]=='\n')continue;
    if(sscanf(line,"NCASES %d",&declared)==1)continue;
    if(!strncmp(line,"CASE ",5)) {
      if(state){malformed=1;break;}
      if(sscanf(line+5,"%31s %31s %d %d",fn,signal,&mag,&scale)!=4||scale<0||scale>255){malformed=1;break;}
      headers++;state=1;
      selected=!strcmp(fn,o->inverse?"idft64":"dft64");continue;
    }
    if(line[0]=='X'&&state==1){if(parse_words(line,x,128)){malformed=1;break;}state=2;continue;}
    if(line[0]=='Y'&&state==2) {
      if(parse_words(line,want,128)){malformed=1;break;}state=0;
      if(!selected)continue;
      selected_module.run(x,got,(unsigned char)scale);errors e=compare(got,want,128);count++;
      int stress=selected_module.approximate&&((!strcmp(signal,"random")&&mag==32767)||
                 ((!strcmp(signal,"dc")||!strcmp(signal,"tone"))&&mag==4096));
      int tol=selected_module.approximate?(scale?2:16):0;
      int ok=e.max<=tol&&(!selected_module.approximate||nrmse(e)<0.5);
      if(stress)report++;else if(ok)pass++;else fail++;
      const char *status=stress?"REPORT":ok?"PASS":"FAIL";
      if(pretty) printf("  %s%-6s%s %-7s %-8s %6d %5d %6zu %7d %10.6f %10.6f\n",status_color(status),status,style("\033[0m"),fn,signal,mag,scale,e.differing,e.max,sqrt(e.squared/128),nrmse(e));
      else printf("%s golden fn=%s signal=%s mag=%d scale=%d different=%zu max_abs=%d rmse=%.6f nrmse_pct=%.6f\n",
          stress?"REPORT":ok?"PASS":"FAIL",fn,signal,mag,scale,e.differing,e.max,sqrt(e.squared/128),nrmse(e));
      continue;
    }
    malformed=1;break;
  }
  if(ferror(f)||state||count==0||declared!=headers)malformed=1;
  if(pretty) {
    const char *status=malformed||fail?"FAIL":"PASS";
    printf("\n  %s[%s]%s Checked: %d | Pass: %d | Fail: %d | Report: %d | Malformed: %d\n\n",status_color(status),status,style("\033[0m"),count,pass,fail,report,malformed);
    if(report) puts("  REPORT = stress input, reported only; not a correctness pass.\n");
  } else printf("golden summary: checked=%d pass=%d fail=%d report=%d malformed=%d\n",count,pass,fail,report,malformed);
  free(line);fclose(f);free(x);free(want);free(got);return malformed?2:fail?1:0;
}
static int random_check(const options *o) {
  int16_t *x=aligned_buffer(2*o->n*sizeof(*x)),*got=aligned_buffer(2*o->n*sizeof(*x)),*want=aligned_buffer(2*o->n*sizeof(*x));
  const int mags[]={364,2048,4095,32767};int failed=0;
  section("Random differential correctness");
  if(pretty) printf("  %-6s %6s %8s %10s %10s %7s %10s %10s\n","STATUS","MAG","CASES","LANES","DIFF","MAX ABS","RMSE","NRMSE %");
  for(size_t m=0;m<sizeof(mags)/sizeof(mags[0]);m++) {
    errors all={0};options in=*o;in.signal="random";in.mag=mags[m];rng=o->seed^(uint32_t)in.mag;if(!rng)rng=1;
    for(int c=0;c<o->cases;c++) {
      input(x,&in);selected_module.run(x,got,(unsigned char)o->scale);reference_module.run(x,want,(unsigned char)o->scale);
      errors e=compare(got,want,2*o->n);if(e.max>all.max)all.max=e.max;
      all.differing+=e.differing;all.squared+=e.squared;all.energy+=e.energy;
    }
    int report=selected_module.approximate&&in.mag>2048;
    int tol=selected_module.approximate?(o->scale?4:32):0;
    int ok=all.max<=tol&&(!selected_module.approximate||nrmse(all)<0.5);
    const char *status=report?"REPORT":ok?"PASS":"FAIL";
    if(pretty) printf("  %s%-6s%s %6d %8d %10zu %10zu %7d %10.6f %10.6f\n",status_color(status),status,style("\033[0m"),in.mag,o->cases,2*o->n*(size_t)o->cases,all.differing,all.max,sqrt(all.squared/(2*o->n*(size_t)o->cases)),nrmse(all));
    else printf("%s random mag=%d cases=%d lanes=%zu different=%zu max_abs=%d rmse=%.6f nrmse_pct=%.6f\n",
      report?"REPORT":ok?"PASS":"FAIL",in.mag,o->cases,2*o->n*(size_t)o->cases,all.differing,all.max,
      sqrt(all.squared/(2*o->n*(size_t)o->cases)),nrmse(all));
    if(!report&&!ok)failed=1;
  }
  if(pretty) printf("\n  %s[%s]%s Random differential gate (REPORT rows excluded).\n\n",status_color(failed?"FAIL":"PASS"),failed?"FAIL":"PASS",style("\033[0m"));
  free(x);free(got);free(want);return failed;
}
static int math_check(const options *o) {
  /* N64 is the only numerical contract currently established.  Larger
   * cartridges must explicitly register their gain policy before validation. */
  if(o->n!=64){fprintf(stderr,"double-reference gain policy established only for N64\n");return 2;}
  int16_t *x=aligned_buffer(2*o->n*sizeof(*x)),*got=aligned_buffer(2*o->n*sizeof(*x));
  options in=*o;in.signal="random";in.mag=364;rng=o->seed?o->seed:1;double sum=0,energy=0,max_error=0;
  for(int c=0;c<8;c++) {
    input(x,&in);selected_module.run(x,got,(unsigned char)o->scale);
    for(size_t k=0;k<o->n;k++) {
      double re=0,im=0;
      for(size_t j=0;j<o->n;j++) {
        double phi=(o->inverse?1:-1)*2*M_PI*(double)k*(double)j/(double)o->n;
        re+=x[2*j]*cos(phi)-x[2*j+1]*sin(phi);im+=x[2*j]*sin(phi)+x[2*j+1]*cos(phi);
      }
      /* The original DFT scale=0 gain is 1/4, unlike IDFT gain=1. */
      double gain=o->scale?1.0/8.0:(o->inverse?1.0:1.0/4.0);re*=gain;im*=gain;
      double dr=got[2*k]-re,di=got[2*k+1]-im;
      sum+=dr*dr+di*di;energy+=re*re+im*im;
      if(fabs(dr)>max_error)max_error=fabs(dr);
      if(fabs(di)>max_error)max_error=fabs(di);
    }
  }
  double evm=100*sqrt(sum/energy);int ok=evm<1;
  if(pretty) {
    section("Independent double reference");
    printf("  Trials      : 8 | mag=364\n  EVM RMS     : %.6f %% (required < 1 %%)\n  Max abs     : %.6f\n\n  %s[%s]%s Mathematical accuracy gate\n\n",evm,max_error,status_color(ok?"PASS":"FAIL"),ok?"PASS":"FAIL",style("\033[0m"));
  } else printf("%s independent-double trials=8 mag=364 evm_rms_pct=%.6f max_abs=%.6f criterion=1_percent\n",ok?"PASS":"FAIL",evm,max_error);
  free(x);free(got);return !ok;
}
static int cmp_double(const void *a,const void *b){double x=*(const double*)a,y=*(const double*)b;return(x>y)-(x<y);}
static int benchmark(const options *o) {
  int16_t *x=aligned_buffer(2*o->n*sizeof(*x)),*y=aligned_buffer(2*o->n*sizeof(*x));
  double *times=calloc((size_t)o->reps,sizeof(*times));if(!times){perror("calloc");return 2;}
  rng=UINT32_C(0x12345678)^(uint32_t)o->mag^(o->seed-1);if(!rng)rng=1;input(x,o);
  uint64_t checksum=UINT64_C(1469598103934665603);
  for(size_t i=0;i<2*o->n;i++){checksum^=(uint16_t)x[i];checksum*=UINT64_C(1099511628211);}
  section("Performance / batch timing");
  if(pretty) printf("  Warmup      : 10000 calls\n  Measurement : %"PRIu64" calls x %d rounds\n\n  %7s %16s\n",o->iters,o->reps,"ROUND","ns / call");
  for(int i=0;i<10000;i++)selected_module.run(x,y,(unsigned char)o->scale);
  /* one bracket per batch, amortizing clock_gettime to <0.001 ns at defaults */
  for(int r=0;r<o->reps;r++) {
    __asm__ volatile("" ::: "memory");uint64_t start=now_ns();
    for(uint64_t i=0;i<o->iters;i++)selected_module.run(x,y,(unsigned char)o->scale);
    uint64_t stop=now_ns();__asm__ volatile("" ::: "memory");
    sink^=y[r%(2*o->n)];times[r]=(double)(stop-start)/(double)o->iters;
    if(pretty) printf("  %3d / %-3d %16.6f\n",r+1,o->reps,times[r]);
    else printf("round=%d ns_per_call=%.6f\n",r+1,times[r]);
  }
  qsort(times,(size_t)o->reps,sizeof(*times),cmp_double);
  double median=o->reps%2?times[o->reps/2]:(times[o->reps/2-1]+times[o->reps/2])/2;
  if(pretty) {
    section("Performance result");
    printf("  Min         : %.6f ns / call\n  %sMedian      : %.6f ns / call%s\n  Max         : %.6f ns / call\n  Input hash  : %016"PRIx64"\n",times[0],style("\033[1;36m"),median,style("\033[0m"),times[o->reps-1],checksum);
  } else printf("RESULT module=%s min_ns=%.6f median_ns=%.6f max_ns=%.6f input_hash=%016"PRIx64"\n",
       selected_module.name,times[0],median,times[o->reps-1],checksum);
  if(o->csv) {
    FILE *f=fopen(o->csv,"a+");if(!f){perror(o->csv);free(x);free(y);free(times);return 2;}
    fseek(f,0,SEEK_END);if(ftell(f)==0)fprintf(f,"unix_time,module,compiler,flags,transform,points,scale,cpu,seed,signal,mag,iters,reps,input_hash,min_ns,median_ns,max_ns\n");
    fprintf(f,"%lld,%s,\"%s\",\"%s\",%s,%zu,%d,%d,%"PRIu32",%s,%d,%"PRIu64",%d,%016"PRIx64",%.6f,%.6f,%.6f\n",
      (long long)time(NULL),selected_module.name,BUILD_CC,BUILD_FLAGS,o->inverse?"idft":"dft",o->n,o->scale,o->cpu,o->seed,o->signal,o->mag,o->iters,o->reps,checksum,times[0],median,times[o->reps-1]);
    if(fclose(f)){perror("csv close");return 2;}
    if(pretty) printf("  CSV saved   : %s (append)\n",o->csv);
  }
  if(pretty) puts("");
  free(x);free(y);free(times);return 0;
}
static void help(void) {
  puts("bench --mode bench|golden|random|math --transform idft|dft --size N --cpu N\n"
       "      --iters N --reps N --seed N --scale N --mag N --signal sparse|random|dc|impulse|tone\n"
       "      --cases N --golden FILE --csv FILE --output pretty|raw --color auto|always|never\n"
       "Exit 0=passed, 1=numerical failure, 2=usage/unsupported/error. REPORT is never PASS.");
}
static uint64_t number(const char *s) {
  errno=0;char *end;unsigned long long n=strtoull(s,&end,10);
  if(!*s||*s=='-'||errno||*end){fprintf(stderr,"invalid nonnegative number: %s\n",s);exit(2);}return (uint64_t)n;
}
int main(int argc,char **argv) {
  const char *color_mode="auto";
  options o={64,1,2,1,364,7,2000,1,100000,"sparse","bench","reference/x86_golden.txt",NULL};
  for(int i=1;i<argc;i++) {
    const char *a=argv[i];if(!strcmp(a,"--help")){help();return 0;}
    if(i+1>=argc){help();return 2;}const char *v=argv[++i];
    if(!strcmp(a,"--mode"))o.mode=v;
    else if(!strcmp(a,"--transform")){if(strcmp(v,"idft")&&strcmp(v,"dft"))return 2;o.inverse=!strcmp(v,"idft");}
    else if(!strcmp(a,"--size"))o.n=(size_t)number(v);
    else if(!strcmp(a,"--cpu")){if(!strcmp(v,"-1"))o.cpu=-1;else {uint64_t n=number(v);if(n>=CPU_SETSIZE)return 2;o.cpu=(int)n;}}
    else if(!strcmp(a,"--iters"))o.iters=number(v);
    else if(!strcmp(a,"--seed")){uint64_t n=number(v);if(!n||n>UINT32_MAX)return 2;o.seed=(uint32_t)n;}
    else if(!strcmp(a,"--scale")){uint64_t n=number(v);if(n>255)return 2;o.scale=(int)n;}
    else if(!strcmp(a,"--mag")){uint64_t n=number(v);if(n>32767)return 2;o.mag=(int)n;}
    else if(!strcmp(a,"--reps")){uint64_t n=number(v);if(!n||n>10000)return 2;o.reps=(int)n;}
    else if(!strcmp(a,"--cases")){uint64_t n=number(v);if(!n||n>1000000)return 2;o.cases=(int)n;}
    else if(!strcmp(a,"--signal"))o.signal=v;
    else if(!strcmp(a,"--golden"))o.golden=v;
    else if(!strcmp(a,"--csv"))o.csv=v;
    else if(!strcmp(a,"--output")){if(strcmp(v,"pretty")&&strcmp(v,"raw"))return 2;pretty=!strcmp(v,"pretty");}
    else if(!strcmp(a,"--color")){if(strcmp(v,"auto")&&strcmp(v,"always")&&strcmp(v,"never"))return 2;color_mode=v;}
    else {fprintf(stderr,"unknown option %s\n",a);return 2;}
  }
  const char *term=getenv("TERM");
  use_color=pretty&&(!strcmp(color_mode,"always")||(!strcmp(color_mode,"auto")&&isatty(STDOUT_FILENO)&&!getenv("NO_COLOR")&&term&&strcmp(term,"dumb")));
  if(!o.iters||!o.n||o.n>98304||pin(o.cpu))return 2;
  if(strcmp(o.signal,"sparse")&&strcmp(o.signal,"random")&&strcmp(o.signal,"dc")&&strcmp(o.signal,"tone")&&strcmp(o.signal,"impulse"))return 2;
  if(selected_module.setup(o.n,o.inverse)) {
    fprintf(stderr,"unsupported cartridge=%s transform=%s points=%zu\n",selected_module.name,o.inverse?"idft":"dft",o.n);return 2;
  }
  if(reference_module.setup(o.n,o.inverse))return 2;
  environment(&o);
  if(!strcmp(o.mode,"golden"))return golden_check(&o);
  if(!strcmp(o.mode,"random"))return random_check(&o);
  if(!strcmp(o.mode,"math"))return math_check(&o);
  if(!strcmp(o.mode,"bench"))return benchmark(&o);
  fprintf(stderr,"unknown mode %s\n",o.mode);return 2;
}
