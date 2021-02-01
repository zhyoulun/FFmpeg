//
//  main.c
//  tutorial04
//
//  Created by zhangyoulun on 2020/10/15.
//

#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <string.h>

/**
 * Maximum number of samples per channel in an audio frame.
 */
#define MAX_AUDIO_FRAME_SIZE 192000

#define VIDEO_PICTURE_QUEUE_SIZE 1

/**
 * Queue structure used to store AVPackets.
 */
typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

/**
 * Queue structure used to store processed video frames.
 */
typedef struct VideoPicture{
    //    SDL_Overlay *bmp;//come from SDL1
    AVFrame *frame;
    int width, height;//source height & width
    int allocated;
} VideoPicture;

typedef struct VideoState {
    AVFormatContext *pFormatCtx;
    int videoStream, audioStream;
    
    AVStream *audio_st;
    PacketQueue audioq;
    AVFrame audio_frame;
    AVPacket audio_packet;
    uint8_t *audio_packet_data;
    int audio_packet_size;
    uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE*3)/2];//音频缓冲区相关的数据
    unsigned int audio_buf_size;//音频缓冲区相关的数据
    unsigned int audio_buf_index;//音频缓冲区相关的数据
    
    AVStream *video_st;
    AVCodecContext *video_ctx;
    PacketQueue videoq;//视频数据队列
    
    VideoPicture pictq[VIDEO_PICTURE_QUEUE_SIZE];//视频数据缓冲区,存储解码后的视频帧
    int pictq_size, pictq_rindex,pictq_windex;
    SDL_mutex *pictq_mutex;
    SDL_cond *pictq_cond;
    
    SDL_Texture *texture;
    SDL_Renderer *renderer;
    
    SDL_Thread *parse_tid;
    SDL_Thread *video_tid;
    
    char filename[1024];//媒体文件名
    int quit;//退出标志
    
    AVIOContext *io_context;
    struct SwsContext *sws_ctx;
} VideoState;

int decode_thread(void *arg);
int stream_component_open(VideoState *is, int stream_index);

void packet_queue_init(PacketQueue *q);
int packet_queue_put(PacketQueue *queue, AVPacket *packet);
static int packet_queue_get(PacketQueue *queue, AVPacket *packet, int blocking);

//Global SDL_Window reference
SDL_Window *screen;
SDL_mutex * screen_mutex;
VideoState *global_video_state;

int main(int argc, const char * argv[]) {
    SDL_Event event;
    VideoState *videoState;
    videoState = av_mallocz(sizeof(VideoState));//分配内存并将内存初始化为 0
    
    strcpy(videoState->filename, "/Users/zhangyoulun/codes/video/mot_1500k_5s.flv");
    videoState->pictq_mutex = SDL_CreateMutex();
    videoState->pictq_cond = SDL_CreateCond();
    
    videoState->parse_tid = SDL_CreateThread(decode_thread, "decode", videoState);
    if(!videoState->parse_tid){
        av_free(videoState);
        return -1;
    }
    
    SDL_Delay(1000);
    
    return 0;
}

int decode_thread(void *arg)
{
    VideoState *videoState = (VideoState *)arg;
    int ret =0;
    
    ret = avformat_open_input(&videoState->pFormatCtx, videoState->filename, NULL, NULL);
    if(ret<0){
        printf("Could not open file %s\n", videoState->filename);
        return -1;
    }
    
    AVPacket pkt1;
    AVPacket *packet = &pkt1;
    
    videoState->videoStream = -1;
    videoState->audioStream = -1;
    
    ret = avformat_find_stream_info(videoState->pFormatCtx, NULL);
    if (ret<0){
        printf("could not find stream information %s\n", videoState->filename);
        return -1;
    }
    
    av_dump_format(videoState->pFormatCtx, 0, videoState->filename, 0);
    
    for(int i=0;i<videoState->pFormatCtx->nb_streams;i++){
        if(videoState->pFormatCtx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO && videoState->videoStream<0){
            videoState->videoStream = i;
        }
        if(videoState->pFormatCtx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO && videoState->audioStream<0){
            videoState->audioStream = i;
        }
    }
    
    if(videoState->videoStream==-1 || videoState->audioStream==-1){
        printf("could not find video or audio stream\n");
        //        goto fail;
        return -1;
    }
    
    ret = stream_component_open(videoState, videoState->videoStream);
    if(ret<0){
        printf("could not open video codec\n");
//        goto fail;
        return -1;
    }
    ret = stream_component_open(videoState, videoState->audioStream);
    if(ret<0){
        printf("could not open audio codec\n");
//        goto fail;
        return -1;
    }
    
    //todo
    
    return 0;
}

int stream_component_open(VideoState *videoState, int stream_index){
    int ret;
    
    AVCodec *codec = NULL;
    codec = avcodec_find_decoder(videoState->pFormatCtx->streams[stream_index]->codecpar->codec_id);
    if (codec==NULL){
        printf("unsupport codec\n");
        return -1;
    }
    
    AVCodecContext *codecCtx = NULL;
    codecCtx = avcodec_alloc_context3(codec);
    ret = avcodec_parameters_to_context(codecCtx, videoState->pFormatCtx->streams[stream_index]->codecpar);
    if (ret!=0){
        printf("could not copy codec context\n");
        return -1;
    }
    ret = avcodec_open2(codecCtx, codec, NULL);
    if (ret<0){
        printf("unsupport codec\n");
        return -1;
    }
    
    switch (codecCtx->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            videoState->audio_st = videoState->pFormatCtx->streams[stream_index];
            videoState->audio_ctx = codecCtx;
            videoState->audio_buf_size = 0
            videoState->audio_buf_index = 0;
            
            memset(&videoState->audio_packet,0,sizeof(videoState->audio_packet));
            
            packet_queue_init(&videoState->audioq);
            
            SDL_PauseAudio(0);
            break;
        case AVMEDIA_TYPE_VIDEO:
        {
            videoState->video_st = videoState->pFormatCtx->streams[stream_index];
            videoState->video_ctx = codecCtx;
            
            
            packet_queue_init(&videoState->videoq);
            
            videoState->video_tid = SDL_CreateThread(video_thread, "video", videoState);
            
            videoState->sws_ctx = sws_getContext(videoState->video_ctx->width, videoState->video_ctx->height, videoState->video_ctx->pix_fmt, videoState->video_ctx->width, videoState->video_ctx->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
        }
            
        {
            screen = SDL_CreateWindow("FFmpeg SDL video player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, videoState->video_ctx->width/2, videoState->video_ctx->height/2, SDL_WINDOW_OPENGL|SDL_WINDOW_ALLOW_HIGHDPI);
            if(!screen){
                printf("could not create window\n");
                return -1;
            }
            
            SDL_GL_SetSwapInterval(1);
            scrren_mutex = SDL_CreateMutex();
            
            videoState->renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC|SDL_RENDERER_TARGETTEXTURE);
            videoState->texture = SDL_CreateTexture(videoState->renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, videoState->video_ctx->width, videoState->video_ctx->height);
        }
            break;
        default:
            break;
    }
    return 0;
}

int video_thread(void *arg){
    VideoState *videoState = (VideoState *)arg;
    int ret;
    
    AVPacket *packet = av_packet_alloc();
    if(packet==NULL){
        printf("could not alloc packet\n");
        return -1;
    }
    
    int frameFinished = -1;
    static AVFrame *pFrame = NULL;
    pFrame = av_frame_alloc();
    if(!pFrame){
        printf("could not allocate AVFrame\n");
        return -1;
    }
    
    for(;;){
        if(global_video_state->quit){
            break;
        }
        packet_queue_get(&videoState->videoq, packet);
        ret = avcodec_send_packet(videoState->video_ctx, packet);
        if(ret<0){
            printf("error sending packet for decoding\n");
            return -1;
        }
        while(ret>=0){
            ret = avcodec_receive_frame(videoState->video_ctx, pFrame);
            if(ret==AVERROR(EAGAIN) || ret ==AVERROR_EOF){
                break;
            }else if(ret<0){
                printf("error while decoding\n");
                return -1;
            }else{
                frameFinished = 1;
            }
            
            if(frameFinished){
                if(queue_picture(videoState, pFrame)<0){
                    break;
                }
            }
        }
        
        av_packet_unref(packet);
    }
    
    av_frame_free(&pFrame);
    av_free(pFrame);
    
    return 0;
}

void alloc_picture(void *userdata){
    VideoState *videoState = (VideoState *)userdata;
    
    VideoPicture *videoPicture;
    videoPicture = &videoState->pictq[videoState->pictq_windex];
    
    if(videoPicture->frame){
        av_frame_free(&videoPicture->frame);
        av_free(videoPicture->frame);
    }
    
    SDL_LockMutex(screen_mutex);
    
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, videoState->video_ctx->width, videoState->video_ctx->height, 32);
    
    uint8_t *buffer = NULL;
    buffer = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
    
    videoPicture->frame = av_frame_alloc();
    if(videoPicture->frame==NULL){
        printf("could not allocate frame\n");
        return;
    }
    
    av_image_fill_arrays(videoPicture->frame->data, videoPicture->frame->linesize, buffer, AV_PIX_FMT_YUV420P, videoState->video_ctx->width, videoState->video_ctx->height, 32);
    
    SDL_UnlockMutex(screen_mutex);
    
    videoPicture->width = videoState->video_ctx->width;
    videoPicture->height = videoState->video_ctx->height;
    videoPicture->allocated = 1;
}

int queue_picture(VideoState *videoState, AVFrame *pFrame){
    SDL_LockMutex(videoState->pictq_mutex);
    
    //todo
}

void packet_queue_init(PacketQueue *q){
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    if(!q->mutex){
        printf("SDL_CreateMutex error: %s\n", SDL_GetError());
        return;
    }
    q->cond = SDL_CreateCond();
    if(!q->cond){
        printf("SDL_CreateCond error: %s\n", SDL_GetError());
        return;
    }
}

int packet_queue_put(PacketQueue *queue, AVPacket *packet){
    AVPacketList *avPacketList;
    avPacketList = av_malloc(sizeof(AVPacketList));
    if(!avPacketList){
        return -1;
    }
    
    avPacketList->pkt = *packet;
    avPacketList->next = NULL;
    
    SDL_LockMutex(queue->mutex);
    
    if(!queue->last_pkt){
        queue->first_pkt = avPacketList;
        queue->last_pkt = avPacketList;
    }else{
        queue->last_pkt->next = avPacketList;
        queue->last_pkt = avPacketList;
    }
    
    queue->nb_packets++;
    
    queue->size += avPacketList->pkt.size;
    
    SDL_CondSignal(queue->cond);
    SDL_UnlockMutex(queue->mutex);
    
    return 0;
}

static int packet_queue_get(PacketQueue *queue, AVPacket *packet){
    int ret = 0;
    
    AVPacketList *avPacketList;
    
    SDL_LockMutex(queue->mutex);
    for(;;){
        //todo add global quit
        avPacketList = queue->first_pkt;
        if(avPacketList){
            queue->first_pkt = avPacketList->next;
            if(!queue->first_pkt){
                queue->last_pkt = NULL;
            }
            queue->nb_packets--;
            queue->size -= avPacketList->pkt.size;
            *packet = avPacketList->pkt;
            av_free(avPacketList);
            ret = 1;
            break;
        }else{
            SDL_CondWait(queue->cond, queue->mutex);
        }
    }
    
    SDL_UnlockMutex(queue->mutex);
    return ret;
}
