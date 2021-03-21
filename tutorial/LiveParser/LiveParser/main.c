//
//  main.c
//  LiveParser
//
//  Created by zhangyoulun on 2021/3/20.
//

#include <stdio.h>
#include <assert.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>

// global quit flag
int quit = 0;

SDL_Window *gWindow = NULL;

SDL_Renderer *renderer = NULL;
SDL_Texture *texture = NULL;
SDL_Event event;
AVFrame *pict = NULL;
struct SwsContext *sws_ctx = NULL;

SDL_AudioSpec wanted_specs;
SDL_AudioSpec specs;
SDL_AudioDeviceID audioDeviceID;

typedef struct PacketQueue{
    AVPacketList *first_pkt;
    AVPacketList *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
}PacketQueue;

int packet_queue_init(PacketQueue *q);
int packet_queue_put(PacketQueue *q, AVPacket *packet);
int packet_queue_get(PacketQueue *q, AVPacket *packet, int block);

PacketQueue audioq;

int open_codec_content(AVCodecContext **dec_ctx, AVStream *stream);
int init_sdl(void);
int init_video_sdl(AVCodecContext *video_dec_ctx);
int init_audio_sdl(AVCodecContext *audio_dec_ctx);
void audio_callback(void *userdata, uint8_t *stream, int len);
int audio_decode_frame(AVCodecContext * audio_dec_ctx, uint8_t * audio_buf, int buf_size);
void close_sdl(void);
void show_video_sdl(AVCodecContext *video_dec_ctx,AVFrame *frame);
void handle_sdl(void);
int audio_resampling(AVCodecContext * audio_decode_ctx,AVFrame * decoded_audio_frame,enum AVSampleFormat out_sample_fmt,int out_channels,int out_sample_rate,uint8_t * out_buf);

int main(int argc, const char * argv[]) {
    int ret = 0;
    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *video_dec_ctx = NULL;
    AVCodecContext *audio_dec_ctx = NULL;
    int video_stream_idx = -1;
    int audio_stream_idx = -1;
    AVPacket pkt;
    AVFrame *frame;
    
    const char *liveUrl = "http://domain/app/stream.flv";
    
    av_log(NULL, AV_LOG_INFO, "ffmpeg version: %s\n", av_version_info());
    av_log(NULL, AV_LOG_INFO, "input url: %s\n", liveUrl);
    
    //打开input stream，读header，不打开codecs
    if(avformat_open_input(&fmt_ctx, liveUrl, NULL, NULL)!=0){
        av_log(NULL, AV_LOG_FATAL, "avformat_open_input fail\n");
        return -1;
    }
    
    //有些情况下，av_dump_format无法打印出相关信息，需要在调用av_dump_format之前调用avformat_find_stream_info
    //原因是有些文件格式没有header
    if((ret = avformat_find_stream_info(fmt_ctx, NULL))<0){
        av_log(NULL, AV_LOG_FATAL, "avformat_find_stream_info fail, err: %s\n", av_err2str(ret));
        return -1;
    }
    //打印input or output format的详细信息, 例如：duration, bitrate, streams, container, programs, metadata, side data, codec and time base.
    av_dump_format(fmt_ctx, 0, liveUrl, 0);
    
    //找到video_stream_id和audio_stream_id
    for(int i=0;i<fmt_ctx->nb_streams;i++){
        int codecType = fmt_ctx->streams[i]->codecpar->codec_type;
        if(codecType == AVMEDIA_TYPE_VIDEO){
            video_stream_idx = i;
        }else if(codecType == AVMEDIA_TYPE_AUDIO){
            audio_stream_idx = i;
        }
    }
    if(video_stream_idx<0 || audio_stream_idx<0){
        av_log(NULL, AV_LOG_FATAL, "video_stream_idx or audio_stream_idx not found, video_stream_idx: %d, audio_stream_idx: %d\n", video_stream_idx, audio_stream_idx);
        return -1;
    }
    
    //初始化video_dec_ctx, audio_dec_ctx
    if((ret = open_codec_content(&video_dec_ctx, fmt_ctx->streams[video_stream_idx]))<0){
        av_log(NULL, AV_LOG_FATAL, "open_codec_content fail\n");
        return -1;
    }
    av_log(NULL, AV_LOG_INFO, "width: %d, height: %d\n", video_dec_ctx->width, video_dec_ctx->height);
    if((ret = open_codec_content(&audio_dec_ctx, fmt_ctx->streams[audio_stream_idx]))<0){
        av_log(NULL, AV_LOG_FATAL, "open_codec_content fail\n");
        return -1;
    }
    av_log(NULL, AV_LOG_INFO, "sample_rate: %d\n", audio_dec_ctx->sample_rate);
    
    //init packet
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;
    
    frame = av_frame_alloc();
    if(!frame){
        av_log(NULL, AV_LOG_FATAL, "av_frame_alloc fail\n");
        return -1;
    }
    
    //
    if(init_sdl()<0){
        av_log(NULL, AV_LOG_FATAL, "init_sdl fail\n");
        return -1;
    }
    //
    
    //
    if(init_video_sdl(video_dec_ctx)<0){
        av_log(NULL, AV_LOG_FATAL, "init_video_sdl fail\n");
        return -1;
    }
    //
    
    //
    if(init_audio_sdl(audio_dec_ctx)<0){
        av_log(NULL, AV_LOG_FATAL, "init_audio_sdl fail\n");
        return -1;
    }
    //
    
    while((ret = av_read_frame(fmt_ctx, &pkt))==0){//av_read_packet被淘汰了，用av_read_frame
        av_log(NULL, AV_LOG_INFO, "pkt info, type=%s, pts: %lld, dts: %lld, size: %d\n", pkt.stream_index==video_stream_idx?"video":(pkt.stream_index==audio_stream_idx?"audio":"unknown"), pkt.pts, pkt.dts, pkt.size);
        
        if(pkt.stream_index==video_stream_idx){
            ret = avcodec_send_packet(video_dec_ctx, &pkt);
            if(ret!=0){
                av_log(NULL, AV_LOG_FATAL, "avcodec_send_packet fail\n");
                return -1;
            }
            
            ret = avcodec_receive_frame(video_dec_ctx, frame);
            if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
                continue;//注意！
            }else if(ret<0){
                av_log(NULL, AV_LOG_FATAL, "avcodec_receive_frame fail, err: %s\n", av_err2str(ret));
                return -1;
            }
            av_log(NULL, AV_LOG_INFO, "\t\tframe info, type: video, pts: %lld, width: %d, height: %d, picture_type: %c\n", frame->pts, frame->width, frame->height, av_get_picture_type_char(frame->pict_type));
            
            //
            show_video_sdl(video_dec_ctx, frame);
            //
            
        }else if(pkt.stream_index==audio_stream_idx){
            packet_queue_put(&audioq, &pkt);
        }
        
        
        //
        handle_sdl();
        if(quit){
            break;
        }
        //
    }
    av_log(NULL, AV_LOG_FATAL, "av_read_frame fail, err: %s\n", av_err2str(ret));
    
    //--------------------------------------------------------
    //end
    //--------------------------------------------------------
    //pFormatCtx使用avformat_open_input打开后，必须用avformat_close_input关闭
    avformat_close_input(&fmt_ctx);
    //和avcodec_alloc_context3配对
    avcodec_free_context(&video_dec_ctx);
    avcodec_free_context(&audio_dec_ctx);
    //和av_alloc_frame配对
    av_frame_free(&frame);
    //和init_sdl配对
    close_sdl();
    av_log(NULL, AV_LOG_INFO, "end\n");
    return 0;
}

int open_codec_content(AVCodecContext **dec_ctx, AVStream *stream){
    AVCodec *dec = NULL;
    int ret = 0;
    
    dec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!dec){
        av_log(NULL, AV_LOG_FATAL, "avcodec_find_decoder fail, codec_id=%d\n", stream->codecpar->codec_id);
        return -1;
    }
    
    *dec_ctx = avcodec_alloc_context3(dec);
    if(!*dec_ctx){
        av_log(NULL, AV_LOG_FATAL, "avcodec_alloc_context3 fail\n");
        return -1;
    }
    
    if((ret = avcodec_parameters_to_context(*dec_ctx, stream->codecpar))<0){
        av_log(NULL, AV_LOG_FATAL, "avcodec_parameters_to_context fail, err = %s\n",av_err2str(ret));
        return -1;
    }
    
    if((ret = avcodec_open2(*dec_ctx, dec, NULL))<0){
        av_log(NULL, AV_LOG_FATAL, "avcodec_open2 fail, err = %s\n", av_err2str(ret));
        return -1;
    }
    return 0;
}

int init_sdl(void){
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)<0){
        av_log(NULL,AV_LOG_FATAL, "SDL_Init fail, err: %s\n", SDL_GetError());
        return -1;
    }
    return 0;
}

int init_video_sdl(AVCodecContext *video_dec_ctx){
    gWindow = SDL_CreateWindow("player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, video_dec_ctx->width/2, video_dec_ctx->height/2, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
    if(!gWindow){
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateWindow fail, err: %s\n", SDL_GetError());
        return -1;
    }
    
    SDL_GL_SetSwapInterval(1);
    
//    gScreenSurface = SDL_GetWindowSurface(gWindow);
    
    sws_ctx = sws_getContext(video_dec_ctx->width, video_dec_ctx->height, video_dec_ctx->pix_fmt, video_dec_ctx->width, video_dec_ctx->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
    if(!sws_ctx){
        av_log(NULL, AV_LOG_FATAL, "sws_getContext fail\n");
        return -1;
    }
    
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, video_dec_ctx->width, video_dec_ctx->height, 32);
    if(numBytes<0){
        av_log(NULL, AV_LOG_FATAL, "av_image_get_buffer_size fail, err: %s\n", av_err2str(numBytes));
        return -1;
    }
    uint8_t *buffer = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
    if(!buffer){
        av_log(NULL, AV_LOG_FATAL, "av_malloc fail\n");
        return -1;
    }
    
    pict = av_frame_alloc();
    if(!pict){
        av_log(NULL, AV_LOG_FATAL, "av_frame_alloc fail\n");
        return -1;
    }
    int ret = av_image_fill_arrays(pict->data, pict->linesize, buffer, AV_PIX_FMT_YUV420P, video_dec_ctx->width, video_dec_ctx->height, 32);
    if(ret<0){
        av_log(NULL, AV_LOG_FATAL, "av_image_fill_arrays fail, err: %s\n",av_err2str(ret));
        return -1;
    }
    
    renderer = SDL_CreateRenderer(gWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if(!renderer){
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateRenderer fail, err: %s\n", SDL_GetError());
        return -1;
    }
    
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, video_dec_ctx->width, video_dec_ctx->height);
    if(!texture){
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateTexture fail, err: %s\n", SDL_GetError());
        return -1;
    }
    
    return 0;
}

int init_audio_sdl(AVCodecContext *audio_dec_ctx){
    wanted_specs.freq = audio_dec_ctx->sample_rate;
    wanted_specs.format = AUDIO_S16SYS;
    wanted_specs.channels = audio_dec_ctx->channels;
    wanted_specs.silence = 0;
    wanted_specs.samples = 1024;
    wanted_specs.callback = audio_callback;
    wanted_specs.userdata = audio_dec_ctx;
    
    audioDeviceID = SDL_OpenAudioDevice(NULL, 0, &wanted_specs, &specs, SDL_AUDIO_ALLOW_FORMAT_CHANGE);
    if(audioDeviceID==0){
        av_log(NULL, AV_LOG_FATAL, "SDL_OpenAudioDevice fail, err: %s\n", SDL_GetError());
        return -1;
    }
    
//    packet_queue_init(&audioq);
    if(packet_queue_init(&audioq)==-1){
        av_log(NULL, AV_LOG_FATAL, "packet_queue_init fail\n");
        return -1;
    }
    
    // start playing audio on the given audio device
    SDL_PauseAudioDevice(audioDeviceID, 0);
    
    return 0;
}

void show_video_sdl(AVCodecContext *video_dec_ctx,AVFrame *frame){
    int ret;
    
    ret = sws_scale(sws_ctx, (uint8_t const * const *)frame->data, frame->linesize, 0, video_dec_ctx->height, pict->data, pict->linesize);
    av_log(NULL, AV_LOG_INFO, "\t\t\t\tpict info, width: %d, height: %d, linesize[0]: %d, ret: %d\n", pict->width, pict->height, pict->linesize[0], ret);
    
    SDL_Rect rect;
    rect.x = 0;
    rect.y = 0;
    rect.w = video_dec_ctx->width;
    rect.h = video_dec_ctx->height;
    
    ret = SDL_UpdateYUVTexture(texture, &rect, pict->data[0], pict->linesize[0], pict->data[1], pict->linesize[1], pict->data[2], pict->linesize[2]);
    if(ret!=0){
        av_log(NULL, AV_LOG_ERROR, "SDL_UpdateYUVTexture fail\n");
        return;
    }
    
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

//这个必须加，不然不出画面
void handle_sdl(void){
    SDL_PollEvent(&event);
    switch(event.type)
    {
        case SDL_QUIT:
        {
            SDL_Quit();

            quit = 1;
        }
            break;
            
        default:
        {
            // nothing to do
        }
            break;
    }
}

void close_sdl(void){
    SDL_DestroyWindow(gWindow);
    gWindow = NULL;
    
    SDL_Quit();
}

int packet_queue_init(PacketQueue *q)
{
    // alloc memory for the audio queue
    memset(q,0,sizeof(PacketQueue));
    
    // Returns the initialized and unlocked mutex or NULL on failure
    q->mutex = SDL_CreateMutex();
    if (!q->mutex){
        // could not create mutex
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex Error: %s.\n", SDL_GetError());
        return -1;
    }
    
    // Returns a new condition variable or NULL on failure
    q->cond = SDL_CreateCond();
    if (!q->cond){
        // could not create condition variable
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond Error: %s.\n", SDL_GetError());
        return -1;
    }
    return 0;
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    // alloc the new AVPacketList to be inserted in the audio PacketQueue
    AVPacketList * avPacketList;
    avPacketList = av_malloc(sizeof(AVPacketList));
    
    // check the AVPacketList was allocated
    if (!avPacketList){
        return -1;
    }
    
    // add reference to the given AVPacket
    avPacketList->pkt = * pkt;
    
    // the new AVPacketList will be inserted at the end of the queue
    avPacketList->next = NULL;
    
    // lock mutex
    SDL_LockMutex(q->mutex);
    
    // check the queue is empty
    if (!q->last_pkt){
        // if it is, insert as first
        q->first_pkt = avPacketList;
    }else{
        // if not, insert as last
        q->last_pkt->next = avPacketList;
    }
    
    // point the last AVPacketList in the queue to the newly created AVPacketList
    q->last_pkt = avPacketList;
    
    // increase by 1 the number of AVPackets in the queue
    q->nb_packets++;
    
    // increase queue size by adding the size of the newly inserted AVPacket
    q->size += avPacketList->pkt.size;
    
    // notify packet_queue_get which is waiting that a new packet is available
    SDL_CondSignal(q->cond);
    
    // unlock mutex
    SDL_UnlockMutex(q->mutex);
    
    return 0;
}

int packet_queue_get(PacketQueue * q, AVPacket * pkt, int block)
{
    int ret;
    
    AVPacketList * avPacketList;
    
    // lock mutex
    SDL_LockMutex(q->mutex);
    
    for (;;){
        // check quit flag
        if (quit){
            ret = -1;
            break;
        }
        
        // point to the first AVPacketList in the queue
        avPacketList = q->first_pkt;
        
        // if the first packet is not NULL, the queue is not empty
        if (avPacketList){
            // place the second packet in the queue at first position
            q->first_pkt = avPacketList->next;
            
            // check if queue is empty after removal
            if (!q->first_pkt){
                // first_pkt = last_pkt = NULL = empty queue
                q->last_pkt = NULL;
            }
            
            // decrease the number of packets in the queue
            q->nb_packets--;
            
            // decrease the size of the packets in the queue
            q->size -= avPacketList->pkt.size;
            
            // point pkt to the extracted packet, this will return to the calling function
            *pkt = avPacketList->pkt;
            
            // free memory
            av_free(avPacketList);
            
            ret = 1;
            break;
        }else if (!block){
            ret = 0;
            break;
        }else{
            // unlock mutex and wait for cond signal, then lock mutex again
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    
    // unlock mutex
    SDL_UnlockMutex(q->mutex);
    
    return ret;
}

/**
 * Pull in data from audio_decode_frame(), store the result in an intermediary
 * buffer, attempt to write as many bytes as the amount defined by len to
 * stream, and get more data if we don't have enough yet, or save it for later
 * if we have some left over.
 *
 * @param   userdata    the pointer we gave to SDL.
 * @param   stream      the buffer we will be writing audio data to.
 * @param   len         the size of that buffer.
 */
void audio_callback(void *userdata, Uint8 *stream, int len)
{
    av_log(NULL, AV_LOG_INFO, "audio_callback start\n");
    AVCodecContext *audio_dec_ctx = (AVCodecContext *)userdata;
    
//    int len1 = -1;
//    int audio_size = -1;
    
#define MAX_AUDIO_FRAME_SIZE 192000
    // The size of audio_buf is 1.5 times the size of the largest audio frame
    // that FFmpeg will give us, which gives us a nice cushion.
    static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
    static unsigned int audio_buf_size = 0;
    static unsigned int audio_buf_index = 0;
    
    while (len > 0){
        // check global quit flag
        if (quit){
            return;
        }
        
        if(audio_buf_index >= audio_buf_size){//empty
            int audio_size = audio_decode_frame(audio_dec_ctx, audio_buf, sizeof(audio_buf));
            if(audio_size>0){//got data
                audio_buf_size = audio_size;
                audio_buf_index = 0;
            }
        }else{//has data
            int len1 = audio_buf_size - audio_buf_index;
            if(len1>len){
                len1 = len;
            }
            memcpy(stream, (uint8_t *)(audio_buf+audio_buf_index), len1);
            audio_buf_index += len1;
            stream+=len1;
            len-=len1;
        }
    }
}

int audio_decode_frame(AVCodecContext *audio_dec_ctx, uint8_t *audio_buf, int buf_size){
    int ret = 0;
    AVPacket *audioPacket = av_packet_alloc();
    
    // allocate a new frame, used to decode audio packets
    static AVFrame *audioFrame = NULL;
    audioFrame = av_frame_alloc();
    if (!audioFrame){
        printf("Could not allocate AVFrame.\n");
        return -1;
    }
    
    while(1){
        if(quit){
            return -1;
        }
        
        while(1){
            ret = avcodec_receive_frame(audio_dec_ctx, audioFrame);
            if(ret==AVERROR(EAGAIN) || ret==AVERROR_EOF){
                break;//注意!
            }else if(ret<0){
                av_log(NULL, AV_LOG_FATAL, "avcodec_receive_frame fail\n");
                return -1;
            }
            //got frame
            int data_size = audio_resampling(audio_dec_ctx, audioFrame, AV_SAMPLE_FMT_S16, audio_dec_ctx->channels, audio_dec_ctx->sample_rate, audio_buf);
            return data_size;
        }
        
        //get packet
        ret = packet_queue_get(&audioq, audioPacket, 1);
        if(ret<0){
            av_log(NULL, AV_LOG_FATAL, "packet_queue_get fail\n");
            return -1;
        }
        
        //send packet
        ret = avcodec_send_packet(audio_dec_ctx, audioPacket);
        if(ret!=0){
            av_log(NULL, AV_LOG_FATAL, "avcodec_send_packet fail\n");
            return -1;
        }
    }
    
    return 0;
}

/**
 * Resample the audio data retrieved using FFmpeg before playing it.
 *
 * @param   audio_decode_ctx    the audio codec context retrieved from the original AVFormatContext.
 * @param   decoded_audio_frame the decoded audio frame.
 * @param   out_sample_fmt      audio output sample format (e.g. AV_SAMPLE_FMT_S16).
 * @param   out_channels        audio output channels, retrieved from the original audio codec context.
 * @param   out_sample_rate     audio output sample rate, retrieved from the original audio codec context.
 * @param   out_buf             audio output buffer.
 *
 * @return                      the size of the resampled audio data.
 */
int audio_resampling(AVCodecContext * audio_decode_ctx,AVFrame * decoded_audio_frame,enum AVSampleFormat out_sample_fmt,int out_channels,int out_sample_rate,uint8_t * out_buf)
{
    // check global quit flag
    if (quit){
        return -1;
    }
    
    SwrContext * swr_ctx = NULL;
    int ret = 0;
    int64_t in_channel_layout = audio_decode_ctx->channel_layout;
    int64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
    int out_nb_channels = 0;
    int out_linesize = 0;
    int in_nb_samples = 0;
    int out_nb_samples = 0;
    int max_out_nb_samples = 0;
    uint8_t ** resampled_data = NULL;
    int resampled_data_size = 0;
    
    swr_ctx = swr_alloc();
    
    if (!swr_ctx){
        printf("swr_alloc error.\n");
        return -1;
    }
    
    // get input audio channels
    in_channel_layout = (audio_decode_ctx->channels ==
                         av_get_channel_layout_nb_channels(audio_decode_ctx->channel_layout)) ?   // 2
    audio_decode_ctx->channel_layout :
    av_get_default_channel_layout(audio_decode_ctx->channels);
    
    // check input audio channels correctly retrieved
    if (in_channel_layout <= 0){
        printf("in_channel_layout error.\n");
        return -1;
    }
    
    // set output audio channels based on the input audio channels
    if (out_channels == 1){
        out_channel_layout = AV_CH_LAYOUT_MONO;
    }else if (out_channels == 2){
        out_channel_layout = AV_CH_LAYOUT_STEREO;
    }else{
        out_channel_layout = AV_CH_LAYOUT_SURROUND;
    }
    
    // retrieve number of audio samples (per channel)
    in_nb_samples = decoded_audio_frame->nb_samples;
    if (in_nb_samples <= 0){
        printf("in_nb_samples error.\n");
        return -1;
    }
    
    // Set SwrContext parameters for resampling
    av_opt_set_int(swr_ctx,"in_channel_layout",in_channel_layout,0);
    
    // Set SwrContext parameters for resampling
    av_opt_set_int(swr_ctx,"in_sample_rate",audio_decode_ctx->sample_rate,0);
    
    // Set SwrContext parameters for resampling
    av_opt_set_sample_fmt(swr_ctx,"in_sample_fmt",audio_decode_ctx->sample_fmt,0);
    
    // Set SwrContext parameters for resampling
    av_opt_set_int(swr_ctx,"out_channel_layout",out_channel_layout,0);
    
    // Set SwrContext parameters for resampling
    av_opt_set_int(swr_ctx,"out_sample_rate",out_sample_rate,0);
    
    // Set SwrContext parameters for resampling
    av_opt_set_sample_fmt(swr_ctx,"out_sample_fmt",out_sample_fmt,0);
    
    // Once all values have been set for the SwrContext, it must be initialized
    // with swr_init().
    ret = swr_init(swr_ctx);;
    if (ret < 0){
        printf("Failed to initialize the resampling context.\n");
        return -1;
    }
    
    max_out_nb_samples = out_nb_samples = (int)av_rescale_rnd(in_nb_samples,out_sample_rate,audio_decode_ctx->sample_rate,AV_ROUND_UP);
    
    // check rescaling was successful
    if (max_out_nb_samples <= 0){
        printf("av_rescale_rnd error.\n");
        return -1;
    }
    
    // get number of output audio channels
    out_nb_channels = av_get_channel_layout_nb_channels(out_channel_layout);
    
    ret = av_samples_alloc_array_and_samples(&resampled_data,&out_linesize,out_nb_channels,out_nb_samples,out_sample_fmt,0);
    
    if (ret < 0){
        printf("av_samples_alloc_array_and_samples() error: Could not allocate destination samples.\n");
        return -1;
    }
    
    // retrieve output samples number taking into account the progressive delay
    out_nb_samples = (int) av_rescale_rnd(swr_get_delay(swr_ctx,audio_decode_ctx->sample_rate) + in_nb_samples,out_sample_rate,audio_decode_ctx->sample_rate,AV_ROUND_UP);
    
    // check output samples number was correctly retrieved
    if (out_nb_samples <= 0){
        printf("av_rescale_rnd error\n");
        return -1;
    }
    
    if (out_nb_samples > max_out_nb_samples){
        // free memory block and set pointer to NULL
        av_free(resampled_data[0]);
        
        // Allocate a samples buffer for out_nb_samples samples
        ret = av_samples_alloc(resampled_data,&out_linesize,out_nb_channels,out_nb_samples,out_sample_fmt,1);
        
        // check samples buffer correctly allocated
        if (ret < 0){
            printf("av_samples_alloc failed.\n");
            return -1;
        }
        
        max_out_nb_samples = out_nb_samples;
    }
    
    // do the actual audio data resampling
    ret = swr_convert(swr_ctx,resampled_data,out_nb_samples,(const uint8_t **) decoded_audio_frame->data,decoded_audio_frame->nb_samples);
    
    // check audio conversion was successful
    if (ret < 0){
        printf("swr_convert_error.\n");
        return -1;
    }
    
    // Get the required buffer size for the given audio parameters
    resampled_data_size = av_samples_get_buffer_size(&out_linesize,out_nb_channels,ret,out_sample_fmt,1);
    
    // check audio buffer size
    if (resampled_data_size < 0){
        printf("av_samples_get_buffer_size error.\n");
        return -1;
    }

    // copy the resampled data to the output buffer
    memcpy(out_buf, resampled_data[0], resampled_data_size);
    
    /*
     * Memory Cleanup.
     */
    if (resampled_data){
        // free memory block and set pointer to NULL
        av_freep(&resampled_data[0]);
    }
    
    av_freep(&resampled_data);
    resampled_data = NULL;
    
    // Free the given SwrContext and set the pointer to NULL
    swr_free(&swr_ctx);
    
    return resampled_data_size;
}
