/* Local Android integration test, 2026. Released under the libpng license. */
#include "../../pngpriv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <unistd.h>
#include <dlfcn.h>

static unsigned long choices[3][3], ties[3][3];
static unsigned bucket;
static unsigned rng = 0x061b2026U;
static unsigned random_byte(void) {
   rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
   return rng & 255;
}
static unsigned paeth(unsigned a, unsigned b, unsigned c) {
   int p = (int)a + (int)b - (int)c;
   int pa = abs(p-(int)a), pb = abs(p-(int)b), pc = abs(p-(int)c);
   unsigned k=pa <= pb && pa <= pc ? 0 : pb <= pc ? 1 : 2;
   ++choices[bucket][k]; if(pa==pb && pa<=pc)++ties[bucket][0];if(pa==pc && pa<=pb)++ties[bucket][1];if(pb==pc && pb<=pa)++ties[bucket][2];
   return k==0?a:k==1?b:c;
}
static void reference(unsigned char *r, const unsigned char *p,
                      size_t n, unsigned bpp, int f) {
   size_t j;
   for (j=0; j<n; ++j) {
      unsigned a=j>=bpp?r[j-bpp]:0, b=p[j], c=j>=bpp?p[j-bpp]:0;
      unsigned v=f==1?a:f==2?b:f==3?(a+b)/2:f==4?paeth(a,b,c):0;
      r[j]=(unsigned char)(r[j]+v);
   }
}
static void maps(void) {
   char line[1024]; FILE *fp=fopen("/proc/self/maps","r");
   puts("MAPS_BEGIN");
   if (!fp) exit(90);
   while (fgets(line,sizeof line,fp)) fputs(line,stdout);
   fclose(fp); puts("MAPS_END");
}
int main(void) {
   static const unsigned required_widths[]={1,2,3,4,7,8,15,16,17,31,32,33,63,64,65,127,128};
   static const unsigned formats[][3]={
      {0,1,1},{0,2,1},{0,4,1},{0,8,1},{0,16,1},
      {2,8,3},{2,16,3},{3,1,1},{3,2,1},{3,4,1},{3,8,1},
      {4,8,2},{4,16,2},{6,8,4},{6,16,4}};
   unsigned widths[40];size_t nw, k;
   size_t page=(size_t)sysconf(_SC_PAGESIZE), fi, wi, j;
   unsigned long cases=0; int pat, first, f;
   unsigned char *mr=mmap(NULL,page*3,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
   unsigned char *mp=mmap(NULL,page*3,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
   unsigned char expected[2048], before[2048], prevcopy[2048];
   if(mr==MAP_FAILED || mp==MAP_FAILED || mprotect(mr+page,page,PROT_READ|PROT_WRITE) || mprotect(mp+page,page,PROT_READ|PROT_WRITE)) return 91;
   printf("HWCAP=%#lx SEED=0x061b2026\n",getauxval(AT_HWCAP)); maps();
   for(fi=0;fi<sizeof formats/sizeof formats[0];++fi) {
      unsigned depth=formats[fi][1]*formats[fi][2], bpp=(depth+7)/8;
      bucket=bpp==3?0:bpp==4?1:2;
      nw=sizeof required_widths/sizeof required_widths[0];
      memcpy(widths,required_widths,sizeof required_widths);
      for(k=8;k<=64;k*=2) {
         unsigned q=(unsigned)(k*8/depth), v;
         for(v=q>1?q-1:1;v<=q+1;++v) {
            size_t z;for(z=0;z<nw && widths[z]!=v;++z) {}
            if(z==nw)widths[nw++]=v;
         }
      }
      for(wi=0;wi<nw;++wi) {
         size_t n=(widths[wi]*depth+7)/8;
         /* Same allocation size formula as png_read_start_row. Place its end
          * at the guard page; align pixel data, retaining >=16 tail bytes. */
         size_t allocation=(((widths[wi]+7)&~7U)*depth+7)/8+1+bpp+48;
         unsigned char *base=mr+2*page-allocation, *pbase=mp+2*page-allocation;
         unsigned char *row=(unsigned char *)((uintptr_t)(base+32)&~(uintptr_t)15);
         unsigned char *prev=(unsigned char *)((uintptr_t)(pbase+32)&~(uintptr_t)15);
         png_row_info ri;
         memset(&ri,0,sizeof ri); ri.width=widths[wi];ri.rowbytes=n;
         ri.color_type=formats[fi][0];ri.bit_depth=formats[fi][1];
         ri.channels=formats[fi][2];ri.pixel_depth=depth;
         for(pat=0;pat<5;++pat) for(first=0;first<2;++first) for(f=0;f<5;++f) {
            png_structp pp=png_create_read_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
            if(!pp) return 92;
            pp->pixel_depth=(png_byte)depth;
            memset(base,0xa5,allocation);memset(pbase,0x5a,allocation);
            for(j=0;j<n;++j) {
               row[j]=pat==0?0:pat==1?255:pat==2?(j&1?255:0):pat==3?(unsigned char)j:random_byte();
               prev[j]=first?0:pat==0?0:pat==1?255:pat==2?(j&1?0:255):pat==3?(unsigned char)(3*j+11):random_byte();
            }
            memcpy(expected,row,n);memcpy(before,base,allocation);memcpy(prevcopy,pbase,allocation);
            reference(expected,prev,n,bpp,f);
            png_read_filter_row(pp,&ri,row,prev,f);
            if(memcmp(expected,row,n)) {
               for(j=0;j<n && expected[j]==row[j];++j) {}
               fprintf(stderr,"FAIL color=%u depth=%u width=%u pattern=%d first=%d filter=%d byte=%zu expected=%u actual=%u\n",formats[fi][0],formats[fi][1],widths[wi],pat,first,f,j,expected[j],row[j]);return 1;
            }
            if(memcmp(base,before,(size_t)(row-base)) || memcmp(row+n,before+(row-base)+n,allocation-(row-base)-n) || memcmp(pbase,prevcopy,allocation)) {fprintf(stderr,"GUARD_FAIL color=%u depth=%u width=%u pattern=%d first=%d filter=%d\n",formats[fi][0],formats[fi][1],widths[wi],pat,first,f);return 2;}
            if(wi==0 && pat==0 && first==0 && f==4) {
               int k; for(k=0;k<4;++k) {
                  Dl_info info; memset(&info,0,sizeof info);
                  if(!dladdr((void *)(uintptr_t)pp->read_filter[k],&info)) return 3;
                  printf("DISPATCH pixel_depth=%u bpp=%u filter=%d address=%p base=%p offset=%#lx owner=%s\n",depth,bpp,k+1,(void *)(uintptr_t)pp->read_filter[k],info.dli_fbase,(unsigned long)((uintptr_t)pp->read_filter[k]-(uintptr_t)info.dli_fbase),info.dli_fname);
               }
            }
            png_destroy_read_struct(&pp,NULL,NULL);++cases;
         }
      }
   }
   /* Distinct predictions exercise a/c and b/c minimum ties. All-three
    * ties necessarily have equal predictions. Legal two-pixel RGB/RGBA rows. */
   {
      static const unsigned triples[][4]={{3,0,0,3},{0,3,0,3},{0,2,1,1},
         {0,3,2,0},{3,0,2,0},{7,7,7,7}};
      unsigned bpp; size_t t;
      for(bpp=3;bpp<=4;++bpp) for(t=0;t<6;++t) {
         size_t n=2*bpp, allocation=8*bpp+1+bpp+48;
         unsigned char *base=mr+2*page-allocation,*pbase=mp+2*page-allocation;
         unsigned char *row=(unsigned char *)((uintptr_t)(base+32)&~(uintptr_t)15);
         unsigned char *prev=(unsigned char *)((uintptr_t)(pbase+32)&~(uintptr_t)15);
         png_row_info ri; png_structp pp=png_create_read_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
         if(!pp)return 92;
         memset(&ri,0,sizeof ri);ri.width=2;ri.rowbytes=n;ri.bit_depth=8;
         ri.color_type=bpp==3?2:6;ri.channels=bpp;ri.pixel_depth=8*bpp;
         pp->pixel_depth=ri.pixel_depth;bucket=bpp-3;
         memset(base,0xa5,allocation);memset(pbase,0x5a,allocation);
         for(j=0;j<bpp;++j) {row[j]=(unsigned char)(triples[t][0]-triples[t][2]);
            row[j+bpp]=0;prev[j]=triples[t][2];prev[j+bpp]=triples[t][1];}
         memcpy(expected,row,n);memcpy(before,base,allocation);memcpy(prevcopy,pbase,allocation);
         reference(expected,prev,n,bpp,4);png_read_filter_row(pp,&ri,row,prev,4);
         for(j=0;j<bpp;++j)if(row[j]!=triples[t][0] || row[j+bpp]!=triples[t][3]) {
            fprintf(stderr,"PAETH_PRIORITY_FAIL bpp=%u triple=%zu channel=%zu\n",bpp,t,j);return 5;}
         if(memcmp(expected,row,n) || memcmp(base,before,(size_t)(row-base)) ||
            memcmp(row+n,before+(row-base)+n,allocation-(row-base)-n) || memcmp(pbase,prevcopy,allocation)) {
            fprintf(stderr,"PAETH_PRIORITY_GUARD_FAIL bpp=%u triple=%zu\n",bpp,t);return 6;}
         printf("PAETH_PRIORITY bpp=%u triple=%zu a=%u b=%u c=%u expected=%u\n",bpp,t,triples[t][0],triples[t][1],triples[t][2],triples[t][3]);
         png_destroy_read_struct(&pp,NULL,NULL);++cases;
      }
   }
   if(cases!=15212) {fprintf(stderr,"CASE_COUNT_FAIL expected=15212 actual=%lu\n",cases);return 7;}
   for(k=0;k<3;++k) {
      for(fi=0;fi<3;++fi)if(!choices[k][fi] || !ties[k][fi]) {fprintf(stderr,"PAETH_COVERAGE_FAIL bucket=%zu index=%zu\n",k,fi);return 4;}
      printf("PAETH bucket=%zu choices=%lu,%lu,%lu ties=%lu,%lu,%lu\n",k,choices[k][0],choices[k][1],choices[k][2],ties[k][0],ties[k][1],ties[k][2]);
   }
   printf("PASS filter_cases=%lu\n",cases);
   munmap(mr,page*3);munmap(mp,page*3);return 0;
}
