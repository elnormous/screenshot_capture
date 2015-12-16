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

#include <jpeglib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define OK 0
#define ERROR -1

#define BUFFER_SIZE 1024 * 8
#define MEMORY_STEP 1024

struct file_info
{
    FILE* file;
    off_t size;
};

static uint32_t
jpeg_compress(uint8_t * buffer, int linesize, int out_width, int out_height, caddr_t *out_buffer, size_t *out_len, size_t uncompressed_size)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPROW row_pointer[1];
    unsigned char *outbuf;
    unsigned long outsize;
    
    if ( !buffer ) return 1;
    
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    //ngx_http_video_thumbextractor_jpeg_memory_dest(&cinfo, out_buffer, out_len, uncompressed_size);
    
    size_t sz = 1024*1024;
    outbuf = malloc(sz);
    outsize = sz;
    jpeg_mem_dest(&cinfo, &outbuf, &outsize);
    
    cinfo.image_width = out_width;
    cinfo.image_height = out_height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    
    jpeg_set_defaults(&cinfo);
    /* Important: Header info must be set AFTER jpeg_set_defaults() */
    cinfo.write_JFIF_header = TRUE;
    cinfo.JFIF_major_version = 1;
    cinfo.JFIF_minor_version = 2;
    cinfo.density_unit = 1; /* 0=unknown, 1=dpi, 2=dpcm */
    cinfo.X_density = 72;
    cinfo.Y_density = 72;
    cinfo.write_Adobe_marker = TRUE;
    
    jpeg_set_quality(&cinfo, 75, 1);
    cinfo.optimize_coding = 100;
    cinfo.smoothing_factor = 0;
    
    //if ( jpeg_progressive_mode ) {
        jpeg_simple_progression(&cinfo);
    //}
    
    jpeg_start_compress(&cinfo, TRUE);
    
    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = &buffer[cinfo.next_scanline * linesize];
        (void)jpeg_write_scanlines(&cinfo, row_pointer,1);
    }
    
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    
    return 0;
}

int filter_frame(AVFilterContext *buffersrc_ctx, AVFilterContext *buffersink_ctx, AVFrame *inFrame, AVFrame *outFrame)
{
    int rc = OK;
    
    /*if (av_buffersrc_add_frame_flags(buffersrc_ctx, inFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
        printf("video thumb extractor module: Error while feeding the filtergraph");
        return ERROR;
    }*/
    
    if ((rc = av_buffersink_get_frame(buffersink_ctx, outFrame)) < 0) {
        if (rc != AVERROR(EAGAIN)) {
            printf("video thumb extractor module: Error while getting the filtergraph result frame");
        }
    }
    
    return rc;
}


int get_frame(AVFormatContext *pFormatCtx, AVCodecContext *pCodecCtx, AVFrame *pFrame, int videoStream, int64_t second)
{
    AVPacket packet;
    int      frameFinished = 0;
    int      rc;
    
    int64_t second_on_stream_time_base = second * pFormatCtx->streams[videoStream]->time_base.den / pFormatCtx->streams[videoStream]->time_base.num;
    
    if ((pFormatCtx->duration > 0) && ((((float_t) pFormatCtx->duration / AV_TIME_BASE) - second)) < 0.1) {
        return ERROR;
    }
    
    // seek first frame
    if (av_seek_frame(pFormatCtx, videoStream, second_on_stream_time_base, 0) < 0) {
        printf("video thumb extractor module: Seek to an invalid time");
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
        av_free_packet(&packet);
    }
    av_free_packet(&packet);
    
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

int64_t seek_data_from_file(void *opaque, int64_t offset, int whence)
{
    struct file_info *info = (struct file_info *) opaque;
    if (whence == AVSEEK_SIZE) {
        return info->size;
    }
    
    if ((whence == SEEK_SET) || (whence == SEEK_CUR) || (whence == SEEK_END)) {
        int result = fseek(info->file, offset, whence);
        return result < 0 ? -1 : 0;
    }
    return -1;
}


int read_data_from_file(void *opaque, uint8_t *buf, int buf_len)
{
    struct file_info *info = (struct file_info *) opaque;
    
    ssize_t r = fread(buf, buf_len, 1, info->file);
    return (r == -1) ? AVERROR(ERROR) : (int)r;
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
        printf("video thumb extractor module: unable to create filter graph: out of memory");
        return ERROR;
    }
    
    AVRational time_base = pFormatCtx->streams[videoStream]->time_base;
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
             time_base.num, time_base.den,
             pCodecCtx->sample_aspect_ratio.num, pCodecCtx->sample_aspect_ratio.den);
    
    if (avfilter_graph_create_filter(buffersrc_ctx, avfilter_get_by_name("buffer"), NULL, args, NULL, filter_graph) < 0) {
        printf("video thumb extractor module: Cannot create buffer source");
        return ERROR;
    }
    
    if ((rotate90 || rotate180 || rotate270) && (avfilter_graph_create_filter(&transpose_ctx, avfilter_get_by_name("transpose"), NULL, rotate270 ? "2" : "1", NULL, filter_graph) < 0)) {
        printf("video thumb extractor module: error initializing transpose filter");
        return ERROR;
    }
    
    if (rotate180 && (avfilter_graph_create_filter(&transpose_cw_ctx, avfilter_get_by_name("transpose"), NULL, "1", NULL, filter_graph) < 0)) {
        printf("video thumb extractor module: error initializing transpose filter");
        return ERROR;
    }
    
    snprintf(args, sizeof(args), "%d:%d:flags=bicubic", scale_width, scale_height);
    if (avfilter_graph_create_filter(&scale_ctx, avfilter_get_by_name("scale"), NULL, args, NULL, filter_graph) < 0) {
        printf("video thumb extractor module: error initializing scale filter");
        return ERROR;
    }
    
    if (needs_crop) {
        snprintf(args, sizeof(args), "%d:%d", (int) width, (int) height);
        if (avfilter_graph_create_filter(&crop_ctx, avfilter_get_by_name("crop"), NULL, args, NULL, filter_graph) < 0) {
            printf("video thumb extractor module: error initializing crop filter");
            return ERROR;
        }
    }
    
    if (avfilter_graph_create_filter(&tile_ctx, avfilter_get_by_name("tile"), NULL, args, NULL, filter_graph) < 0) {
        printf("video thumb extractor module: error initializing tile filter");
        return ERROR;
    }
    
    if (avfilter_graph_create_filter(&format_ctx, avfilter_get_by_name("format"), NULL, "pix_fmts=rgb24", NULL, filter_graph) < 0) {
        printf("video thumb extractor module: error initializing format filter");
        return ERROR;
    }
    
    /* buffer video sink: to terminate the filter chain. */
    if (avfilter_graph_create_filter(buffersink_ctx, avfilter_get_by_name("buffersink"), NULL, NULL, NULL, filter_graph) < 0) {
        printf("video thumb extractor module: Cannot create buffer sink");
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
        printf("video thumb extractor module: error connecting filters");
        return ERROR;
    }
    
    if (avfilter_graph_config(filter_graph, NULL) < 0) {
        printf("video thumb extractor module: error configuring the filter graph");
        return ERROR;
    }
    
    return OK;
}

static int
get_thumb(const char* filename, caddr_t *out_buffer, size_t *out_len)
{
    int              rc, ret, videoStream;
    AVFormatContext *pFormatCtx = NULL;
    AVCodecContext  *pCodecCtx = NULL;
    AVCodec         *pCodec = NULL;
    AVFrame         *pFrame = NULL;
    size_t           uncompressed_size;
    unsigned char   *bufferAVIO = NULL;
    AVIOContext     *pAVIOCtx = NULL;
    AVFilterContext *buffersink_ctx;
    AVFilterContext *buffersrc_ctx;
    AVFilterGraph   *filter_graph = NULL;
    int              need_flush = 0;
    char             value[10];
    int              threads = 2;
    struct file_info info;
    int              second = 0;
    
    // Open video file
    info.file = fopen(filename, "r");
    if (info.file == 0) {
        printf("Failed to open file \"%s\"\n", filename);
        goto exit;
    }
    
    struct stat s;
    fstat(fileno(info.file), &s);
    
    info.size = s.st_size;
    
    pFormatCtx = avformat_alloc_context();
    bufferAVIO = (unsigned char *)malloc(BUFFER_SIZE);
    if ((pFormatCtx == NULL) || (bufferAVIO == NULL)) {
        printf("video thumb extractor module: Couldn't alloc AVIO buffer\n");
        goto exit;
    }
    
    pAVIOCtx = avio_alloc_context(bufferAVIO, BUFFER_SIZE, 0, &info, read_data_from_file, NULL, seek_data_from_file);
    //pAVIOCtx = avio_alloc_context(bufferAVIO, BUFFER_SIZE, 0, &info, NULL, NULL, NULL);
    if (pAVIOCtx == NULL) {
        printf("video thumb extractor module: Couldn't alloc AVIO context\n");
        goto exit;
    }
    
    pFormatCtx->pb = pAVIOCtx;
    
    // Open video file
    if ((ret = avformat_open_input(&pFormatCtx, filename, NULL, NULL)) != 0) {
        printf("video thumb extractor module: Couldn't open file %s, error: %d\n", filename, ret);
        rc = ERROR;
        goto exit;
    }
    
    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        printf("video thumb extractor module: Couldn't find stream information\n");
        goto exit;
    }
    
    if ((pFormatCtx->duration > 0) && ((((float_t) pFormatCtx->duration / AV_TIME_BASE))) < 0.1) {
        printf("video thumb extractor module: seconds greater than duration\n");
        rc = ERROR;
        goto exit;
    }
    
    // Find the first video stream
    videoStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0);
    if (videoStream == -1) {
        printf("video thumb extractor module: Didn't find a video stream\n");
        goto exit;
    }
    
    // Get a pointer to the codec context for the video stream
    pCodecCtx = pFormatCtx->streams[videoStream]->codec;
    
    AVDictionary *dict = NULL;
    sprintf(value, "%d", threads);
    av_dict_set(&dict, "threads", value, 0);
    
    // Open codec
    if ((avcodec_open2(pCodecCtx, pCodec, &dict)) < 0) {
        printf("video thumb extractor module: Could not open codec\n");
        goto exit;
    }
    
    //setup_parameters(cf, ctx, pFormatCtx, pCodecCtx);
    
    if (setup_filters(pFormatCtx, pCodecCtx, videoStream, &filter_graph, &buffersrc_ctx, &buffersink_ctx) < 0) {
        goto exit;
    }
    
    // Allocate video frame
    pFrame = av_frame_alloc();
    
    if (pFrame == NULL) {
        printf("video thumb extractor module: Could not alloc frame memory\n");
        goto exit;
    }
    
    while ((rc = get_frame(pFormatCtx, pCodecCtx, pFrame, videoStream, second)) == 0) {
        if (pFrame->pict_type == 0) { // AV_PICTURE_TYPE_NONE
            need_flush = 1;
            break;
        }
        
        if (filter_frame(buffersrc_ctx, buffersink_ctx, pFrame, pFrame) == AVERROR(EAGAIN)) {
            need_flush = 1;
            continue;
        }
        
        need_flush = 0;
        break;
    }
    
    if (need_flush) {
        if (filter_frame(buffersrc_ctx, buffersink_ctx, NULL, pFrame) < 0) {
            goto exit;
        }
        
        rc = OK;
    }
    
    
    if (rc == OK) {
        // Convert the image from its native format to JPEG
        uncompressed_size = pFrame->width * pFrame->height * 3;
        if (jpeg_compress(pFrame->data[0], pFrame->linesize[0], pFrame->width, pFrame->height, out_buffer, out_len, uncompressed_size) == 0) {
            rc = OK;
        }
    }
    
exit:
    
    if ((info.file == NULL) && (fclose(info.file) != 0)) {
        printf("video thumb extractor module: Couldn't close file %s", filename);
        rc = ERROR;
    }
    
    /* destroy unneeded objects */
    
    // Free the YUV frame
    if (pFrame != NULL) av_frame_free(&pFrame);
    
    // Close the codec
    if (pCodecCtx != NULL) avcodec_close(pCodecCtx);
    
    // Close the video file
    if (pFormatCtx != NULL) avformat_close_input(&pFormatCtx);
    
    // Free AVIO context
    if (pAVIOCtx != NULL) {
        if (pAVIOCtx->buffer != NULL) av_freep(&pAVIOCtx->buffer);
        av_freep(&pAVIOCtx);
    }
    
    if (filter_graph != NULL) avfilter_graph_free(&filter_graph);
    
    return rc;
}

void init()
{
    // Register all formats and codecs
    av_register_all();
    avfilter_register_all();
    av_log_set_level(AV_LOG_ERROR);
}

void deinit()
{
    
}

int main(int argc, const char * argv[])
{
    if (argc < 2)
    {
        printf("Too few arguments\n");
        return 1;
    }
    
    init();
    
    printf("Path: %s\n", argv[1]);
    
    caddr_t* buffer = malloc(1024 * 1024 * 100);
    size_t len;
    
    if (get_thumb(argv[1], buffer, &len) == OK)
    {
        FILE* f = fopen("/Users/elviss/Desktop/test.jpg", "wb");
        fwrite(buffer, len, 1, f);
        fclose(f);
    }
    
    deinit();
    
    return 0;
}
