// GitHub VPKs are extracted into a fixed staging directory, never into the app.
#include "miniz/miniz.h"
#include "package.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#ifdef __vita__
#include <psp2/io/stat.h>
#define make_dir(p) sceIoMkdir(p,0777)
#elif defined(_WIN32)
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#define make_dir(p) mkdir(p,0777)
#endif
int package_safe_name(const char *n){
 if(!n || !*n || strlen(n)>180 || n[0]=='/' || strchr(n,'\\') || strchr(n,':'))return 0;
 if(strcmp(n,"eboot.bin") && strncmp(n,"sce_sys/",8) && strncmp(n,"assets/",7))return 0;
 for(const char *p=n;*p;p++){if(!isalnum((unsigned char)*p) && !strchr("_./-",*p))return 0;if(*p=='.' && (p[1]=='/' || !p[1]))return 0;if(*p=='.' && (p==n || p[-1]=='/') && (p[1]=='/' || p[1]==0 || (p[1]=='.' && (p[2]=='/' || p[2]==0))))return 0;}
 return !strstr(n,"//");
}
static uint32_t le32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned le16(const unsigned char *p){return p[0]|p[1]<<8;}
static int sfo_value(const unsigned char *data,size_t size,const char *key,char *out,unsigned cap){
 if(size<20 || le32(data)!=0x46535000)return -1;uint32_t keys=le32(data+8),values=le32(data+12),count=le32(data+16);
 if(count>128 || count>(size-20)/16 || keys>=size || values>=size)return -1;
 for(uint32_t i=0;i<count;i++){const unsigned char *e=data+20+i*16;uint32_t k=le16(e),v=le32(e+12),n=le32(e+4);
  if(k>=size-keys || v>=size-values || n>size-values-v)return -1;const char *name=(const char*)data+keys+k;if(!memchr(name,0,size-keys-k))return -1;
  if(!strcmp(name,key)){const char *text=(const char*)data+values+v;if(le16(e+2)!=0x204 || !n || !memchr(text,0,n) || strlen(text)>=cap)return -1;strcpy(out,text);return 0;}}
 return -1;
}
int package_extract(const char *archive,const char *directory,const char *version){
 mz_zip_archive zip;memset(&zip,0,sizeof(zip));int result=-1;unsigned char sfo[16384];char id[32],ver[32];
 FILE *f=fopen(archive,"rb");if(!f)return -1;if(fseek(f,0,SEEK_END)){fclose(f);return -1;}long bytes=ftell(f);fclose(f);
 if(bytes<=0 || bytes>32*1024*1024 || !mz_zip_reader_init_file(&zip,archive,0))return -1;
 unsigned count=mz_zip_reader_get_num_files(&zip);uint64_t total=0;int executable=0;
 if(!count || count>256)goto out;
 for(unsigned i=0;i<count;i++){mz_zip_archive_file_stat st;if(!mz_zip_reader_file_stat(&zip,i,&st) || !package_safe_name(st.m_filename) || st.m_is_encrypted || !st.m_is_supported || st.m_uncomp_size>32*1024*1024 || ((st.m_external_attr>>16)&0170000)==0120000)goto out;
  total+=st.m_uncomp_size;if(total>64*1024*1024)goto out;
  if(!strcmp(st.m_filename,"eboot.bin")){if(st.m_uncomp_size<0x100)goto out;executable=1;}
  for(unsigned j=0;j<i;j++){mz_zip_archive_file_stat other;if(!mz_zip_reader_file_stat(&zip,j,&other))goto out;const char *a=st.m_filename,*b=other.m_filename;while(*a && *b && tolower((unsigned char)*a)==tolower((unsigned char)*b)){a++;b++;}if(!*a && !*b)goto out;}
 }
 int index=mz_zip_reader_locate_file(&zip,"sce_sys/param.sfo",NULL,MZ_ZIP_FLAG_CASE_SENSITIVE);mz_zip_archive_file_stat st;
 if(!executable || index<0 || !mz_zip_reader_file_stat(&zip,index,&st) || st.m_uncomp_size>sizeof(sfo) || !mz_zip_reader_extract_to_mem(&zip,index,sfo,sizeof(sfo),0) || sfo_value(sfo,(size_t)st.m_uncomp_size,"TITLE_ID",id,sizeof(id)) || strcmp(id,"PLEX00001") || sfo_value(sfo,(size_t)st.m_uncomp_size,"APP_VER",ver,sizeof(ver)) || strcmp(ver,version))goto out;
 make_dir(directory);
 for(unsigned i=0;i<count;i++){if(!mz_zip_reader_file_stat(&zip,i,&st))goto out;char path[512];if(snprintf(path,sizeof(path),"%s/%s",directory,st.m_filename)>=(int)sizeof(path))goto out;
  for(char *p=path+strlen(directory)+1;*p;p++)if(*p=='/'){*p=0;make_dir(path);*p='/';}
  if(st.m_is_directory){make_dir(path);continue;}
  if(!mz_zip_reader_extract_to_file(&zip,i,path,0))goto out;
 }
 result=0;
out:mz_zip_reader_end(&zip);return result;
}
