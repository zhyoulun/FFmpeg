//
//  main.c
//  tutorial04
//
//  Created by zhangyoulun on 2020/10/15.
//

#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>

//?
typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

typedef struct VideoState {
    AVFormatContext *pFormatCtx;
    int videoStream, audioStream;
    
    AVStream *audio_st;
    PacketQueue audioq;
    AVFrame audio_frame;
    AVPacket audio_packet;
    uint8_t audio_buf[(*3)/2];
    
    AVStream *video_st;
    PacketQueue videoq;
    
    
} VideoState;

int main(int argc, const char * argv[]) {
    // insert code here...
    printf("Hello, World!\n");
    return 0;
}
