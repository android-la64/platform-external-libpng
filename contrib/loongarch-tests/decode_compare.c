/* Local Android integration test, 2026. Released under the libpng license. */
#include "../../png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>

typedef struct { png_bytep data; png_bytep *rows; png_uint_32 w,h;
   size_t rowbytes; int depth,color,passes,ended; } image;
static void setup(png_structp p,png_infop i,image *x) {
   png_uint_32 y;
   x->w=png_get_image_width(p,i);x->h=png_get_image_height(p,i);
   x->depth=png_get_bit_depth(p,i);x->color=png_get_color_type(p,i);
   x->passes=png_set_interlace_handling(p);png_read_update_info(p,i);
   x->rowbytes=png_get_rowbytes(p,i);
   if(!x->w || !x->h || x->h>SIZE_MAX/x->rowbytes) png_error(p,"size overflow");
   x->data=calloc(x->h,x->rowbytes);x->rows=calloc(x->h,sizeof *x->rows);
   if(!x->data || !x->rows) png_error(p,"allocation failed");
   for(y=0;y<x->h;++y)x->rows[y]=x->data+y*x->rowbytes;
}
static void info_cb(png_structp p,png_infop i) {
   setup(p,i,png_get_progressive_ptr(p));
}
static void row_cb(png_structp p,png_bytep row,png_uint_32 y,int pass) {
   image *x=png_get_progressive_ptr(p);(void)pass;
   if(y>=x->h)png_error(p,"row out of bounds");
   png_progressive_combine_row(p,x->rows[y],row);
}
static void end_cb(png_structp p,png_infop i) {
   image *x=png_get_progressive_ptr(p);(void)i;x->ended=1;
}
static void hexbytes(FILE *out,const unsigned char *bytes,size_t n) {
   size_t j;for(j=0;j<n;++j)fprintf(out,"%02x",bytes[j]);
}
static void metadata(png_structp p,png_infop i,FILE *out) {
   png_fixed_point gamma;png_uint_32 rx,ry;int unit,n,j;png_colorp palette;
   png_bytep alpha;png_color_16p trans;png_textp text;
   if(png_get_gAMA_fixed(p,i,&gamma))fprintf(out,"GAMA %ld\n",(long)gamma);
   if(png_get_pHYs(p,i,&rx,&ry,&unit))fprintf(out,"PHYS %u %u %d\n",rx,ry,unit);
   if(png_get_PLTE(p,i,&palette,&n)) {
      fprintf(out,"PLTE %d ",n);for(j=0;j<n;++j)fprintf(out,"%02x%02x%02x",palette[j].red,palette[j].green,palette[j].blue);fputc('\n',out);
   }
   if(png_get_tRNS(p,i,&alpha,&n,&trans)) {
      if(png_get_color_type(p,i)==PNG_COLOR_TYPE_PALETTE) {fprintf(out,"TRNS %d ",n);hexbytes(out,alpha,n);fputc('\n',out);}
      else fprintf(out,"TRNS_COLOR %u %u %u %u\n",trans->gray,trans->red,trans->green,trans->blue);
   }
   n=png_get_text(p,i,&text,NULL);
   for(j=0;j<n;++j) {
      fprintf(out,"TEXT %d ",text[j].compression);hexbytes(out,(const unsigned char *)text[j].key,strlen(text[j].key));fputc(' ',out);
      hexbytes(out,(const unsigned char *)text[j].text,strlen(text[j].text));fputc('\n',out);
   }
}
static int decode(const char *mode,const char *path,FILE *out) {
   FILE *in;png_structp p;png_infop i,end;image x;unsigned char buf[97];size_t n;
   memset(&x,0,sizeof x);in=fopen(path,"rb");if(!in)return 65;
   p=png_create_read_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);if(!p)return 66;
   i=png_create_info_struct(p);end=png_create_info_struct(p);if(!i || !end)return 67;
   if(setjmp(png_jmpbuf(p)))return 1;
   if(!strcmp(mode,"seq")) {
      png_init_io(p,in);png_read_info(p,i);setup(p,i,&x);png_read_image(p,x.rows);png_read_end(p,end);x.ended=1;
   } else {
      png_set_progressive_read_fn(p,&x,info_cb,row_cb,end_cb);
      while((n=fread(buf,1,sizeof buf,in))!=0)png_process_data(p,i,buf,n);
   }
   if(ferror(in) || !x.ended)return 2;fclose(in);
   /* Canonicalize unused packed low bits only; they are outside PNG pixels. */
   if(x.depth<8) {
      unsigned used=(x.w*x.depth)%8;png_uint_32 y;
      if(used)for(y=0;y<x.h;++y)x.rows[y][x.rowbytes-1]&=(unsigned char)(255U<<(8-used));
   }
   fprintf(out,"PNGROWS %u %u %d %d %zu\n",x.w,x.h,x.depth,x.color,x.rowbytes);
   if(fwrite(x.data,x.rowbytes,x.h,out)!=x.h)return 3;
   fputs("META_BEGIN\n",out);
   fprintf(out,"IHDR %u %u %d %d %d %d %d\n",x.w,x.h,x.depth,x.color,
      png_get_interlace_type(p,i),png_get_compression_type(p,i),png_get_filter_type(p,i));
   fprintf(out,"VALID %u\n",png_get_valid(p,i,~0U)|png_get_valid(p,end,~0U));
   metadata(p,i,out);metadata(p,end,out);fputs("META_END\n",out);
   printf("PASS mode=%s width=%u height=%u depth=%d color=%d rowbytes=%zu passes=%d zlib_compile=%s zlib_runtime=%s\n",mode,x.w,x.h,x.depth,x.color,x.rowbytes,x.passes,ZLIB_VERSION,zlibVersion());
   png_destroy_read_struct(&p,&i,&end);free(x.rows);free(x.data);return 0;
}

int main(int argc,char **argv) {
   FILE *out,*list;char path[4096];unsigned count=0;int code;
   if(argc!=4 && argc!=5)return 64;
   if(argc==4) {
      if(strcmp(argv[1],"seq") && strcmp(argv[1],"prog"))return 64;
      out=fopen(argv[3],"wb");if(!out)return 68;
      code=decode(argv[1],argv[2],out);if(fclose(out))return 3;return code;
   }
   if(strcmp(argv[1],"--batch") || (strcmp(argv[2],"seq") && strcmp(argv[2],"prog")))return 64;
   list=fopen(argv[3],"r");out=fopen(argv[4],"wb");if(!list || !out)return 68;
   while(fgets(path,sizeof path,list)) {
      size_t n=strlen(path);if(!n || path[n-1]!='\n')return 69;path[n-1]=0;
      code=decode(argv[2],path,out);if(code) {fprintf(stderr,"FAIL input=%s code=%d\n",path,code);return code;}++count;
   }
   if(ferror(list) || fclose(out))return 3;fclose(list);
   printf("BATCH_PASS images=%u\n",count);return count?0:4;
}
