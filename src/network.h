#pragma once
int network_get(const char *url,const char *client,const char *accept,char *body,unsigned size,unsigned deadline);
int network_pins(const char *url,const char *client,char *body,unsigned size);
int network_download(const char *url,const char *path);
int network_exit_requested(void);
int network_cancelled(void);

int network_put(const char *url,const char *client,char *body,unsigned size);
