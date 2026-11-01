// The AmiCradle Team. Reads qemu -d in_asm,exec,nochain from stdin, prints executed guest instructions.
// Counts executed guest instructions from qemu -d in_asm,exec,nochain on stdin.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define HN (1u<<22)
static uint32_t key[HN]; static uint32_t val[HN]; static unsigned long long hits[HN];
static uint32_t *slot(uint32_t pc){uint32_t h=(pc*2654435761u)&(HN-1);while(key[h]&&key[h]!=pc)h=(h+1)&(HN-1);key[h]=pc;return &val[h];}
int main(){char line[512];uint32_t inpc=0,inn=0;int inblk=0;unsigned long long total=0,tbs=0;
 while(fgets(line,sizeof line,stdin)){
  if(!strncmp(line,"IN:",3)){inblk=1;inpc=0;inn=0;continue;}
  if(inblk){ if(line[0]=='0'&&line[1]=='x'){uint32_t pc=strtoul(line,0,16);if(!inn)inpc=pc;inn++;continue;}
    if(inn){*slot(inpc)=inn;} inblk=0; }
  if(!strncmp(line,"Trace",5)){char*p=strchr(line,'[');if(!p)continue;p=strchr(p,'/');uint32_t pc=strtoull(p+1,0,16);uint32_t*v=slot(pc);total+=*v;tbs++;hits[v-val]++;}
 }
 printf("%llu %llu\n",total,tbs); if(getenv("TOP")){FILE*o=fopen(getenv("TOP"),"w");for(uint32_t i=0;i<HN;i++)if(hits[i]>1000)fprintf(o,"%llu %08x %u\n",hits[i]*val[i],key[i],val[i]);fclose(o);}return 0;}
