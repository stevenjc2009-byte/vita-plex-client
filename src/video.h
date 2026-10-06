#pragma once
#include <stdint.h>
typedef struct {
 const unsigned char *source;unsigned *dest;
 unsigned width,height,out_width,out_height,left,top;
 unsigned short x[960],y[544];
} video_job_t;
int video_prepare(video_job_t *j,const unsigned char *src,unsigned w,unsigned h,float aspect,unsigned *dst);
void video_rows(const video_job_t *j,unsigned begin,unsigned end);
int video_pool_init(void);
void video_convert(const video_job_t *j);
void video_pool_shutdown(void);

unsigned video_observed_cores(void);
