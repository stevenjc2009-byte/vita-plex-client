#include "text.h"
#include <string.h>
unsigned text_next(const char **text){const unsigned char *p=(const unsigned char*)*text;unsigned c=*p;if(!c)return 0;p++;unsigned n=c>=0xC2 && c<=0xDF?1:c>=0xE0 && c<=0xEF?2:c>=0xF0 && c<=0xF4?3:0;unsigned original=c;
 if(n){c&=(1u<<(6-n))-1;for(unsigned i=0;i<n;i++){if(!*p || (*p&0xC0)!=0x80){*text=(const char*)p;return '?';}c=(c<<6)|(*p++&63);}if((n==1 && c<128) || (n==2 && c<2048) || (n==3 && c<65536) || c>0x10FFFF || (c>=0xD800 && c<=0xDFFF))c='?';}else if(original>=128)c='?';*text=(const char*)p;return c;}
unsigned text_count(const char *text){unsigned count=0;while(*text){text_next(&text);count++;}return count;}
const char *text_at(const char *text,unsigned index){while(*text && index--)text_next(&text);return text;}
int text_append(char *buffer,unsigned cap,const char *character){const char *end=character;text_next(&end);unsigned n=(unsigned)strlen(buffer),bytes=(unsigned)(end-character);if(!bytes || n+bytes>=cap)return -1;memcpy(buffer+n,character,bytes);buffer[n+bytes]=0;return 0;}
void text_delete(char *buffer){const char *p=buffer,*last=p;while(*p){last=p;text_next(&p);}if(p!=buffer)buffer[last-buffer]=0;}
