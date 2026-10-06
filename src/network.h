#pragma once
int network_get(const char *url,const char *client,const char *accept,char *body,unsigned size,unsigned deadline);
int network_pins(const char *url,const char *client,char *body,unsigned size);
int network_download(const char *url,const char *path);
int network_exit_requested(void);
int network_cancelled(void);

int network_put(const char *url,const char *client,char *body,unsigned size);

int network_last_error(void);
int network_last_status(void);
const char *network_last_stage(void);

int network_download_image(const char *url,const char *path);

void network_request_exit(void);
