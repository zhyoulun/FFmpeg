//
//  main.c
//  LiveParser
//
//  Created by zhangyoulun on 2021/3/20.
//

#include <stdio.h>
#include <libavutil/avutil.h>
#include <libavformat/avformat.h>

int process_video(AVStream *streams);
int open_codec_content(AVCodecContext **dec_ctx, AVStream *stream);

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
            
        }else if(pkt.stream_index==audio_stream_idx){
            ret = avcodec_send_packet(audio_dec_ctx, &pkt);
            if(ret!=0){
                av_log(NULL, AV_LOG_FATAL, "avcodec_send_packet fail\n");
                return -1;
            }
            
            do{
                ret = avcodec_receive_frame(audio_dec_ctx, frame);
                if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
                    break;//注意！
                }else if(ret<0){
                    av_log(NULL, AV_LOG_FATAL, "avcodec_receive_frame fail, err: %s\n", av_err2str(ret));
                    return -1;
                }
                av_log(NULL, AV_LOG_INFO, "\t\tframe info, type: audio, pts: %lld, channels: %d\n", frame->pts, frame->channels);
            }while(1);
        }
        
        
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
