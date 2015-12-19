//
//  main.c
//  ScreenshotCapture
//
//  Created by Elviss Strazdins on 14/12/15.
//  Copyright © 2015 Elviss Strazdins. All rights reserved.
//

#include <stdio.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavformat/avio.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavcodec/avcodec.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define OK 0
#define ERROR -1

#define BUFFER_SIZE 1024 * 8
#define MEMORY_STEP 1024

void log_str(const char * format, ... )
{
    va_list vl;
    va_start(vl, format);
    
    vprintf(format, vl);
    printf("\n");
    
    va_end(vl);
}

int filter_frame(AVFilterContext *buffersrc_ctx, AVFilterContext *buffersink_ctx, AVFrame *inFrame, AVFrame *outFrame)
{
    int rc = OK;
    
    /*if (av_buffersrc_add_frame_flags(buffersrc_ctx, inFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
        log_str("video thumb extractor module: Error while feeding the filtergraph");
        return ERROR;
    }*/
    
    if ((rc = av_buffersink_get_frame(buffersink_ctx, outFrame)) < 0) {
        if (rc != AVERROR(EAGAIN)) {
            log_str("video thumb extractor module: Error while getting the filtergraph result frame");
        }
    }
    
    return rc;
}


int get_frame(AVFormatContext *pFormatCtx, AVCodecContext *pCodecCtx, AVFrame *pFrame, int videoStream, int64_t second)
{
    AVPacket packet;
    int      frameFinished = 0;
    int      rc;
    
    if ((pFormatCtx->duration > 0) && ((((float_t) pFormatCtx->duration / AV_TIME_BASE) - second)) < 0.1) {
        return ERROR;
    }
    
    rc = ERROR;
    // Find the nearest frame
    while (!frameFinished && av_read_frame(pFormatCtx, &packet) >= 0) {
        // Is this a packet from the video stream?
        if (packet.stream_index == videoStream) {
            // Decode video frame
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
            // Did we get a video frame?
            if (frameFinished) {
                rc = OK;
            }
        }
        // Free the packet that was allocated by av_read_frame
        av_packet_unref(&packet);
    }
    
    return rc;
}

float display_aspect_ratio(AVCodecContext *pCodecCtx)
{
    double aspect_ratio = av_q2d(pCodecCtx->sample_aspect_ratio);
    return ((float) pCodecCtx->width / pCodecCtx->height) * (aspect_ratio ? aspect_ratio : 1);
}


int display_width(AVCodecContext *pCodecCtx)
{
    return pCodecCtx->height * display_aspect_ratio(pCodecCtx);
}

int setup_filters(AVFormatContext *pFormatCtx, AVCodecContext *pCodecCtx, int videoStream, AVFilterGraph **fg, AVFilterContext **buffersrc_ctx, AVFilterContext **buffersink_ctx)
{
    AVFilterGraph   *filter_graph;
    
    AVFilterContext *transpose_ctx;
    AVFilterContext *transpose_cw_ctx;
    AVFilterContext *scale_ctx;
    AVFilterContext *crop_ctx;
    AVFilterContext *tile_ctx;
    AVFilterContext *format_ctx;
    
    int              rc = 0;
    char             args[512];
    
    unsigned int     needs_crop = 0;
    float            new_aspect_ratio = 0.0, scale_sws = 0.0, scale_w = 0.0, scale_h = 0.0;
    int              scale_width = 0, scale_height = 0;
    
    unsigned int     rotate90 = 0, rotate180 = 0, rotate270 = 0;
    
    AVDictionaryEntry *rotate = av_dict_get(pFormatCtx->streams[videoStream]->metadata, "rotate", NULL, 0);
    if (rotate) {
        rotate90 = strcasecmp(rotate->value, "90") == 0;
        rotate180 = strcasecmp(rotate->value, "180") == 0;
        rotate270 = strcasecmp(rotate->value, "270") == 0;
    }
    
    float aspect_ratio = display_aspect_ratio(pCodecCtx);
    int width = display_width(pCodecCtx);
    int height = pCodecCtx->height;
    
    if (rotate90 || rotate270) {
        height = width;
        width = pCodecCtx->height;
        aspect_ratio = 1.0 / aspect_ratio;
    }
    
    new_aspect_ratio = (float) width / height;
    
    scale_width = width;
    scale_height = height;
    
    if (aspect_ratio != new_aspect_ratio) {
        scale_w = (float) width / width;
        scale_h = (float) height / height;
        scale_sws = (scale_w > scale_h) ? scale_w : scale_h;
        
        scale_width = width * scale_sws + 0.5;
        scale_height = height * scale_sws + 0.5;
        
        needs_crop = 1;
    }
    
    // create filters to scale and crop the selected frame
    if ((*fg = filter_graph = avfilter_graph_alloc()) == NULL) {
        log_str("video thumb extractor module: unable to create filter graph: out of memory");
        return ERROR;
    }
    
    AVRational time_base = pFormatCtx->streams[videoStream]->time_base;
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
             time_base.num, time_base.den,
             pCodecCtx->sample_aspect_ratio.num, pCodecCtx->sample_aspect_ratio.den);
    
    if (avfilter_graph_create_filter(buffersrc_ctx, avfilter_get_by_name("buffer"), NULL, args, NULL, filter_graph) < 0) {
        log_str("video thumb extractor module: Cannot create buffer source");
        return ERROR;
    }
    
    if ((rotate90 || rotate180 || rotate270) && (avfilter_graph_create_filter(&transpose_ctx, avfilter_get_by_name("transpose"), NULL, rotate270 ? "2" : "1", NULL, filter_graph) < 0)) {
        log_str("video thumb extractor module: error initializing transpose filter");
        return ERROR;
    }
    
    if (rotate180 && (avfilter_graph_create_filter(&transpose_cw_ctx, avfilter_get_by_name("transpose"), NULL, "1", NULL, filter_graph) < 0)) {
        log_str("video thumb extractor module: error initializing transpose filter");
        return ERROR;
    }
    
    snprintf(args, sizeof(args), "%d:%d:flags=bicubic", scale_width, scale_height);
    if (avfilter_graph_create_filter(&scale_ctx, avfilter_get_by_name("scale"), NULL, args, NULL, filter_graph) < 0) {
        log_str("video thumb extractor module: error initializing scale filter");
        return ERROR;
    }
    
    if (needs_crop) {
        snprintf(args, sizeof(args), "%d:%d", (int) width, (int) height);
        if (avfilter_graph_create_filter(&crop_ctx, avfilter_get_by_name("crop"), NULL, args, NULL, filter_graph) < 0) {
            log_str("video thumb extractor module: error initializing crop filter");
            return ERROR;
        }
    }
    
    if (avfilter_graph_create_filter(&tile_ctx, avfilter_get_by_name("tile"), NULL, args, NULL, filter_graph) < 0) {
        log_str("video thumb extractor module: error initializing tile filter");
        return ERROR;
    }
    
    if (avfilter_graph_create_filter(&format_ctx, avfilter_get_by_name("format"), NULL, "pix_fmts=rgb24", NULL, filter_graph) < 0) {
        log_str("video thumb extractor module: error initializing format filter");
        return ERROR;
    }
    
    /* buffer video sink: to terminate the filter chain. */
    if (avfilter_graph_create_filter(buffersink_ctx, avfilter_get_by_name("buffersink"), NULL, NULL, NULL, filter_graph) < 0) {
        log_str("video thumb extractor module: Cannot create buffer sink");
        return ERROR;
    }
    
    // connect inputs and outputs
    if (rotate) {
        rc = avfilter_link(*buffersrc_ctx, 0, transpose_ctx, 0);
        if (rotate180) {
            if (rc >= 0) rc = avfilter_link(transpose_ctx, 0, transpose_cw_ctx, 0);
            if (rc >= 0) rc = avfilter_link(transpose_cw_ctx, 0, scale_ctx, 0);
        } else {
            if (rc >= 0) rc = avfilter_link(transpose_ctx, 0, scale_ctx, 0);
        }
    } else {
        rc = avfilter_link(*buffersrc_ctx, 0, scale_ctx, 0);
    }
    
    if (needs_crop) {
        if (rc >= 0) rc = avfilter_link(scale_ctx, 0, crop_ctx, 0);
        if (rc >= 0) rc = avfilter_link(crop_ctx, 0, tile_ctx, 0);
    } else {
        if (rc >= 0) rc = avfilter_link(scale_ctx, 0, tile_ctx, 0);
    }
    
    if (rc >= 0) rc = avfilter_link(tile_ctx, 0, format_ctx, 0);
    if (rc >= 0) rc = avfilter_link(format_ctx, 0, *buffersink_ctx, 0);
    
    if (rc < 0) {
        log_str("video thumb extractor module: error connecting filters");
        return ERROR;
    }
    
    if (avfilter_graph_config(filter_graph, NULL) < 0) {
        log_str("video thumb extractor module: error configuring the filter graph");
        return ERROR;
    }
    
    return OK;
}

static int
get_thumb(const char* filename, const char* out_name)
{
    int              rc, ret, videoStream;
    AVFormatContext *pFormatCtx = NULL;
    AVCodecContext  *pCodecCtx = NULL;
    AVCodec         *pCodec = NULL;
    AVFrame         *pFrame = NULL;
    size_t           uncompressed_size;
    unsigned char   *bufferAVIO = NULL;
    //AVIOContext     *pAVIOCtx = NULL;
    //AVFilterContext *buffersink_ctx;
    //AVFilterContext *buffersrc_ctx;
    AVFilterGraph   *filter_graph = NULL;
    int              need_flush = 0;
    char             value[10];
    int              threads = 2;
    int              second = 0;
    AVCodecContext  *pOCodecCtx = NULL;
    AVCodec         *pOCodec = NULL;
    AVPacket        *packet = NULL;
    AVFrame         *pFrameRGB = NULL;
    struct SwsContext *scalerCtx = NULL;
    AVDictionary    *input_options = NULL;
    AVProbeData      pd = { filename, NULL, 0 };
    AVInputFormat   *input_format = NULL;
    
    rc = ERROR;
    
    bufferAVIO = (unsigned char *)malloc(BUFFER_SIZE);
    if (!bufferAVIO)
    {
        log_str("video thumb extractor module: Couldn't alloc AVIO buffer\n");
        goto exit;
    }
    
    pFormatCtx = avformat_alloc_context();
    if (!pFormatCtx)
    {
        log_str("video thumb extractor module: Couldn't alloc AVIO buffer\n");
        goto exit;
    }
    
    //pFormatCtx->pb = pAVIOCtx;
    pFormatCtx->flags |= AVFMT_FLAG_NONBLOCK;
    
    input_format = av_probe_input_format(&pd, 0);
    
    if (!input_format)
    {
        log_str("Failed to get input format\n");
        goto exit;
    }
    
    av_dict_set(&input_options, "rtmp_live", "live", 0);
    
    // Open video file
    if ((ret = avformat_open_input(&pFormatCtx, filename, input_format, &input_options)) != 0)
    {
        log_str("video thumb extractor module: Couldn't open file %s, error: %d\n", filename, ret);
        goto exit;
    }
    
    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
    {
        log_str("video thumb extractor module: Couldn't find stream information\n");
        goto exit;
    }
    
    if ((pFormatCtx->duration > 0) && ((((float_t) pFormatCtx->duration / AV_TIME_BASE))) < 0.1) {
        log_str("video thumb extractor module: seconds greater than duration\n");
        rc = ERROR;
        goto exit;
    }
    
    // Find the first video stream
    videoStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0);
    if (videoStream == -1) {
        log_str("video thumb extractor module: Didn't find a video stream\n");
        goto exit;
    }
    
    // Get a pointer to the codec context for the video stream
    pCodecCtx = pFormatCtx->streams[videoStream]->codec;
    
    AVDictionary *dict = NULL;
    sprintf(value, "%d", threads);
    av_dict_set(&dict, "threads", value, 0);
    
    // Open codec
    if ((avcodec_open2(pCodecCtx, pCodec, &dict)) < 0) {
        log_str("video thumb extractor module: Could not open codec\n");
        goto exit;
    }
    
    //setup_parameters(cf, ctx, pFormatCtx, pCodecCtx);
    
    /*if (setup_filters(pFormatCtx, pCodecCtx, videoStream, &filter_graph, &buffersrc_ctx, &buffersink_ctx) < 0) {
        goto exit;
    }*/
    
    // Allocate video frame
    pFrame = av_frame_alloc();
    
    if (pFrame == NULL) {
        log_str("video thumb extractor module: Could not alloc frame memory\n");
        goto exit;
    }
    
    while ((rc = get_frame(pFormatCtx, pCodecCtx, pFrame, videoStream, second)) == 0) {
        
        if (pFrame->pict_type == 0) { // AV_PICTURE_TYPE_NONE
            need_flush = 1;
            break;
        }
        
        /*if (filter_frame(buffersrc_ctx, buffersink_ctx, pFrame, pFrame) == AVERROR(EAGAIN)) {
            need_flush = 1;
            continue;
        }*/
        
        need_flush = 0;
        break;
    }
    
    if (pFrame == NULL || rc != OK) {
        log_str("Failed to get frame\n");
        goto exit;
    }
    
    /*if (need_flush) {
        if (filter_frame(buffersrc_ctx, buffersink_ctx, NULL, pFrame) < 0) {
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
    
    uncompressed_size = pFrame->width * pFrame->height * 3;
    
    //char *newBuffer = malloc(uncompressed_size);
    //av_image_copy_to_buffer(newBuffer, uncompressed_size, pFrame->data, pFrame->linesize, AV_PIX_FMT_RGB24, pFrame->width, pFrame->height);
    
    log_str("Pixel format: %d, %d", pCodecCtx->pix_fmt, AV_PIX_FMT_YUV420P);
    log_str("Colorspace: %d", pFrame->colorspace);
    
    scalerCtx = sws_getContext(pCodecCtx->width,
                               pCodecCtx->height,
                               pCodecCtx->pix_fmt,
                               pCodecCtx->width,
                               pCodecCtx->height,
                               AV_PIX_FMT_RGB24,
                               SWS_BILINEAR, //SWS_BICUBIC
                               NULL, NULL, NULL);
    if (!scalerCtx) {
        printf("sws_getContext() failed\n");
        goto exit;
    }
    
    pFrameRGB = av_frame_alloc();
    
    if (pFrameRGB == NULL)
    {
        printf("Failed to alloc frame\n");
        goto exit;
    }
    
    avpicture_alloc((AVPicture *)pFrameRGB, AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);
    
    sws_scale(scalerCtx, pFrame->data, pFrame->linesize, 0, pFrame->height, pFrameRGB->data, pFrameRGB->linesize);
    
    pOCodec = avcodec_find_encoder (AV_CODEC_ID_PNG);
    
    if (!pOCodec)
    {
        log_str("Could not find png encode");
        goto exit;
    }
    
    pOCodecCtx = avcodec_alloc_context3(pOCodec);
    
    if (!pOCodecCtx) {
        log_str("Failed to create codec context");
        goto exit;
    }
    
    pOCodecCtx->bit_rate      = pCodecCtx->bit_rate;
    pOCodecCtx->width         = pCodecCtx->width;
    pOCodecCtx->height        = pCodecCtx->height;
    //pOCodecCtx->pix_fmt       = pCodecCtx->pix_fmt;
    pOCodecCtx->pix_fmt       = AV_PIX_FMT_RGB24;
    //pOCodecCtx->codec_id      = AV_CODEC_ID_PNG;
    //pOCodecCtx->codec_type    = AVMEDIA_TYPE_VIDEO;
    pOCodecCtx->time_base.num = pCodecCtx->time_base.num;
    pOCodecCtx->time_base.den = pCodecCtx->time_base.den;
    
    packet = av_packet_alloc();
    
    int got_packet;
    
    avcodec_encode_video2(pOCodecCtx, packet, pFrameRGB, &got_packet);
    
    if (!got_packet)
    {
        log_str("Didn't get packet");
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
    
    if (pOCodecCtx)
    {
        avcodec_free_context(&pOCodecCtx);
    }
    
    if (pFrameRGB) av_frame_free(&pFrameRGB);
    
    // Free the YUV frame
    if (pFrame) av_frame_free(&pFrame);
    
    if (scalerCtx) sws_freeContext(scalerCtx);
    
    // Close the codec
    if (pCodecCtx) avcodec_close(pCodecCtx);
    
    // Close the video file
    if (pFormatCtx) avformat_close_input(&pFormatCtx);
    
    if (filter_graph) avfilter_graph_free(&filter_graph);
    
    return rc;
}

void init()
{
    // Register all formats and codecs
    av_register_all();
    avfilter_register_all();
    av_log_set_level(AV_LOG_ERROR);
}

int main(int argc, const char * argv[])
{
    if (argc < 3)
    {
        log_str("Too few arguments");
        return 1;
    }
    
    init();
    
    log_str("Path: %s", argv[1]);
    
    if (get_thumb(argv[1], argv[2]) != OK)
    {
        return 1;
    }
    
    return 0;
}
