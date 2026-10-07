// HLS transport/demux owned by the application. Vita playback explicitly uses
// h264_vita / aac_vita; the file-only AvPlayer entry point never receives HLS.
#include "hls_backend.h"
#include "hls.h"
#include "http.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/io/fcntl.h>
#include "performance.h"
#endif

#define VIDEO_QUEUE 6
#define AUDIO_QUEUE 24
#define SEGMENT_CAP (8u*1024*1024)
#define PLAYLIST_CAP (512u*1024)
typedef struct {unsigned char *data;unsigned bytes,width,height;float aspect;uint64_t stamp;} video_slot_t;
typedef struct {unsigned char data[8192];unsigned size,rate,channels;uint64_t stamp;} audio_slot_t;
typedef struct {AVPacket *packets[256];unsigned read,write,count,bytes;uint64_t next_stamp_us;int eof,done,result,video;AVCodecContext *codec;AVStream *track;pthread_t thread;int running;} decode_queue_t;
static struct {
 pthread_mutex_t lock;
 volatile int stop;
 int running,ready,finished,error,transport_error,paused,started,audio_present;
 const char *stage;
 char url[4096];char *playlist;unsigned char *segment;
 unsigned segment_size,segment_at,variants;uint64_t sequence;
 video_slot_t video[VIDEO_QUEUE],shown;audio_slot_t audio[AUDIO_QUEUE],playing;
 unsigned vr,vw,vn,ar,aw,an;
 uint64_t start_clock,pause_clock,last_stamp,origin;
 int origin_set,buffering,has_video,has_audio;
 uint64_t consumed,video_until,audio_until,download_bytes,download_us;unsigned dropped;
 decode_queue_t vdecode,adecode;
 unsigned char *next_segment;unsigned next_size;int next_ready,download_done,download_error;pthread_t downloader;int downloader_running;
#ifdef __vita__
 SceUID thread;unsigned modules;
#else
 pthread_t thread;
#endif
} stream;
static uint64_t now_us(void){
#ifdef __vita__
 return sceKernelGetProcessTimeWide();
#else
 struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;
#endif
}
static void delay_us(unsigned n){
#ifdef __vita__
 sceKernelDelayThread(n);
#else
 struct timespec t={n/1000000,(long)(n%1000000)*1000};nanosleep(&t,NULL);
#endif
}
static int cancelled(void){return __atomic_load_n(&stream.stop,__ATOMIC_ACQUIRE);}
static int interrupt(void *unused){(void)unused;return cancelled();}
static void stage(const char *text){pthread_mutex_lock(&stream.lock);if(!stream.error)stream.stage=text;pthread_mutex_unlock(&stream.lock);}
static uint64_t clock_locked(void){
 if(!stream.started)return 0;
 uint64_t now=now_us(),t=((stream.paused?stream.pause_clock:now)-stream.start_clock)/1000;
 uint64_t until=stream.has_audio && stream.audio_until && !(stream.has_video && stream.adecode.done && !stream.an)?stream.audio_until:stream.video_until;
 if(stream.adecode.done && !stream.an && stream.audio_until>until)until=stream.audio_until;
 // Anchor to media actually handed to the renderer/audio output. An empty
 // producer queue must never advance saved position through a network stall.
 if(!stream.paused && t>until){stream.start_clock=now-until*1000;t=until;stream.buffering=1;}
 return t;
}
static int fetch_retry(const char *url,void *data,unsigned cap,unsigned *used){
 for(unsigned attempt=0;attempt<3;attempt++){
  if(cancelled())return AVERROR_EXIT;
  uint64_t begin=now_us();int r=http_media_fetch(url,data,cap,used,&stream.stop);if(r>=0){pthread_mutex_lock(&stream.lock);stream.download_bytes+=*used;stream.download_us+=now_us()-begin;pthread_mutex_unlock(&stream.lock);return r;}
  // Authentication, missing media, cancellation and malformed/oversize
  // responses are terminal. Retry timeouts, partial transfers and 5xx only.
  if(r==-2 || r==-401 || r==-403 || r==-404 || r==-9 || r==HTTP_MEDIA_DISCONNECTED)return r;
  if(attempt==2)return r;
  stage("HLS connection retry");for(unsigned i=0;i<(attempt+1)*10 && !cancelled();i++)delay_us(20000);
 }return AVERROR(EIO);
}
static int load_segment(void){
 char uri[2048],url[4096];uint64_t seq=0,start=now_us();
 for(;;){
  if(cancelled())return AVERROR_EXIT;
  int r=stream.playlist && stream.playlist[0]?hls_segment(stream.playlist,stream.sequence,uri,sizeof(uri),&seq):0;
  if(r<0){stage("HLS playlist format");return AVERROR_INVALIDDATA;}
  if(r==2)return AVERROR_EOF;
  if(r==1){
   if(hls_resolve(stream.url,uri,url,sizeof(url))){stage("HLS segment URL");return AVERROR_INVALIDDATA;}
   stage("HLS segment download");r=fetch_retry(url,stream.next_segment,SEGMENT_CAP,&stream.next_size);
   if(r<0)return r;
   if(!stream.next_size || stream.next_size%188 || stream.next_segment[0]!=0x47){stage("HLS MPEG-TS data");return AVERROR_INVALIDDATA;}
   stream.sequence=seq+1;return 0;
  }
  if(now_us()-start>30000000){stage("HLS playlist wait");return AVERROR(ETIMEDOUT);}
  stage("HLS playlist download");unsigned used;
  r=fetch_retry(stream.url,stream.playlist,PLAYLIST_CAP-1,&used);if(r<0)return r;stream.playlist[used]=0;
  int variant=hls_variant(stream.playlist,uri,sizeof(uri));
  if(variant<0 || (variant && (++stream.variants>3 || hls_resolve(stream.url,uri,url,sizeof(url))))){stage("HLS variant playlist");return AVERROR_INVALIDDATA;}
  if(variant){strcpy(stream.url,url);stream.playlist[0]=0;continue;}
  r=hls_segment(stream.playlist,stream.sequence,uri,sizeof(uri),&seq);
  if(!r)delay_us(200000);
 }
}
static void *download_worker(void *unused){
 (void)unused;
#ifdef __vita__
 if(sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(),0x10000)<0)sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(),0);
#endif
 while(!cancelled()){
  pthread_mutex_lock(&stream.lock);int full=stream.next_ready;pthread_mutex_unlock(&stream.lock);
  if(full){delay_us(2000);continue;}
  int r=load_segment();pthread_mutex_lock(&stream.lock);
  if(r<0){stream.download_done=1;stream.download_error=r;pthread_mutex_unlock(&stream.lock);break;}
  stream.next_ready=1;pthread_mutex_unlock(&stream.lock);
 }return NULL;
}
static int read_media(void *unused,unsigned char *data,int size){
 (void)unused;if(cancelled())return AVERROR_EXIT;
 while(stream.segment_at==stream.segment_size){
  pthread_mutex_lock(&stream.lock);
  if(stream.next_ready){unsigned char *old=stream.segment;stream.segment=stream.next_segment;stream.next_segment=old;
   stream.segment_size=stream.next_size;stream.segment_at=0;stream.next_ready=0;pthread_mutex_unlock(&stream.lock);break;}
  int done=stream.download_done,r=stream.download_error;pthread_mutex_unlock(&stream.lock);
  if(done){if(r!=AVERROR_EOF && r!=AVERROR_EXIT)stream.transport_error=r;return r;}
  if(cancelled())return AVERROR_EXIT;delay_us(2000);
 }
 unsigned n=stream.segment_size-stream.segment_at;if(n>(unsigned)size)n=(unsigned)size;
 memcpy(data,stream.segment+stream.segment_at,n);stream.segment_at+=n;return (int)n;
}
static uint64_t timestamp(AVFrame *frame,AVStream *track){
 int64_t pts=frame->best_effort_timestamp;
 if(pts==AV_NOPTS_VALUE)pts=frame->pts;
 int64_t ms=pts==AV_NOPTS_VALUE?0:av_rescale_q(pts,track->time_base,(AVRational){1,1000});
 if(!stream.origin_set){stream.origin=ms>0?(uint64_t)ms:0;stream.origin_set=1;}
 decode_queue_t *q=track->codecpar->codec_type==AVMEDIA_TYPE_AUDIO?&stream.adecode:&stream.vdecode;
 uint64_t stamp=pts==AV_NOPTS_VALUE?q->next_stamp_us:ms>0 && (uint64_t)ms>=stream.origin?((uint64_t)ms-stream.origin)*1000:0;
 // MPEG-TS PES packets often timestamp only the first AAC frame. Derive
 // subsequent timestamps from sample counts, never reset those frames to zero.
 if(stamp<q->next_stamp_us)stamp=q->next_stamp_us;
 uint64_t duration;
 if(frame->nb_samples && frame->sample_rate)duration=(uint64_t)frame->nb_samples*1000000/frame->sample_rate;
 else if(frame->duration>0)duration=av_rescale_q(frame->duration,track->time_base,(AVRational){1,1000000});
 else if(track->avg_frame_rate.num>0 && track->avg_frame_rate.den>0)duration=av_rescale_q(1,av_inv_q(track->avg_frame_rate),(AVRational){1,1000000});
 else duration=41667;
 q->next_stamp_us=stamp+duration;return stamp/1000;
}
static int queue_video(AVFrame *f,AVStream *track){
 if(f->width<=0 || f->height<=0 || (f->width&1) || (f->height&1) || f->width>1280 || f->height>720 ||
    (f->format!=AV_PIX_FMT_NV12 && f->format!=AV_PIX_FMT_YUV420P))return AVERROR_INVALIDDATA;
 while(!cancelled()){
  pthread_mutex_lock(&stream.lock);int full=stream.vn==VIDEO_QUEUE;pthread_mutex_unlock(&stream.lock);if(!full)break;delay_us(2000);
 }if(cancelled())return AVERROR_EXIT;
 video_slot_t *slot=stream.video+stream.vw;unsigned size=(unsigned)f->width*f->height*3/2;
 if(slot->bytes<size){unsigned char *p=realloc(slot->data,size);if(!p)return AVERROR(ENOMEM);slot->data=p;slot->bytes=size;}
 unsigned w=(unsigned)f->width,h=(unsigned)f->height;
 if(!f->data[0] || !f->data[1] || (f->format==AV_PIX_FMT_YUV420P && (!f->data[2] || f->linesize[2]<(int)(w/2))) || f->linesize[0]<(int)w || f->linesize[1]<(int)(f->format==AV_PIX_FMT_NV12?w:w/2))return AVERROR_INVALIDDATA;
 for(unsigned y=0;y<h;y++)memcpy(slot->data+y*w,f->data[0]+y*f->linesize[0],w);
 unsigned char *vu=slot->data+w*h;
 for(unsigned y=0;y<h/2;y++)for(unsigned x=0;x<w;x+=2){
  if(f->format==AV_PIX_FMT_NV12){const unsigned char *uv=f->data[1]+y*f->linesize[1]+x;vu[y*w+x]=uv[1];vu[y*w+x+1]=uv[0];}
  else{vu[y*w+x]=f->data[2][y*f->linesize[2]+x/2];vu[y*w+x+1]=f->data[1][y*f->linesize[1]+x/2];}
 }
 slot->width=w;slot->height=h;slot->aspect=(float)w/h;
 if(f->sample_aspect_ratio.num>0 && f->sample_aspect_ratio.den>0)slot->aspect*=av_q2d(f->sample_aspect_ratio);
 slot->stamp=timestamp(f,track);
 pthread_mutex_lock(&stream.lock);stream.vw=(stream.vw+1)%VIDEO_QUEUE;stream.vn++;stream.ready=1;
 if(slot->stamp>stream.last_stamp)stream.last_stamp=slot->stamp;pthread_mutex_unlock(&stream.lock);return 0;
}
static int queue_audio(AVFrame *f,AVStream *track){
 unsigned channels=(unsigned)f->ch_layout.nb_channels;
 if((channels!=1 && channels!=2) || f->sample_rate<8000 || f->sample_rate>48000 || f->nb_samples<1 || f->nb_samples>2048 || (f->format!=AV_SAMPLE_FMT_S16 && f->format!=AV_SAMPLE_FMT_FLTP))return AVERROR_INVALIDDATA;
 while(!cancelled()){
  pthread_mutex_lock(&stream.lock);int full=stream.an==AUDIO_QUEUE;pthread_mutex_unlock(&stream.lock);if(!full)break;delay_us(2000);
 }if(cancelled())return AVERROR_EXIT;
 audio_slot_t *slot=stream.audio+stream.aw;slot->size=(unsigned)f->nb_samples*channels*2;slot->rate=(unsigned)f->sample_rate;slot->channels=channels;
 slot->stamp=timestamp(f,track);
 if(f->format==AV_SAMPLE_FMT_S16){if(!f->data[0])return AVERROR_INVALIDDATA;memcpy(slot->data,f->data[0],slot->size);}
 else{short *pcm=(short*)slot->data;for(unsigned c=0;c<channels;c++){if(!f->extended_data[c])return AVERROR_INVALIDDATA;const float *samples=(const float*)f->extended_data[c];for(int n=0;n<f->nb_samples;n++){float sample=samples[n]*32768.0f;pcm[n*channels+c]=sample>=32767?32767:sample<=-32768?-32768:(short)sample;}}}
 pthread_mutex_lock(&stream.lock);stream.aw=(stream.aw+1)%AUDIO_QUEUE;stream.an++;stream.ready=1;if(!stream.audio_present)stream.audio_present=1;
 if(slot->stamp+(unsigned)f->nb_samples*1000/slot->rate>stream.last_stamp)stream.last_stamp=slot->stamp+(unsigned)f->nb_samples*1000/slot->rate;
 pthread_mutex_unlock(&stream.lock);return 0;
}
static int decode_packet(AVCodecContext *codec,AVPacket *packet,AVFrame *frame,AVStream *track,int video){
 int r=avcodec_send_packet(codec,packet);if(r<0)return r;
 while((r=avcodec_receive_frame(codec,frame))>=0){r=video?queue_video(frame,track):queue_audio(frame,track);av_frame_unref(frame);if(r<0)return r;}
 return r==AVERROR(EAGAIN) || r==AVERROR_EOF?0:r;
}
static void *decode_worker(void *arg){
 decode_queue_t *q=arg;
#ifdef __vita__
 if(sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(),q->video?0x20000:0x40000)<0)sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(),0);
#endif
 AVFrame *frame=av_frame_alloc();int r=frame?0:AVERROR(ENOMEM);
 while(r>=0 && !cancelled()){
  pthread_mutex_lock(&stream.lock);AVPacket *p=NULL;int eof=q->eof;
  if(q->count){p=q->packets[q->read];q->packets[q->read]=NULL;q->read=(q->read+1)%256;q->count--;q->bytes-=(unsigned)p->size;}
  pthread_mutex_unlock(&stream.lock);
  if(p){r=decode_packet(q->codec,p,frame,q->track,q->video);av_packet_free(&p);}
  else if(eof){r=decode_packet(q->codec,NULL,frame,q->track,q->video);break;}
  else delay_us(2000);
 }
 av_frame_free(&frame);pthread_mutex_lock(&stream.lock);q->result=r;q->done=1;
 if(r<0 && !cancelled()){stream.error=r;stream.stage=q->video?"H.264 decode":"AAC decode";}
 pthread_mutex_unlock(&stream.lock);return NULL;
}
static int submit_packet(decode_queue_t *q,AVPacket *packet){
 if(packet->size<0 || packet->size>2*1024*1024)return AVERROR_INVALIDDATA;
 for(;;){
  if(cancelled())return AVERROR_EXIT;
  pthread_mutex_lock(&stream.lock);int error=stream.error;
  int room=q->count<256 && q->bytes+(unsigned)packet->size<=2*1024*1024;
  if(room && !error){AVPacket *copy=av_packet_clone(packet);if(!copy){pthread_mutex_unlock(&stream.lock);return AVERROR(ENOMEM);}
   q->packets[q->write]=copy;q->write=(q->write+1)%256;q->count++;q->bytes+=(unsigned)copy->size;pthread_mutex_unlock(&stream.lock);return 0;}
  pthread_mutex_unlock(&stream.lock);if(error)return error;delay_us(2000);
 }
}
static int start_decode(decode_queue_t *q,AVCodecContext *codec,AVStream *track,int video){
 q->codec=codec;q->track=track;q->video=video;int r=pthread_create(&q->thread,NULL,decode_worker,q);if(r)return AVERROR(r);q->running=1;return 0;
}
static void finish_decode(decode_queue_t *q){
 if(q->running){pthread_mutex_lock(&stream.lock);q->eof=1;pthread_mutex_unlock(&stream.lock);pthread_join(q->thread,NULL);q->running=0;}
 for(unsigned i=0;i<256;i++)av_packet_free(&q->packets[i]);
}
static int open_codec(AVFormatContext *format,int index,const char *name,AVCodecContext **codec){
 const AVCodec *impl=avcodec_find_decoder_by_name(name);if(!impl)return AVERROR_DECODER_NOT_FOUND;
 *codec=avcodec_alloc_context3(impl);if(!*codec)return AVERROR(ENOMEM);
 int r=avcodec_parameters_to_context(*codec,format->streams[index]->codecpar);if(r<0)return r;
 (*codec)->thread_count=1;(*codec)->pkt_timebase=format->streams[index]->time_base;
 if((*codec)->codec_type==AVMEDIA_TYPE_VIDEO)(*codec)->pix_fmt=AV_PIX_FMT_NV12;
 return avcodec_open2(*codec,impl,NULL);
}
#ifdef __vita__
static void codec_log(void *context,int level,const char *fmt,va_list args){
 (void)context;if(level>AV_LOG_ERROR || !fmt || (strncmp(fmt,"vita_h264",9) && strncmp(fmt,"vita_aac",8) && strncmp(fmt,"vita_%s",7)))return;
 // Only codec diagnostic formats, never demux/network strings or URLs.
 char text[256];int n=vsnprintf(text,sizeof(text),fmt,args);if(n<=0)return;if(n>255)n=255;
 SceUID file=sceIoOpen("ux0:data/plex-client/debug.log",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0777);
 if(file>=0){sceIoWrite(file,text,(unsigned)n);sceIoClose(file);}
}
#endif
static int worker(void){
 AVFormatContext *format=NULL;AVIOContext *io=NULL;AVCodecContext *video=NULL,*audio=NULL;AVPacket *packet=NULL;AVFrame *frame=NULL;
 int r=0,vi=-1,ai=-1;unsigned char *buffer=NULL;
#ifdef __vita__
 av_log_set_callback(codec_log);av_log_set_level(AV_LOG_ERROR);
#else
 av_log_set_level(AV_LOG_QUIET);
#endif
#ifdef __vita__
 const int modules[]={SCE_SYSMODULE_AVCDEC,SCE_SYSMODULE_AUDIOCODEC};
 for(unsigned i=0;i<2;i++)if(sceSysmoduleIsLoaded(modules[i])!=0){stage(i?"AAC module":"AVC module");r=sceSysmoduleLoadModule(modules[i]);if(r<0)goto out;stream.modules|=1u<<i;}
#endif
 stage("HLS prefetch startup");
 r=pthread_create(&stream.downloader,NULL,download_worker,NULL);if(r){r=AVERROR(r);goto out;}stream.downloader_running=1;
 stage("MPEG-TS demux allocation");format=avformat_alloc_context();buffer=av_malloc(32768);packet=av_packet_alloc();frame=av_frame_alloc();
 if(!format || !buffer || !packet || !frame){r=AVERROR(ENOMEM);goto out;}
 io=avio_alloc_context(buffer,32768,0,NULL,read_media,NULL,NULL);if(!io){r=AVERROR(ENOMEM);goto out;}buffer=NULL;
 io->seekable=0;format->pb=io;format->flags|=AVFMT_FLAG_CUSTOM_IO;format->interrupt_callback=(AVIOInterruptCB){interrupt,NULL};
 format->probesize=256*1024;format->max_analyze_duration=2000000;format->fps_probe_size=0;
 stage("MPEG-TS header");r=avformat_open_input(&format,NULL,av_find_input_format("mpegts"),NULL);if(r<0)goto out;
 stage("MPEG-TS stream information");r=avformat_find_stream_info(format,NULL);if(r<0)goto out;
 for(unsigned i=0;i<format->nb_streams;i++){
  AVCodecParameters *p=format->streams[i]->codecpar;
  if(p->codec_type==AVMEDIA_TYPE_VIDEO && vi<0){if(p->codec_id!=AV_CODEC_ID_H264){r=AVERROR_DECODER_NOT_FOUND;goto out;}vi=(int)i;}
  if(p->codec_type==AVMEDIA_TYPE_AUDIO && ai<0){if(p->codec_id!=AV_CODEC_ID_AAC){r=AVERROR_DECODER_NOT_FOUND;goto out;}ai=(int)i;}
 }
 if(vi<0 && ai<0){r=AVERROR_STREAM_NOT_FOUND;goto out;}
 if(format->start_time!=AV_NOPTS_VALUE){stream.origin=format->start_time>0?(uint64_t)format->start_time/1000:0;stream.origin_set=1;}
#ifdef __vita__
 const char *vcodec="h264_vita",*acodec="aac_vita";
#else
 const char *vcodec="h264",*acodec="aac";
#endif
 if(vi>=0){stage("H.264 hardware codec");r=open_codec(format,vi,vcodec,&video);if(r<0)goto out;}
 if(ai>=0){stage("AAC hardware codec");r=open_codec(format,ai,acodec,&audio);if(r<0)goto out;}
 stream.has_video=vi>=0;stream.has_audio=ai>=0;
 if(video){r=start_decode(&stream.vdecode,video,format->streams[vi],1);if(r<0)goto out;}
 if(audio){r=start_decode(&stream.adecode,audio,format->streams[ai],0);if(r<0)goto out;}
 stage("HLS decoding");
 while(!cancelled()){
  pthread_mutex_lock(&stream.lock);int paused=stream.paused;pthread_mutex_unlock(&stream.lock);if(paused){delay_us(5000);continue;}
  r=av_read_frame(format,packet);if(r<0)break;
  if(packet->stream_index==vi){stage("H.264 hardware decode");r=submit_packet(&stream.vdecode,packet);}
  else if(packet->stream_index==ai){stage("AAC hardware decode");r=submit_packet(&stream.adecode,packet);}else r=0;
  av_packet_unref(packet);if(r<0)break;
  if(vi<0){pthread_mutex_lock(&stream.lock);if(stream.an)stream.ready=1;pthread_mutex_unlock(&stream.lock);}
 }
 if(stream.transport_error){r=stream.transport_error;stage("HLS transport");}
 if(r==AVERROR_EOF)r=0;
out:
 if(r<0 || cancelled())__atomic_store_n(&stream.stop,1,__ATOMIC_RELEASE);
 pthread_mutex_lock(&stream.lock);stream.vdecode.eof=stream.adecode.eof=1;pthread_mutex_unlock(&stream.lock);
 finish_decode(&stream.vdecode);finish_decode(&stream.adecode);
 if(stream.downloader_running){http_media_abort();pthread_join(stream.downloader,NULL);stream.downloader_running=0;}
 if(stream.transport_error){r=stream.transport_error;stage("HLS transport");}
 av_frame_free(&frame);av_packet_free(&packet);avcodec_free_context(&audio);avcodec_free_context(&video);
 avformat_close_input(&format);if(io){av_freep(&io->buffer);avio_context_free(&io);}av_free(buffer);
 pthread_mutex_lock(&stream.lock);if(r<0 && r!=AVERROR_EXIT && r!=-2)stream.error=r;stream.finished=1;pthread_mutex_unlock(&stream.lock);
 return r;
}
#ifdef __vita__
static int worker_entry(SceSize argc,void *arg){(void)argc;(void)arg;return worker();}
#else
static void *worker_entry(void *arg){(void)arg;worker();return NULL;}
#endif
int hls_backend_start(const char *url){
 hls_backend_stop();memset(&stream,0,sizeof(stream));stream.sequence=UINT64_MAX;stream.stage="HLS worker";
 if(!url || strlen(url)>=sizeof(stream.url))return -1;strcpy(stream.url,url);
 if(pthread_mutex_init(&stream.lock,NULL))return -1;
 stream.playlist=calloc(1,PLAYLIST_CAP);stream.segment=malloc(SEGMENT_CAP);stream.next_segment=malloc(SEGMENT_CAP);
 if(!stream.playlist || !stream.segment || !stream.next_segment){free(stream.playlist);free(stream.segment);free(stream.next_segment);pthread_mutex_destroy(&stream.lock);return AVERROR(ENOMEM);}
 stream.running=1;
#ifdef __vita__
 stream.thread=performance_thread("plex_hls_decode",worker_entry,0x10000110,0x40000,0x20000);
 int r=stream.thread;if(r>=0)r=sceKernelStartThread(stream.thread,0,NULL);
 if(r<0){if(stream.thread>=0)sceKernelDeleteThread(stream.thread);stream.thread=-1;hls_backend_stop();return r;}
#else
 if(pthread_create(&stream.thread,NULL,worker_entry,NULL)){stream.running=0;free(stream.playlist);free(stream.segment);free(stream.next_segment);pthread_mutex_destroy(&stream.lock);return -1;}
#endif
 return 0;
}
void hls_backend_stop(void){
 if(!stream.running)return;__atomic_store_n(&stream.stop,1,__ATOMIC_RELEASE);http_media_abort();
#ifdef __vita__
 if(stream.thread>=0){sceKernelWaitThreadEnd(stream.thread,NULL,NULL);sceKernelDeleteThread(stream.thread);}
#else
 pthread_join(stream.thread,NULL);
#endif
 for(unsigned i=0;i<VIDEO_QUEUE;i++)free(stream.video[i].data);free(stream.shown.data);free(stream.playlist);free(stream.segment);free(stream.next_segment);
#ifdef __vita__
 if(stream.modules&2)sceSysmoduleUnloadModule(SCE_SYSMODULE_AUDIOCODEC);if(stream.modules&1)sceSysmoduleUnloadModule(SCE_SYSMODULE_AVCDEC);
#endif
 stream.running=0;pthread_mutex_destroy(&stream.lock);
}
int hls_backend_active(void){
 if(!stream.running)return 0;pthread_mutex_lock(&stream.lock);
 int result=stream.ready && !stream.error && (!stream.finished || stream.vn || stream.an || clock_locked()<stream.last_stamp);
 pthread_mutex_unlock(&stream.lock);return result;
}
uint64_t hls_backend_time(void){if(!stream.running)return 0;pthread_mutex_lock(&stream.lock);uint64_t t=clock_locked();pthread_mutex_unlock(&stream.lock);return t;}
int hls_backend_video(hls_video_t *frame){
 if(!stream.running)return 0;pthread_mutex_lock(&stream.lock);
 if(stream.paused || !stream.vn){pthread_mutex_unlock(&stream.lock);return 0;}
 if(!stream.started){stream.started=1;stream.start_clock=now_us();}
 uint64_t clock=clock_locked();
 while(stream.vn>1 && stream.video[stream.vr].stamp+100<clock){stream.vr=(stream.vr+1)%VIDEO_QUEUE;stream.vn--;stream.dropped++;}
 video_slot_t *slot=stream.video+stream.vr;if(slot->stamp>clock+25){pthread_mutex_unlock(&stream.lock);return 0;}
 uint64_t t=clock_locked();stream.video_until=slot->stamp+50;
 if(!stream.has_audio && stream.buffering){stream.start_clock=now_us()-t*1000;stream.buffering=0;}
 stream.consumed=slot->stamp;
 // Swap storage instead of returning a queue slot the producer may overwrite.
 video_slot_t previous=stream.shown;stream.shown=*slot;*slot=previous;stream.vr=(stream.vr+1)%VIDEO_QUEUE;stream.vn--;
 *frame=(hls_video_t){stream.shown.data,stream.shown.width,stream.shown.height,stream.shown.aspect,stream.shown.stamp};
 pthread_mutex_unlock(&stream.lock);return 1;
}
int hls_backend_audio(hls_audio_t *frame){
 if(!stream.running)return 0;pthread_mutex_lock(&stream.lock);
 if(stream.paused || !stream.an){pthread_mutex_unlock(&stream.lock);return 0;}
 if(!stream.started){stream.started=1;stream.start_clock=now_us();}
 audio_slot_t *slot=stream.audio+stream.ar;
 if(!stream.has_video && !stream.audio_until && slot->stamp>clock_locked()+30)stream.start_clock=now_us()-slot->stamp*1000;
 if(slot->stamp>clock_locked()+30){pthread_mutex_unlock(&stream.lock);return 0;}
 uint64_t t=clock_locked();stream.audio_until=slot->stamp+(uint64_t)slot->size*1000/(slot->channels*2*slot->rate);
 if(stream.buffering){stream.start_clock=now_us()-t*1000;stream.buffering=0;}
 stream.consumed=slot->stamp;
 stream.playing=*slot;stream.ar=(stream.ar+1)%AUDIO_QUEUE;stream.an--;
 *frame=(hls_audio_t){stream.playing.data,stream.playing.channels,stream.playing.rate,stream.playing.size,stream.playing.stamp};
 pthread_mutex_unlock(&stream.lock);return 1;
}
int hls_backend_pause(int pause){
 if(!stream.running)return -1;pthread_mutex_lock(&stream.lock);
 if(pause && !stream.paused){uint64_t t=clock_locked();stream.pause_clock=now_us();if(stream.started)stream.start_clock=stream.pause_clock-t*1000;stream.paused=1;}
 else if(!pause && stream.paused){if(stream.started)stream.start_clock+=now_us()-stream.pause_clock;stream.paused=0;}
 pthread_mutex_unlock(&stream.lock);return 0;
}
int hls_backend_error(void){if(!stream.running)return 0;pthread_mutex_lock(&stream.lock);int r=stream.error;pthread_mutex_unlock(&stream.lock);return r;}
const char *hls_backend_stage(void){if(!stream.running)return "HLS startup";pthread_mutex_lock(&stream.lock);const char *s=stream.stage;pthread_mutex_unlock(&stream.lock);return s?s:"HLS startup";}

int hls_backend_audio_eof(void){if(!stream.running)return 1;pthread_mutex_lock(&stream.lock);int result=(stream.ready && !stream.has_audio) || (stream.adecode.done && !stream.an);pthread_mutex_unlock(&stream.lock);return result;}
int hls_backend_buffering(void){if(!stream.running)return 0;pthread_mutex_lock(&stream.lock);clock_locked();int result=stream.buffering && !stream.finished && !stream.paused;pthread_mutex_unlock(&stream.lock);return result;}


void hls_backend_diagnostics(char *out,unsigned cap){
 if(!out || !cap)return;if(!stream.running){snprintf(out,cap,"Decoder idle");return;}pthread_mutex_lock(&stream.lock);uint64_t clock=clock_locked();
 snprintf(out,cap,"%s | %llu kbps | buffer %llu ms | dropped %u | %s",stream.buffering?"Buffering":stream.ready?"Playing":"Preparing",(unsigned long long)(stream.download_us?stream.download_bytes*8000/stream.download_us:0),(unsigned long long)(stream.last_stamp>clock?stream.last_stamp-clock:0),stream.dropped,stream.error?"decoder failed":stream.ready?"decoder ready":"decoder starting");pthread_mutex_unlock(&stream.lock);
}
