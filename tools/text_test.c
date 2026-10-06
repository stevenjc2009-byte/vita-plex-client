#include "text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){char buffer[16]="caf";assert(!text_append(buffer,sizeof(buffer),"é"));assert(!strcmp(buffer,"café") && text_count(buffer)==4);text_delete(buffer);assert(!strcmp(buffer,"caf"));assert(!text_append(buffer,sizeof(buffer),"Ж"));text_delete(buffer);assert(!strcmp(buffer,"caf"));assert(!text_append(buffer,sizeof(buffer),"😀"));assert(text_count(buffer)==4);text_delete(buffer);assert(!strcmp(buffer,"caf"));assert(text_append(buffer,5,"é")<0 && !strcmp(buffer,"caf"));assert(!strcmp(text_at("αβγ",2),"γ"));const char *p="\xED\xA0\x80";assert(text_next(&p)=='?');p="\xF0\x9F";assert(text_next(&p)=='?' && !*p);text_delete(buffer);text_delete(buffer);text_delete(buffer);text_delete(buffer);assert(!*buffer);puts("UTF-8 entry, deletion, bounds and malformed input tests passed");return 0;}
