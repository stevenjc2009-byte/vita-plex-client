#include "plex_auth.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

void plex_auth_headers(const char *client_id, char *out, unsigned out_len) {
  snprintf(out, out_len,
    "X-Plex-Product: PlexVita\r\n"
    "X-Plex-Version: 1.0\r\n"
    "X-Plex-Platform: PlayStation Vita\r\n"
    "X-Plex-Device: PS Vita\r\n"
    "X-Plex-Client-Identifier: %s\r\n"
    "Accept: application/json",
    client_id);
}

void plex_pin_create_url(char *out, unsigned out_len) {
  snprintf(out, out_len, "https://plex.tv/api/v2/pins?strong=false");
}

void plex_pin_poll_url(int pin_id, char *out, unsigned out_len) {
  snprintf(out, out_len, "https://plex.tv/api/v2/pins/%d", pin_id);
}

static int hex4(const char *p,unsigned *out){unsigned n=0;for(int i=0;i<4;i++){int c=(unsigned char)p[i];if(!c)return -1;int v=c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:-1;if(v<0)return -1;n=n*16+(unsigned)v;}*out=n;return 0;}
static int json_text(const char **cursor,char *out,unsigned cap){const char *p=*cursor;if(*p++!='"')return -1;unsigned n=0;int overflow=0;
 while(*p && *p!='"'){unsigned cp=(unsigned char)*p++;char bytes[4];unsigned count=1;
  if(cp<32)return -1;
  if(cp=='\\'){unsigned esc=(unsigned char)*p++;switch(esc){case '"':case '\\':case '/':cp=esc;break;case 'b':cp=8;break;case 'f':cp=12;break;case 'n':cp=10;break;case 'r':cp=13;break;case 't':cp=9;break;
   case 'u':if(hex4(p,&cp))return -1;p+=4;if(cp>=0xD800 && cp<=0xDBFF){unsigned lo;if(p[0]!='\\' || p[1]!='u' || hex4(p+2,&lo) || lo<0xDC00 || lo>0xDFFF)return -1;cp=0x10000+((cp-0xD800)<<10)+(lo-0xDC00);p+=6;}else if(cp>=0xDC00 && cp<=0xDFFF)return -1;
    if(cp==0)return -1;
    if(cp<128)count=1;else if(cp<2048)count=2;else if(cp<65536)count=3;else count=4;
    if(count==2){bytes[0]=0xC0|(cp>>6);bytes[1]=0x80|(cp&63);}else if(count==3){bytes[0]=0xE0|(cp>>12);bytes[1]=0x80|((cp>>6)&63);bytes[2]=0x80|(cp&63);}else if(count==4){bytes[0]=0xF0|(cp>>18);bytes[1]=0x80|((cp>>12)&63);bytes[2]=0x80|((cp>>6)&63);bytes[3]=0x80|(cp&63);}break;
   default:return -1;}}
  if(count==1)bytes[0]=(char)cp;
  if(out && n+count<cap)memcpy(out+n,bytes,count);else if(out)overflow=1;
  n+=count;
 }
 if(*p!='"')return -1;
 *cursor=p+1;if(out){if(overflow || !cap){if(cap)out[0]=0;return -1;}out[n]=0;}return 0;
}
static const char *json_value(const char *json,const char *key){if(!json || !key)return NULL;const char *p=json;
 while(*p){if(*p!='"'){p++;continue;}char name[96];const char *after=p;int r=json_text(&after,name,sizeof(name));if(after==p)return NULL;p=after;while(isspace((unsigned char)*p))p++;
  if(*p==':'){p++;while(isspace((unsigned char)*p))p++;if(!r && !strcmp(name,key))return p;}}
 return NULL;
}
int plex_json_string(const char *json,const char *key,char *out,unsigned cap){if(!out || !cap)return -1;out[0]=0;const char *p=json_value(json,key);if(!p || json_text(&p,out,cap) || !out[0]){out[0]=0;return -1;}return 0;}
static int extract_int(const char *json,const char *key,int *value){const char *p=json_value(json,key);if(!p || !isdigit((unsigned char)*p))return -1;unsigned n=0;
 while(isdigit((unsigned char)*p)){unsigned digit=(unsigned)(*p++-'0');if(n>(2147483647u-digit)/10)return -1;n=n*10+digit;}
 if(*p && *p!=',' && *p!='}' && !isspace((unsigned char)*p))return -1;
 *value=(int)n;return 0;
}

int plex_parse_pin_create(const char *json, plex_pin_t *pin) {
  plex_pin_t next = {0};
  if (!json || !pin) return -1;
  memset(pin, 0, sizeof(*pin));
  if (extract_int(json, "id", &next.pin_id) != 0 || next.pin_id <= 0) return -1;
  if (plex_json_string(json, "code", next.code, sizeof(next.code)) != 0) return -1;
  *pin = next;
  return 0;
}

int plex_parse_auth_token(const char *json, char *token_out, unsigned len) {
  if(plex_json_string(json,"authToken",token_out,len))return -1;
  for(const unsigned char *p=(const unsigned char*)token_out;*p;p++)if(*p<33 || *p>=127){token_out[0]=0;return -1;}
  return 0;
}

void plex_url_encode(const char *in, char *out, unsigned out_len) {
  if (!out || !out_len) return;
  unsigned j = 0;
  for (unsigned i = 0; in[i]; i++) {
    char c = in[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      if (j + 1 >= out_len) break;
      out[j++] = c;
    } else {
      if (j + 3 >= out_len) break;
      snprintf(out + j, out_len - j, "%%%02X", (unsigned char)c);
      j += 3;
    }
  }
  out[j] = 0;
}
