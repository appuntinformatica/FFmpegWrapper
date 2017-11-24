//
//  ffmpeg.h
//  Example
//
//  Created by Andrea on 10/05/2017.
//  Copyright © 2017 Andrea. All rights reserved.
//

#ifndef ffmpeg_h
#define ffmpeg_h

// https://github.com/cpawelzik/libcpxvta

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavutil/file.h"


#include "libavfilter/avfiltergraph.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"

#endif /* ffmpeg_h */



#define TARGET_FORMAT_MP3   1
#define TARGET_FORMAT_OGG   2

typedef struct ConvertSettings {
    int target_format;
    int rs_sample_rate;
    int rs_bit_rate;
    int rs_channels;
} ConvertSettings;

typedef void (*NotifyProgressCallback) (int progress);


typedef struct FilterContext {
    AVFilterGraph *filter_graph;
    AVFilterContext *buffersink_ctx;
    AVFilterContext *buffersrc_ctx;
} FilterContext;

typedef struct AudioInfo {
    AVCodecContext* audioCodecCtx;
    AVFormatContext* formatCtx;
    AVCodec* audioCodec;
    int audioStreamIndex;
} AudioInfo;

int avio_reading(const char *input_filename);

int cpxvta_convert(const char* inputFileName, const char* outputFileName, ConvertSettings* settings, NotifyProgressCallback callback);
