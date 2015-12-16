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
#include <png.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define OK 0
#define ERROR -1

#define BUFFER_SIZE 1024 * 8
#define MEMORY_STEP 1024

typedef struct {
    int                              fd;
    int64_t                          offset;
} file_t;

typedef struct {
    int64_t                          size;
    int64_t                          offset;
    file_t                           file;
} file_info;

void log_str(const char * format, ... )
{
    va_list vl;
    va_start(vl, format);
    
    vprintf(format, vl);
    printf("\n");
    
    va_end(vl);
}

static uint32_t
png_compress(uint8_t *buffer, int linesize, int width, int height, caddr_t *out_buffer, size_t *out_len, size_t uncompressed_size)
{
    int code = 0;
    FILE *fp = NULL;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_bytep row = NULL;
    
    const char *filename = "/Users/elviss/Desktop/test.png";
    char *title = NULL;
    
    fp = fopen(filename, "wb");
    if (fp == NULL) {
        log_str("Could not open file %s for writing", filename);
        code = 1;
        goto finalise;
    }
    
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL) {
        log_str("Could not allocate write struct");
        code = 1;
        goto finalise;
    }
    
    // Initialize info structure
    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        log_str("Could not allocate info struct");
        code = 1;
        goto finalise;
    }
    
    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "Error during png creation\n");
        code = 1;
        goto finalise;
    }
    
    png_init_io(png_ptr, fp);
    
    // Write header (8 bit colour depth)
    png_set_IHDR(png_ptr, info_ptr, width, height,
                 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    
    // Set title
    if (title != NULL) {
        png_text title_text;
        title_text.compression = PNG_TEXT_COMPRESSION_NONE;
        title_text.key = "Title";
        title_text.text = title;
        png_set_text(png_ptr, info_ptr, &title_text, 1);
    }
    
    png_write_info(png_ptr, info_ptr);
    
    // Allocate memory for one row (3 bytes per pixel - RGB)
    row = (png_bytep) malloc(3 * width * sizeof(png_byte));
    
    // Write image data
    
    log_str("linesize: %d, width: %d, height: %d, uncompressed: %d", linesize, width, height, uncompressed_size);
    
    FILE* raw = fopen("/Users/elviss/Desktop/raw.txt", "wb");
    fwrite(buffer, uncompressed_size, 1, raw);
    fclose(raw);
    
    int x, y;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            /*int p = x * 3 + y * frame->linesize[0];
            char r = buffer[p];
            char g = buffer[p+1];
            char b = buffer[p+2];*/
            
            row[x * 3] = buffer[y * linesize + x];
            row[x * 3 + 1] = buffer[y * linesize + x + 1];
            row[x * 3 + 2] = buffer[y * linesize + x + 2];
        }
        
        png_write_row(png_ptr, row);
    }
    
    // End write
    png_write_end(png_ptr, NULL);
    
    png_write_info(png_ptr, info_ptr);
    
finalise:
    if (fp != NULL) fclose(fp);
    if (info_ptr != NULL) png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
    if (png_ptr != NULL) png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    if (row != NULL) free(row);
    
    return code;
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
    
    int64_t second_on_stream_time_base = second * pFormatCtx->streams[videoStream]->time_base.den / pFormatCtx->streams[videoStream]->time_base.num;
    
    if ((pFormatCtx->duration > 0) && ((((float_t) pFormatCtx->duration / AV_TIME_BASE) - second)) < 0.1) {
        return ERROR;
    }
    
    // seek first frame
    if (av_seek_frame(pFormatCtx, videoStream, second_on_stream_time_base, 0) < 0) {
        log_str("video thumb extractor module: Seek to an invalid time");
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
    file_info *info = (file_info *) opaque;
    if (whence == AVSEEK_SIZE) {
        return info->size;
    }
    
    if ((whence == SEEK_SET) || (whence == SEEK_CUR) || (whence == SEEK_END)) {
        info->file.offset = lseek(info->file.fd, info->offset + offset, whence);
        return info->file.offset < 0 ? -1 : 0;
    }
    
    return -1;
}


int read_data_from_file(void *opaque, uint8_t *buf, int buf_len)
{
    file_info *info = (file_info *) opaque;
    
    if ((info->offset > 0) && (info->file.offset < info->offset)) {
        info->file.offset = lseek(info->file.fd, info->offset, SEEK_SET);
        if (info->file.offset < 0) {
            return AVERROR(errno);
        }
    }
    
    ssize_t r = pread(info->file.fd, buf, buf_len, info->file.offset);
    return (r == ERROR) ? AVERROR(errno) : (int)r;
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
get_thumb(const char* filename, caddr_t *out_buffer, size_t *out_len)
{
    int              rc, ret, videoStream;
    AVFormatContext *pFormatCtx = NULL;
    AVCodecContext  *pCodecCtx = NULL;
    AVCodec         *pCodec = NULL;
    AVFrame         *pFrame = NULL;
    size_t           uncompressed_size;
    unsigned char   *bufferAVIO = NULL;
    //AVIOContext     *pAVIOCtx = NULL;
    AVFilterContext *buffersink_ctx;
    AVFilterContext *buffersrc_ctx;
    AVFilterGraph   *filter_graph = NULL;
    int              need_flush = 0;
    char             value[10];
    int              threads = 2;
    file_info       *info;
    int              second = 0;
    
    rc = OK;
    
    info = malloc(sizeof(file_info));
    memset(info, 0, sizeof(file_info));
    
    // Open video file
    info->file.fd = open(filename, O_RDONLY);
    if (info->file.fd == -1) {
        log_str("Failed to open file \"%s\"\n", filename);
        goto exit;
    }
    
    struct stat s;
    fstat(info->file.fd, &s);
    info->size = s.st_size;
    
    bufferAVIO = (unsigned char *)malloc(BUFFER_SIZE);
    if (!bufferAVIO) {
        log_str("video thumb extractor module: Couldn't alloc AVIO buffer\n");
        goto exit;
    }
    
    pFormatCtx = avformat_alloc_context();
    if (!pFormatCtx) {
        log_str("video thumb extractor module: Couldn't alloc AVIO buffer\n");
        goto exit;
    }
    
    /*pAVIOCtx = avio_alloc_context(bufferAVIO, BUFFER_SIZE, 0, &info, read_data_from_file, NULL, seek_data_from_file);
    if (pAVIOCtx == NULL) {
        log_str("video thumb extractor module: Couldn't alloc AVIO context\n");
        goto exit;
    }*/
    
    //pFormatCtx->pb = pAVIOCtx;
    pFormatCtx->flags |= AVFMT_FLAG_NONBLOCK;
    
    // Open video file
    if ((ret = avformat_open_input(&pFormatCtx, filename, NULL, NULL)) != 0) {
        log_str("video thumb extractor module: Couldn't open file %s, error: %d\n", filename, ret);
        rc = ERROR;
        goto exit;
    }
    
    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
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
    
    /*if (need_flush) {
        if (filter_frame(buffersrc_ctx, buffersink_ctx, NULL, pFrame) < 0) {
            goto exit;
        }
        
        rc = OK;
    }*/
    
    
    if (rc == OK) {
        
        /*AVFrame* newFrame = av_frame_alloc();
        int numBytes = avpicture_get_size(AV_PIX_FMT_RGB24, pFrame->width, pFrame->height);
        uint8_t *buffer= malloc(numBytes);
        
        avpicture_fill((AVPicture *)newFrame, buffer, AV_PIX_FMT_RGB24, pFrame->width, pFrame->height);
        
        img_convert((AVPicture *)newFrame, AV_PIX_FMT_RGB24,
                    (AVPicture*)pFrame, pCodecCtx->pix_fmt, pCodecCtx->width,
                    pCodecCtx->height);*/
        
        uncompressed_size = pFrame->width * pFrame->height * 3;
        
        log_str("Colorspace: %d", pFrame->colorspace);
        
        //avpicture_layout();
        
        if (png_compress(pFrame->data[0], pFrame->linesize[0], pFrame->width, pFrame->height, out_buffer, out_len, uncompressed_size) == 0) {
            rc = OK;
        }
    }
    
exit:
    
    if ((info->file.fd == -1) && (close(info->file.fd) != 0)) {
        log_str("video thumb extractor module: Couldn't close file %s", filename);
        rc = ERROR;
    }
    
    free(info);
    
    /* destroy unneeded objects */
    
    // Free the YUV frame
    if (pFrame != NULL) av_frame_free(&pFrame);
    
    // Close the codec
    if (pCodecCtx != NULL) avcodec_close(pCodecCtx);
    
    // Close the video file
    if (pFormatCtx != NULL) avformat_close_input(&pFormatCtx);
    
    // Free AVIO context
    /*if (pAVIOCtx != NULL) {
        if (pAVIOCtx->buffer != NULL) av_freep(&pAVIOCtx->buffer);
        av_freep(&pAVIOCtx);
    }*/
    
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
        log_str("Too few arguments\n");
        return 1;
    }
    
    init();
    
    log_str("Path: %s", argv[1]);
    
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
