#include "package.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv){assert(argc==5);assert(package_safe_name("assets/font16.bin") && package_safe_name("sce_sys/param.sfo") && !package_safe_name("../eboot.bin") && !package_safe_name("assets/../../eboot.bin") && !package_safe_name("assets/./font.bin") && !package_safe_name("assets\\font.bin") && !package_safe_name("assets/ux0:app") && !package_safe_name("/eboot.bin"));int expected=atoi(argv[4]);int r=package_extract(argv[1],argv[2],argv[3]);assert(expected?(r<0):(r==0));puts(expected?"Unsafe/mismatched package rejected":"Matching package extracted with CRC verification");return 0;}
