//
//  ffmpeg.c
//  Example
//
//  Created by Andrea on 10/05/2017.
//  Copyright © 2017 Andrea. All rights reserved.
//

#include "ffmpeg.h"

struct buffer_data {
    uint8_t *ptr;
    size_t size; ///< size left in the buffer
};
static int read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    struct buffer_data *bd = (struct buffer_data *)opaque;
    buf_size = FFMIN(buf_size, bd->size);
    printf("ptr:%p size:%zu\n", bd->ptr, bd->size);
    /* copy internal buffer data to buf */
    memcpy(buf, bd->ptr, buf_size);
    bd->ptr  += buf_size;
    bd->size -= buf_size;
    return buf_size;
}
int avio_reading(const char *input_filename)
{
    AVFormatContext *fmt_ctx = NULL;
    AVIOContext *avio_ctx = NULL;
    uint8_t *buffer = NULL, *avio_ctx_buffer = NULL;
    size_t buffer_size, avio_ctx_buffer_size = 4096;
    int ret = 0;
    struct buffer_data bd = { 0 };

    /* register codecs and formats and other lavf/lavc components*/
    av_register_all();
    /* slurp file content into buffer */
    ret = av_file_map(input_filename, &buffer, &buffer_size, 0, NULL);
    if (ret < 0)
        goto end;
    /* fill opaque structure used by the AVIOContext read callback */
    bd.ptr  = buffer;
    bd.size = buffer_size;
    if (!(fmt_ctx = avformat_alloc_context())) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    avio_ctx_buffer = av_malloc(avio_ctx_buffer_size);
    if (!avio_ctx_buffer) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    avio_ctx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size,
                                  0, &bd, &read_packet, NULL, NULL);
    if (!avio_ctx) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    fmt_ctx->pb = avio_ctx;
    ret = avformat_open_input(&fmt_ctx, NULL, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open input\n");
        goto end;
    }
    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not find stream information\n");
        goto end;
    }
    av_dump_format(fmt_ctx, 0, input_filename, 0);
end:
    avformat_close_input(&fmt_ctx);
    /* note: the internal buffer could have changed, and be != avio_ctx_buffer */
    if (avio_ctx) {
        av_freep(&avio_ctx->buffer);
        av_freep(&avio_ctx);
    }
    av_file_unmap(buffer, buffer_size);
    if (ret < 0) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }
    return 0;
}


void set_output_sample_fmt(AudioInfo* in, AudioInfo* out)
{
    if (out->audioCodec && out->audioCodec->sample_fmts) {
        
        // check if the output encoder supports the input sample format
        const enum AVSampleFormat *p = out->audioCodec->sample_fmts;
        for (; *p != -1; p++) {
            if (*p == in->audioCodecCtx->sample_fmt) {
                out->audioCodecCtx->sample_fmt = *p;
                break;
            }
        }
        
        if (*p == -1) {
            // if not, we need to convert sample formats and select the first format that is supported by the output encoder
            out->audioCodecCtx->sample_fmt = out->audioCodec->sample_fmts[0];
        }
    }
}

static AVStream* add_stream(AVFormatContext *oc, AVCodec **codec, enum AVCodecID codec_id, ConvertSettings* settings)
{
    AVCodecContext *c;
    AVStream *st;
    
    *codec = avcodec_find_encoder(codec_id);
    
    if (!(*codec)) {
        fprintf(stderr, "Could not find encoder for '%s'\n", avcodec_get_name(codec_id));
        return NULL;
    }
    
    st = avformat_new_stream(oc, *codec);
    if (!st) {
        fprintf(stderr, "Could not allocate stream\n");
        return NULL;
    }
    
    st->id = 1;
    c = st->codec;
    c->bit_rate = settings->rs_bit_rate;
    c->sample_rate = settings->rs_sample_rate;
    c->channels = settings->rs_channels;
    c->channel_layout = settings->rs_channels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
    
    return st;
}


static int open_audio(AudioInfo* in)
{
    int ret = 0;
    
    ret = avcodec_open2(in->audioCodecCtx, in->audioCodec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open audio codec: %s\n", in->audioCodec->name);
    }
    return ret;
}

static int write_audio_frame(AVFormatContext *oc, AVStream* st, AVFrame *frame)
{
    AVCodecContext *codec = st->codec;
    AVPacket pkt;
    int got_packet = 0;
    int ret = 0;
    
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;
    
    ret = avcodec_encode_audio2(codec, &pkt, frame, &got_packet);
    if (ret < 0) {
        fprintf(stderr, "Error encoding audio frame. \n");
        return ret;
    }
    
    if (got_packet)
    {
        if (pkt.pts != AV_NOPTS_VALUE)
            pkt.pts      = av_rescale_q(pkt.pts,      codec->time_base, st->time_base);
        if (pkt.dts != AV_NOPTS_VALUE)
            pkt.dts      = av_rescale_q(pkt.dts,      codec->time_base, st->time_base);
        if (pkt.duration > 0)
            pkt.duration = (int) av_rescale_q(pkt.duration, codec->time_base, st->time_base);
        
        /* Write the compressed frame to the media file. */
        ret = av_interleaved_write_frame(oc, &pkt);
        if (ret != 0) {
            fprintf(stderr, "Error while writing audio frame: %s\n");
            return ret; 
        }
        
        av_free_packet(&pkt);
    }
    
    return 0; 
}


static void flush_queue(AVFormatContext *oc, AVCodecContext *codec)
{
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;
    int got_packet = 0;
    int ret = 0;
    
    for(got_packet = 1;got_packet;) {
        ret = avcodec_encode_audio2(codec, &pkt, NULL, &got_packet);
        
        if (ret < 0 || !got_packet)
            return;
        
        av_interleaved_write_frame (oc, &pkt);
        av_free_packet(&pkt);
    }
    
    ret = av_interleaved_write_frame (oc, NULL);
}

static int32_t handle_progress(int64_t totalSize, int64_t processed, int32_t old_percent_done, NotifyProgressCallback callback)
{
    int32_t percentDone = (processed >= totalSize) ? 100 : (int32_t) ((processed * 100.0) / totalSize) & INT32_MAX;
    
    if(percentDone > old_percent_done)
    {
        printf("Converting... %d %%   \r", percentDone);
        
        if(callback) {
            callback(percentDone); 
        }
    }
    
    return percentDone; 
}

static int init_filter(FilterContext* filterCtx, AudioInfo* in, AudioInfo* out)
{
    filterCtx->buffersink_ctx = NULL;
    filterCtx->buffersrc_ctx = NULL;
    filterCtx->filter_graph = NULL;
    
    char args[512];
    int ret;
    AVFilter *abuffersrc  = avfilter_get_by_name("abuffer");
    AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    
    const enum AVSampleFormat out_sample_fmts[] = { out->audioCodecCtx->sample_fmt, AV_SAMPLE_FMT_NONE };
    
    const int64_t out_channel_layouts[] = { out->audioCodecCtx->channel_layout, -1 };
    const int out_sample_rates[] = { out->audioCodecCtx->sample_rate, -1 };
    const AVFilterLink *inlink;
    const AVFilterLink *outlink;
    AVRational time_base = in->formatCtx->streams[in->audioStreamIndex]->time_base;
    filterCtx->filter_graph = avfilter_graph_alloc();
    
    if (!in->audioCodecCtx->channel_layout) {
        in->audioCodecCtx->channel_layout = av_get_default_channel_layout(in->audioCodecCtx->channels);
    }
    
    fprintf(args, sizeof(args),
                "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
                time_base.num, time_base.den, in->audioCodecCtx->sample_rate,
                av_get_sample_fmt_name(in->audioCodecCtx->sample_fmt), in->audioCodecCtx->channel_layout);
    
    ret = avfilter_graph_create_filter(&filterCtx->buffersrc_ctx, abuffersrc, "in", args, NULL, filterCtx->filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
        return ret;
    }
    
    // set up buffer audio sink
    ret = avfilter_graph_create_filter(&filterCtx->buffersink_ctx, abuffersink, "out", NULL, NULL, filterCtx->filter_graph);
    ret = av_opt_set_int_list(filterCtx->buffersink_ctx, "sample_fmts", out_sample_fmts, -1, AV_OPT_SEARCH_CHILDREN);
    ret = av_opt_set_int_list(filterCtx->buffersink_ctx, "channel_layouts", out_channel_layouts, -1, AV_OPT_SEARCH_CHILDREN);
    ret = av_opt_set_int_list(filterCtx->buffersink_ctx, "sample_rates", out_sample_rates, -1, AV_OPT_SEARCH_CHILDREN);
    
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error setting up buffer sink.\n");
        return ret;
    }
    
    // Endpoints for the filter graph
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = filterCtx->buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = filterCtx->buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;
    
    fprintf(args, sizeof(args),
                "aresample=%d,aformat=sample_fmts=%s:channel_layouts=stereo",
                out->audioCodecCtx->sample_rate,
                av_get_sample_fmt_name(out->audioCodecCtx->sample_fmt));
    
    if ((ret = avfilter_graph_parse(filterCtx->filter_graph, args, &inputs, &outputs, NULL)) < 0)
        return ret;
    
    if ((ret = avfilter_graph_config(filterCtx->filter_graph, NULL)) < 0)
        return ret;
    
    // Make sure that the output frames have the correct size
    if (out->audioCodec->type == AVMEDIA_TYPE_AUDIO && !(out->audioCodec->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE)) {
        av_buffersink_set_frame_size(filterCtx->buffersink_ctx, out->audioCodecCtx->frame_size);
    }
    
    // Print summary of the source buffer
    inlink = filterCtx->buffersrc_ctx->outputs[0];
    av_get_channel_layout_string(args, sizeof(args), -1, inlink->channel_layout);
    
    
    av_log(NULL, AV_LOG_INFO, "Input: sample rate: %dHz; samlpe format:%s; channel layout:%s\n",
           (int)inlink->sample_rate,
           (char *)av_x_if_null(av_get_sample_fmt_name(inlink->format), "?"),
           args);
    
    // Print summary of the sink buffer
    outlink = filterCtx->buffersink_ctx->inputs[0];
    av_get_channel_layout_string(args, sizeof(args), -1, outlink->channel_layout);
    av_log(NULL, AV_LOG_INFO, "Output: sample rate: %dHz; samlpe format:%s; channel layout:%s\n",
           (int)outlink->sample_rate,
           (char *)av_x_if_null(av_get_sample_fmt_name(outlink->format), "?"),
           args);
    
    return 0;
}

static int push_frame(FilterContext* filterCtx, AVFrame* frame)
{
    int ret = av_buffersrc_add_frame_flags(filterCtx->buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_PUSH);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while feeding the audio filtergraph\n");
    }
    return ret;                         
}

static int transcode(AudioInfo* in, AudioInfo* out, ConvertSettings* settings, NotifyProgressCallback callback)
{
    AVPacket pkt;
    AVCodecContext *pCodecCtxFrom = in->audioCodecCtx;
    AVCodecContext *pCodecCtxTo = out->audioCodecCtx;
    AVFrame *frame = av_frame_alloc();
    AVFrame *filt_frame = av_frame_alloc();
    int got_frame = 0;
    int ret = 0;
    int64_t bytesProcessed = 0;
    int32_t percent_done = 0;
    av_init_packet(&pkt);
    int64_t total_size = 0;
    FilterContext filterCtx;
    
    total_size = avio_size(in->formatCtx->pb);
    if (total_size <= 0)
        total_size = avio_tell(in->formatCtx->pb);
    
    if(init_filter(&filterCtx, in, out) < 0) {
        printf("Could not initialize audio filter. \n");
        return -1;
    }
    
    while(av_read_frame(in->formatCtx, &pkt) >= 0)
    {
        bytesProcessed += pkt.size;
        percent_done = handle_progress(total_size, bytesProcessed, percent_done, callback);
        
        if(pkt.stream_index == in->audioStreamIndex)
        {
            
            
            //avcodec_get_frame_defaults(frame);
            got_frame = 0;
            ret = avcodec_decode_audio4(pCodecCtxFrom, frame, &got_frame, &pkt);
            
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error decoding audio frame.\n");
                continue;
            }
            
            if(got_frame)
            {
                // push frame into filter
                push_frame(&filterCtx, frame);
                
                while(1) {
                    // pull filtered frames
                    ret = av_buffersink_get_frame(filterCtx.buffersink_ctx, filt_frame);
                    if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        break;
                    if(ret < 0)
                        goto end;
                    
                    filt_frame->pts = AV_NOPTS_VALUE;
                    
                    // Write the decoded and converted audio frame
                    if(write_audio_frame(out->formatCtx, out->formatCtx->streams[0], filt_frame) < 0) {
                        return -1;
                    }
                    
                    av_frame_unref(filt_frame);
                }
            }                        
        }
        
        av_free_packet(&pkt);     
    }  
    
end:
    
    // write frame that are left in the queue
    flush_queue(out->formatCtx, pCodecCtxTo); 
    
    //avcodec_free_frame(&frame);
    av_write_trailer(out->formatCtx);
    
    return 0; 
}


static void init_audio_info(AudioInfo* in)
{
    in->audioCodec = NULL;
    in->audioCodecCtx = NULL;
    in->audioStreamIndex = 0;
    in->formatCtx = NULL;
}

static int open_input_file(const char* inputFileName, AudioInfo* in)
{
    int ret = 0;
    init_audio_info(in);
    
    printf("Open input file '%s'", inputFileName);
    
    // Open input file
    ret = avformat_open_input(&in->formatCtx, inputFileName, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "\n[open_input_file] error at avformat_open_input");
        goto end;
    }
    
    if(avformat_find_stream_info(in->formatCtx, NULL) < 0)
        return -1;
    
    in->audioStreamIndex = av_find_best_stream(in->formatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, &in->audioCodec, 0);
    if(in->audioStreamIndex < 0)
        return -1;  // Could not  find audio stream
    
    in->audioCodecCtx = in->formatCtx->streams[in->audioStreamIndex]->codec;
    
    // Init the audio codec
    if(avcodec_open2(in->audioCodecCtx, in->audioCodec, NULL) < 0)
        return -1;       
    
end:
    
    return ret;
}

static int open_output_file(const char* outputFileName, AudioInfo* in, AudioInfo* out, ConvertSettings* settings)
{
    static const char* output_formats[] = { NULL, "mp3", "ogg" };
    
    init_audio_info(out);
    
    AVOutputFormat *fmt = NULL;
    AVStream *audio_st = NULL;
    
    int ret = 0;
    
    printf("Open output file '%s'", outputFileName);
    
    avformat_alloc_output_context2(&out->formatCtx, NULL, output_formats[settings->target_format], outputFileName);
    fmt = out->formatCtx->oformat;
    
    if (fmt->audio_codec != AV_CODEC_ID_NONE) {
        // Add audio stream to output format
        audio_st = add_stream(out->formatCtx, &out->audioCodec, fmt->audio_codec, settings);
        if(audio_st == NULL)
            return -1;
        
        out->audioCodecCtx = audio_st->codec;
    }
    
    set_output_sample_fmt(in, out);
    
    // open audio codec
    if(open_audio(out) < 0)
        return -1;
    
    av_dump_format(out->formatCtx, 0, outputFileName, 1);
    
    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&out->formatCtx->pb, outputFileName, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s'", outputFileName);
            return -1;
        }
    }
    /* Write the stream header, if any. */
    ret = avformat_write_header(out->formatCtx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file: %s\n");
        return -1;
    }
    
    return 0;
}

void cpxvta_init()
{
    avcodec_register_all();
    av_register_all();
    avfilter_register_all();
    
    printf("video to audio converter initialized. \n");   
}







int cpxvta_convert(const char* inputFileName, const char* outputFileName, ConvertSettings* settings, NotifyProgressCallback callback)
{
    AudioInfo in;
    AudioInfo out;
    int ret = 0;
    
    ret = open_input_file(inputFileName, &in);
    if (ret < 0) {
        fprintf(stderr, "error at open_input_file");
        goto end;
    }

    ret = open_output_file(outputFileName, &in, &out, settings);
    if (ret < 0) {
        fprintf(stderr, "error at open_output_file");
        goto end;
    }
    
    ret = transcode(&in, &out, settings, callback);
    if (ret < 0) {
        fprintf(stderr, "error at transcode");
        goto end;
    }
    
    avcodec_close(in.audioCodecCtx);
    avcodec_close(out.audioCodecCtx);        
    
    //av_close_input_file(in.formatCtx);
    avio_close(out.formatCtx->pb);
    
    callback(100); 
    
end:
    
    return ret;
}
