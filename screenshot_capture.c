//
//  ScreenshotCapture
//

#include <stdio.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavformat/avio.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavcodec/avcodec.h>

#define OK 0
#define ERROR -1

const size_t BUFFER_SIZE = 1024 * 8;

int get_frame(AVFormatContext *formatCtx, AVCodecContext *codecCtx, AVFrame *frame, int videoStream)
{
    AVPacket packet;
    int      frameFinished = 0;
    int      rc;
    
    /*if ((formatCtx->duration > 0) && ((((float_t) formatCtx->duration / AV_TIME_BASE) - second)) < 0.1)
    {
        return ERROR;
    }*/
    
    rc = ERROR;
    // Find the nearest frame
    while (!frameFinished && av_read_frame(formatCtx, &packet) >= 0)
    {
        // Is this a packet from the video stream?
        if (packet.stream_index == videoStream)
        {
            // Decode video frame
            avcodec_decode_video2(codecCtx, frame, &frameFinished, &packet);
            // Did we get a video frame?
            if (frameFinished)
            {
                rc = OK;
            }
        }
        // Free the packet that was allocated by av_read_frame
        av_packet_unref(&packet);
    }
    
    return rc;
}

static int get_thumb(const char* filename, const char* out_name)
{
    int              rc, ret, videoStream;
    AVFormatContext *formatCtx = NULL;
    AVCodecContext  *codecCtx = NULL;
    AVCodec         *codec = NULL;
    AVFrame         *frame = NULL;
    size_t           uncompressedSize;
    unsigned char   *bufferAVIO = NULL;
    char             value[10];
    int              threads = 2;
    AVCodecContext  *outCodecCtx = NULL;
    AVCodec         *outCodec = NULL;
    AVPacket        *packet = NULL;
    AVFrame         *frameRGB = NULL;
    struct SwsContext *scalerCtx = NULL;
    AVDictionary    *inputOptions = NULL;
    char             proto[8];
    AVDictionary    *dict = NULL;
    
    rc = ERROR;
    
    bufferAVIO = (unsigned char *)malloc(BUFFER_SIZE);
    if (!bufferAVIO)
    {
        fprintf(stderr, "Couldn't alloc AVIO buffer\n");
        goto exit;
    }
    
    formatCtx = avformat_alloc_context();
    if (!formatCtx)
    {
        fprintf(stderr, "Couldn't alloc avformat context\n");
        goto exit;
    }
    
    formatCtx->flags |= AVFMT_FLAG_NONBLOCK;
    
    av_url_split(proto, sizeof(proto), NULL, 0,
                 NULL, 0, NULL,
                 NULL, 0, filename);
    
    if (strcmp(proto, "rtmp") == 0)
    {
        av_dict_set(&inputOptions, "rtmp_live", "live", 0);
    }
    
    // Open video file
    if ((ret = avformat_open_input(&formatCtx, filename, NULL, &inputOptions)) != 0)
    {
        fprintf(stderr, "Couldn't open file %s, error: %d\n", filename, ret);
        goto exit;
    }
    
    // Retrieve stream information
    if (avformat_find_stream_info(formatCtx, NULL) < 0)
    {
        fprintf(stderr, "Couldn't find stream information\n");
        goto exit;
    }
    
    if ((formatCtx->duration > 0) && ((((float_t) formatCtx->duration / AV_TIME_BASE))) < 0.1)
    {
        fprintf(stderr, "seconds greater than duration\n");
        rc = ERROR;
        goto exit;
    }
    
    // Find the first video stream
    videoStream = av_find_best_stream(formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (videoStream == -1)
    {
        fprintf(stderr, "Didn't find a video stream\n");
        goto exit;
    }
    
    // Get a pointer to the codec context for the video stream
    codecCtx = formatCtx->streams[videoStream]->codec;
    
    sprintf(value, "%d", threads);
    av_dict_set(&dict, "threads", value, 0);
    
    // Open codec
    if ((avcodec_open2(codecCtx, codec, &dict)) < 0)
    {
        fprintf(stderr, "Could not open codec\n");
        goto exit;
    }
    
    //setup_parameters(cf, ctx, pFormatCtx, pCodecCtx);
    
    /*if (setup_filters(formatCtx, codecCtx, videoStream, &filterGraph, &buffersrcCtx, &buffersinkCtx) < 0)
    {
        goto exit;
    }*/
    
    // Allocate video frame
    frame = av_frame_alloc();
    
    if (frame == NULL) {
        fprintf(stderr, "Could not alloc frame memory\n");
        goto exit;
    }
    
    while ((rc = get_frame(formatCtx, codecCtx, frame, videoStream)) == 0)
    {
        
        /*if (frame->pict_type == 0) { // AV_PICTURE_TYPE_NONE
            needFlush = 1;
            break;
        }
        
        if (filter_frame(buffersrcCtx, buffersinkCtx, frame, frame) == AVERROR(EAGAIN)) {
            needFlush = 1;
            continue;
        }
        
        needFlush = 0;*/
        break;
    }
    
    if (frame == NULL || rc != OK) {
        fprintf(stderr, "Failed to get frame\n");
        goto exit;
    }
    
    /*if (needFlush)
    {
        if (filter_frame(buffersrc_ctx, buffersink_ctx, NULL, pFrame) < 0)
        {
            goto exit;
        }
        
        rc = OK;
    }*/
    
        
    /*AVFrame* newFrame = av_frame_alloc();
    int numBytes = avpicture_get_size(AV_PIX_FMT_RGB24, pFrame->width, pFrame->height);
    uint8_t *buffer= malloc(numBytes);
    
    avpicture_fill((AVPicture *)newFrame, buffer, AV_PIX_FMT_RGB24, pFrame->width, pFrame->height);
    
    img_convert((AVPicture *)newFrame, AV_PIX_FMT_RGB24,
                (AVPicture*)pFrame, pCodecCtx->pix_fmt, pCodecCtx->width,
                pCodecCtx->height);*/
    
    uncompressedSize = frame->width * frame->height * 3;
    
    //char *newBuffer = malloc(uncompressed_size);
    //av_image_copy_to_buffer(newBuffer, uncompressed_size, pFrame->data, pFrame->linesize, AV_PIX_FMT_RGB24, pFrame->width, pFrame->height);
    
    fprintf(stderr, "Pixel format: %d, %d\n", codecCtx->pix_fmt, AV_PIX_FMT_YUV420P);
    fprintf(stderr, "Colorspace: %d\n", frame->colorspace);
    
    scalerCtx = sws_getContext(codecCtx->width,
                               codecCtx->height,
                               codecCtx->pix_fmt,
                               codecCtx->width,
                               codecCtx->height,
                               AV_PIX_FMT_RGB24,
                               SWS_BILINEAR, //SWS_BICUBIC
                               NULL, NULL, NULL);
    if (!scalerCtx)
    {
        printf("sws_getContext() failed\n");
        goto exit;
    }
    
    frameRGB = av_frame_alloc();
    
    if (frameRGB == NULL)
    {
        printf("Failed to alloc frame\n");
        goto exit;
    }
    
    avpicture_alloc((AVPicture*)frameRGB, AV_PIX_FMT_RGB24, codecCtx->width, codecCtx->height);
    
    sws_scale(scalerCtx, frame->data, frame->linesize, 0, frame->height, frameRGB->data, frameRGB->linesize);
    
    outCodec = avcodec_find_encoder (AV_CODEC_ID_PNG);
    
    if (!outCodec)
    {
        fprintf(stderr, "Could not find png encode\n");
        goto exit;
    }
    
    outCodecCtx = avcodec_alloc_context3(outCodec);
    
    if (!outCodecCtx)
    {
        fprintf(stderr, "Failed to create codec context\n");
        goto exit;
    }
    
    outCodecCtx->bit_rate      = codecCtx->bit_rate;
    outCodecCtx->width         = codecCtx->width;
    outCodecCtx->height        = codecCtx->height;
    //outCodecCtx->pix_fmt       = codecCtx->pix_fmt;
    outCodecCtx->pix_fmt       = AV_PIX_FMT_RGB24;
    //outCodecCtx->codec_id      = AV_CODEC_ID_PNG;
    //outCodecCtx->codec_type    = AVMEDIA_TYPE_VIDEO;
    outCodecCtx->time_base.num = codecCtx->time_base.num;
    outCodecCtx->time_base.den = codecCtx->time_base.den;
    
    packet = av_packet_alloc();
    
    int got_packet;
    
    avcodec_encode_video2(outCodecCtx, packet, frameRGB, &got_packet);
    
    if (!got_packet)
    {
        fprintf(stderr, "Didn't get packet\n");
        goto exit;
    }
    
    FILE* f = fopen(out_name, "wb");
    fwrite(packet->data, packet->size, 1, f);
    fclose(f);
    
    rc = OK;
    
exit:
    if (packet)
    {
        av_packet_free(&packet);
    }
    
    if (outCodecCtx)
    {
        avcodec_free_context(&outCodecCtx);
    }
    
    if (frameRGB)
    {
        avpicture_free((AVPicture*)frameRGB);
        av_frame_free(&frameRGB);
    }
    
    // Free the YUV frame
    if (frame) av_frame_free(&frame);
    
    if (scalerCtx) sws_freeContext(scalerCtx);
    
    // Close the codec
    if (codecCtx) avcodec_close(codecCtx);
    
    // Close the video file
    if (formatCtx) avformat_close_input(&formatCtx);
    
    return rc;
}

int main(int argc, const char * argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "Too few arguments\n");
        return 1;
    }
    
    // Register all formats and codecs
    av_register_all();
    av_log_set_level(AV_LOG_ERROR);
    
    fprintf(stdout, "Path: %s\n", argv[1]);
    
    if (get_thumb(argv[1], argv[2]) != OK)
    {
        return 1;
    }
    
    return 0;
}
