/*
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include <sstream>
#include "Logger.hpp"
#include "CameraDevice.hpp"

static char s_filterDesc[]
        = "drawtext=fontfile=/System/Library/Fonts/STHeiti Medium.ttc:x=5:y=5:fontcolor=white:fontsize=24:shadowcolor=black:shadowx=2:shadowy=2:text=\\'%{localtime\\:%Y-%m-%d %H\\\\:%M\\\\:%S}\\'";

using namespace std;

string GenerateLocalDateTimeString() {

    stringstream ss;
    struct tm *local;
    time_t now;

    time(&now);
    local = localtime(&now);

#ifdef WIN32
    ss << "drawtext=fontfile=\\'C\\:\\/WINDOWS\\/fonts\\/simsun.ttc\\':x=5:y=5:fontcolor=white:fontsize=24:shadowcolor=black:shadowx=2:shadowy=2:text=\\'";
#elif __APPLE__
    ss << "drawtext=fontfile=/System/Library/Fonts/STHeiti Medium.ttc:x=5:y=5:fontcolor=white:fontsize=24:shadowcolor=black:shadowx=2:shadowy=2:text=\\'";
#elif __linux__
    ss << "drawtext=x=5:y=5:fontcolor=white:fontsize=24:shadowcolor=black:shadowx=2:shadowy=2:text=\\'";
#endif
    ss << "%{localtime\\:%Y-%m-%d} ";
    string wday;
    switch (local->tm_wday) {
        case 0:
            wday = "星期日";
            break;
        case 1:
            wday = "星期一";
            break;
        case 2:
            wday = "星期二";
            break;
        case 3:
            wday = "星期三";
            break;
        case 4:
            wday = "星期四";
            break;
        case 5:
            wday = "星期五";
            break;
        case 6:
            wday = "星期六";
            break;
    }
    ss << wday << " ";
    ss << "%{localtime\\:%H\\\\:%M\\\\:%S} ";
    ss << "IPC";

    return ss.str();
}

CameraDevice::CameraDevice() :
        isReady_(false),
        filterGraph_(NULL),
        filterContextSrc_(NULL),
        filterContextSink_(NULL),
        frameYuyv422_(NULL),
        frameYuyv422Watermark_(NULL),
        codecContextRaw_(NULL),
        packetRaw_(NULL),
        formatContext_(NULL) {

    // System initialization
    av_register_all();
    avdevice_register_all();
    avcodec_register_all();
    avfilter_register_all();
}

CameraDevice::~CameraDevice() {

    Close();
}

bool CameraDevice::Open(const char *format, const char *device,
                          int width, int height, int fps) {

    if (isReady_) {
        return false;
    }

    enum AVPixelFormat pix_fmts[] = {AV_PIX_FMT_YUYV422, AV_PIX_FMT_NONE};
    AVInputFormat *inputFormat = NULL;
    AVBufferSinkParams *params = NULL;
    AVFilter *filterSrc = NULL, *filterSink = NULL;
    AVFilterInOut *filterInOutIn = NULL, *filterInOutOut = NULL;
    AVCodec *codecRaw = NULL;
    AVDictionary *options = NULL;
    int videoIndex = -1;
    char args[512];

    // Open and initialize the camera device
    formatContext_ = avformat_alloc_context();
    if (formatContext_ == NULL) {
        LOG(error) << "Cannot alloc context";
        return false;
    }
    inputFormat = av_find_input_format(format);
    if (inputFormat == NULL) {
        LOG(error) << "Cannot find input format:" << format;
        goto cleanup;
    }

    snprintf(args, sizeof(args), "%d", fps);
    av_dict_set(&options, "framerate", args, 0);
    snprintf(args, sizeof(args), "%dx%d", width, height);
    av_dict_set(&options, "video_size", args, 0);
    av_dict_set(&options, "pixel_format", "yuyv422", 0);
    av_dict_set(&options, "rtbufsize", "2000M", 0);
//    av_dict_set(&options, "thread", "6", 0);
    if (avformat_open_input(&formatContext_, device, inputFormat, &options) < 0) {
        LOG(error) << "Cannot open device:" << device;
        goto cleanup;
    }

    // Initializes the video decoder
    if (avformat_find_stream_info(formatContext_, NULL) < 0) {
        LOG(error) << "Cannot find stream info";
        goto cleanup;
    }

    for (int i = 0; i < formatContext_->nb_streams; i++) {
        if (formatContext_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoIndex = i;
            break;
        }
    }
    if (videoIndex == -1) {
        LOG(error) << "Cannot find video in media";
        goto cleanup;
    }

    codecContextRaw_ = avcodec_alloc_context3(NULL);
    if (codecContextRaw_ == NULL) {
        LOG(error) << "Cannot alloc decoder context";
        goto cleanup;
    }
    if (avcodec_parameters_to_context(codecContextRaw_, formatContext_->streams[videoIndex]->codecpar) < 0) {
        LOG(error) << "Cannot convert param to context";
        goto cleanup;
    }
    LOG(info) << formatContext_->streams[videoIndex]->time_base.num << " / "
              << formatContext_->streams[videoIndex]->time_base.den;
    //av_codec_set_pkt_timebase(codecContextRaw_, formatContext_->streams[videoIndex]->time_base);
    codecContextRaw_->time_base = formatContext_->streams[videoIndex]->time_base;
    codecRaw = avcodec_find_decoder(codecContextRaw_->codec_id);
    if (!codecRaw) {
        LOG(error) << "Cannot find decoder";
        goto cleanup;
    }

    if (avcodec_open2(codecContextRaw_, codecRaw, NULL) < 0) {
        LOG(error) << "Cannot open raw codec context";
        goto cleanup;
    }

    // Create video frame structure
    frameYuyv422_ = av_frame_alloc();
    frameYuyv422Watermark_ = av_frame_alloc();
    if ((frameYuyv422_ == NULL) || (frameYuyv422Watermark_ == NULL)) {
        LOG(error) << "Cannot alloc frame";
        goto cleanup;
    }

    frameYuyv422_->format = AV_PIX_FMT_YUYV422;
    frameYuyv422_->width = width;
    frameYuyv422_->height = height;
    frameYuyv422Watermark_->format = AV_PIX_FMT_YUYV422;
    frameYuyv422Watermark_->width = width;
    frameYuyv422Watermark_->height = height;

//    if ((av_frame_get_buffer(frameYuyv422_, 0) < 0)
//        || (av_frame_get_buffer(frameYuyv422Watermark_, 0) < 0)
//        || (av_frame_get_buffer(frameYuv420p_, 0) < 0))

    // Create video filter graph
    filterGraph_ = avfilter_graph_alloc();
    if (!filterGraph_) {
        LOG(error) << "Cannot alloc filter graph";
        goto cleanup;
    }

    filterSrc = avfilter_get_by_name("buffer");
    filterSink = avfilter_get_by_name("buffersink");
    if (!filterSrc || !filterSink) {
        LOG(error) << "Cannot get filter";
        goto cleanup;
    }

    filterInOutIn = avfilter_inout_alloc();
    filterInOutOut = avfilter_inout_alloc();
    if (!filterInOutIn || !filterInOutOut) {
        LOG(error) << "Cannot alloc filter inout";
        goto cleanup;
    }

    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             width, height, AV_PIX_FMT_YUYV422, 1, fps, 1, 1
    );
    LOG(info) << args;

    if ((avfilter_graph_create_filter(&filterContextSrc_, filterSrc, "in", args, NULL, filterGraph_) < 0)
        || (avfilter_graph_create_filter(&filterContextSink_, filterSink, "out", NULL, params, filterGraph_) < 0)) {
        LOG(error) << "Cannot create filter";
        av_free(params);
        goto cleanup;
    }
    av_opt_set_int_list(filterContextSink_, "pix_fmts", pix_fmts,
                        AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);

    /* Endpoints for the filter graph. */
    filterInOutOut->name = av_strdup("in");
    filterInOutOut->filter_ctx = filterContextSrc_;
    filterInOutOut->pad_idx = 0;
    filterInOutOut->next = NULL;

    filterInOutIn->name = av_strdup("out");
    filterInOutIn->filter_ctx = filterContextSink_;
    filterInOutIn->pad_idx = 0;
    filterInOutIn->next = NULL;

    if (avfilter_graph_parse(filterGraph_, GenerateLocalDateTimeString().c_str(),
                             filterInOutIn, filterInOutOut, NULL) < 0) {
        LOG(error) << "Cannot parse filter graph";
        goto cleanup;
    }

    if (avfilter_graph_config(filterGraph_, NULL) < 0) {
        LOG(error) << "Cannot config filter graph";
        goto cleanup;
    }

    // Initializes video packet structure
    packetRaw_ = av_packet_alloc();
    if (packetRaw_ == NULL) {
        LOG(error) << "Cannot malloc packet";
        goto cleanup;
    }

    isReady_ = true;
    return true;

cleanup:
    if (filterGraph_) {
        avfilter_graph_free(&filterGraph_);
        filterGraph_ = NULL;
    }

    if (codecContextRaw_) {
        avcodec_close(codecContextRaw_);
        avcodec_free_context(&codecContextRaw_);
        codecContextRaw_ = NULL;
    }

    if (formatContext_) {
        avformat_close_input(&formatContext_);
        avformat_free_context(formatContext_);
        formatContext_ = NULL;
    }

    if (packetRaw_) {
        av_packet_free(&packetRaw_);
        packetRaw_ = NULL;
    }

    if (frameYuyv422_) {
        av_frame_free(&frameYuyv422_);
        frameYuyv422_ = NULL;
    }

    if (frameYuyv422Watermark_) {
        av_frame_free(&frameYuyv422Watermark_);
        frameYuyv422Watermark_ = NULL;
    }

    return false;
}

void CameraDevice::Close() {

    if (isReady_) {
        isReady_ = false;

        if (filterGraph_) {
            avfilter_graph_free(&filterGraph_);
            filterGraph_ = NULL;
        }

        if (codecContextRaw_) {
            avcodec_close(codecContextRaw_);
            avcodec_free_context(&codecContextRaw_);
            codecContextRaw_ = NULL;
        }

        if (formatContext_) {
            avformat_close_input(&formatContext_);
            avformat_free_context(formatContext_);
            formatContext_ = NULL;
        }

        if (packetRaw_) {
            av_packet_free(&packetRaw_);
            packetRaw_ = NULL;
        }

        if (frameYuyv422_) {
            av_frame_free(&frameYuyv422_);
            frameYuyv422_ = NULL;
        }

        if (frameYuyv422Watermark_) {
            av_frame_free(&frameYuyv422Watermark_);
            frameYuyv422Watermark_ = NULL;
        }
    }
}

AVFrame* CameraDevice::Capture() {

    int ret = 0;

    if (!isReady_) {
        return NULL;
    }

    // Read video data packet
    if ((ret = av_read_frame(formatContext_, packetRaw_)) < 0) {
        LOG(error) << "Cannot read frame";
        return NULL;
    }

    // Decode data to frame yuyv422
    if ((ret = avcodec_send_packet(codecContextRaw_, packetRaw_)) < 0) {
        LOG(error) << "Cannot send packet to decode context";
        return NULL;
    }

    ret = avcodec_receive_frame(codecContextRaw_, frameYuyv422_);
    if (ret < 0) {
        LOG(error) << "Error while receiveing a frame from the decoder";
        return NULL;
    }

    frameYuyv422_->pts = av_frame_get_best_effort_timestamp(frameYuyv422_);
    if (av_buffersrc_add_frame_flags(filterContextSrc_, frameYuyv422_, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
        LOG(error) << "Cannot add frame to src buffer";
        return NULL;
    }

    ret = av_buffersink_get_frame(filterContextSink_, frameYuyv422Watermark_);
    if (ret < 0) {
        return NULL;
    }

    AVFrame* capFrame = av_frame_clone(frameYuyv422Watermark_);

    av_frame_unref(frameYuyv422Watermark_);
    av_frame_unref(frameYuyv422_);
    av_packet_unref(packetRaw_);

    return capFrame;
}
