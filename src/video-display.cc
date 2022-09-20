// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2020 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>

// TODO; help needed.
// * sound output ((platform independently ?)

#include "video-display.h"

#include <mutex>
#include <thread>
#include <utility>

#include "image-display.h"
#include "timg-time.h"

// libav: "U NO extern C in header ?"
extern "C" {
#include <libavcodec/avcodec.h>
#if HAVE_AVDEVICE
#    include <libavdevice/avdevice.h>
#endif
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

static constexpr bool kDebug = false;

namespace timg {
// Convert deprecated color formats to new and manually set the color range.
// YUV has funny ranges (16-235), while the YUVJ are 0-255. SWS prefers to
// deal with the YUV range, but then requires to set the output range.
// https://libav.org/documentation/doxygen/master/pixfmt_8h.html#a9a8e335cf3be472042bc9f0cf80cd4c5
static SwsContext *CreateSWSContext(const AVCodecContext *codec_ctx,
                                    int display_width, int display_height) {
    AVPixelFormat src_pix_fmt;
    bool src_range_extended_yuvj = true;
    // Remap deprecated to new pixel format.
    switch (codec_ctx->pix_fmt) {
    case AV_PIX_FMT_YUVJ420P: src_pix_fmt = AV_PIX_FMT_YUV420P; break;
    case AV_PIX_FMT_YUVJ422P: src_pix_fmt = AV_PIX_FMT_YUV422P; break;
    case AV_PIX_FMT_YUVJ444P: src_pix_fmt = AV_PIX_FMT_YUV444P; break;
    case AV_PIX_FMT_YUVJ440P: src_pix_fmt = AV_PIX_FMT_YUV440P; break;
    default: src_range_extended_yuvj = false; src_pix_fmt = codec_ctx->pix_fmt;
    }
    SwsContext *swsCtx = sws_getContext(
        codec_ctx->width, codec_ctx->height, src_pix_fmt, display_width,
        display_height, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
    if (src_range_extended_yuvj) {
        // Manually set the source range to be extended. Read modify write.
        int dontcare[4];
        int src_range, dst_range;
        int brightness, contrast, saturation;
        sws_getColorspaceDetails(swsCtx, (int **)&dontcare, &src_range,
                                 (int **)&dontcare, &dst_range, &brightness,
                                 &contrast, &saturation);
        const int *coefs = sws_getCoefficients(SWS_CS_DEFAULT);
        src_range        = 1;  // New src range.
        sws_setColorspaceDetails(swsCtx, coefs, src_range, coefs, dst_range,
                                 brightness, contrast, saturation);
    }
    return swsCtx;
}

static void dummy_log(void *, int, const char *, va_list) {
    // Let's not disturb our terminal with messages from here.
    // Maybe add logging to separate stream later.
}

static void OnceInitialize() {
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 9, 100)
    av_register_all();
#endif
#if HAVE_AVDEVICE
    avdevice_register_all();
#endif
    avformat_network_init();
    av_log_set_callback(dummy_log);
}

VideoLoader::VideoLoader(const std::string &filename) : ImageSource(filename) {
    static std::once_flag init;
    std::call_once(init, OnceInitialize);
}

VideoLoader::~VideoLoader() {
    avcodec_close(codec_context_);
    sws_freeContext(sws_context_);
    avformat_close_input(&format_context_);
    delete terminal_fb_;
}

const char *VideoLoader::VersionInfo() {
    return "libav " AV_STRINGIFY(LIBAVFORMAT_VERSION);
}

std::string VideoLoader::FormatTitle(const std::string &format_string) const {
    return FormatFromParameters(format_string, filename_, orig_width_,
                                orig_height_, "video");
}

bool VideoLoader::LoadAndScale(const DisplayOptions &display_options,
                               int frame_offset, int frame_count) {
    options_      = display_options;
    frame_offset_ = frame_offset;
    frame_count_  = frame_count;

    const char *file = (filename() == "-") ? "/dev/stdin" : filename().c_str();
    // Only consider applying transparency for certain file types we know
    // it might happen.
    for (const char *ending :
         {".png", ".gif", ".qoi", ".apng", ".svg", "/dev/stdin"}) {
        if (strcasecmp(file + strlen(file) - strlen(ending), ending) == 0) {
            maybe_transparent_ = true;
            break;
        }
    }

    format_context_ = avformat_alloc_context();
    int ret;
    if ((ret = avformat_open_input(&format_context_, file, NULL, NULL)) != 0) {
        char msg[100];
        av_strerror(ret, msg, sizeof(msg));
        if (kDebug) fprintf(stderr, "%s: %s\n", file, msg);
#if not HAVE_AVDEVICE
        // Not compiled in video device support. Try to give helpful message.
        if (strncmp(file, "/dev/video", strlen("/dev/video")) == 0) {
            fprintf(stderr,
                    "Need to compile with -DWITH_VIDEO_DEVICE=On to "
                    "access v4l2 device\n");
        }
#endif
        return false;
    }

    if (avformat_find_stream_info(format_context_, NULL) < 0) {
        if (kDebug) fprintf(stderr, "Couldn't find stream information\n");
        return false;
    }

    // Find the first video stream
    const AVCodecParameters *codec_parameters = nullptr;
    const AVCodec *av_codec                   = nullptr;
    for (int i = 0; i < (int)format_context_->nb_streams; ++i) {
        codec_parameters = format_context_->streams[i]->codecpar;
        av_codec         = avcodec_find_decoder(codec_parameters->codec_id);
        if (!av_codec) continue;
        if (codec_parameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index_ = i;
            break;
        }
    }
    if (!av_codec || video_stream_index_ == -1) return false;

    auto *stream    = format_context_->streams[video_stream_index_];
    AVRational rate = av_guess_frame_rate(format_context_, stream, nullptr);
    frame_duration_ = Duration::Nanos(1e9 * rate.den / rate.num);

    codec_context_ = avcodec_alloc_context3(av_codec);
    if (av_codec->capabilities & AV_CODEC_CAP_FRAME_THREADS &&
        std::thread::hardware_concurrency() > 1) {
        codec_context_->thread_type = FF_THREAD_FRAME;
        codec_context_->thread_count =
            std::min(4, (int)std::thread::hardware_concurrency());
    }
    if (avcodec_parameters_to_context(codec_context_, codec_parameters) < 0)
        return false;
    if (avcodec_open2(codec_context_, av_codec, NULL) < 0 ||
        codec_context_->width <= 0 || codec_context_->height <= 0)
        return false;

    orig_width_  = codec_context_->width;
    orig_height_ = codec_context_->height;

    /*
     * Prepare frame to hold the scaled target frame to be send to matrix.
     */
    int target_width  = 0;
    int target_height = 0;

    // Make display fit within canvas using the timg scaling utility.
    DisplayOptions opts(display_options);
    // Make sure we don't confuse users. Some image URLs actually end up here,
    // so make sure that it is clear certain options won't work.
    // TODO: this is a crude work-around. And while we tell the user what to
    // do, it would be better if we'd dealt with it already.
    if (opts.crop_border != 0 || opts.auto_crop) {
        const bool is_url = (strncmp(file, "http://", 7) == 0 ||
                             strncmp(file, "https://", 8) == 0);
        fprintf(stderr,
                "%s%s is handled by video subsystem. "
                "Unfortunately, no auto-crop feature is implemented there.\n",
                is_url ? "URL " : "", file);
        if (is_url) {
            fprintf(stderr,
                    "use:\n\twget -qO- %s | timg -T%d -\n... instead "
                    "for this to work\n",
                    file, opts.crop_border);
        }
    }
    opts.fill_height = false;  // This only makes sense for horizontal scroll.
    CalcScaleToFitDisplay(codec_context_->width, codec_context_->height, opts,
                          false, &target_width, &target_height);

    if (display_options.center_horizontally) {
        center_indentation_ = (display_options.width - target_width) / 2;
    }
    // initialize SWS context for software scaling
    sws_context_ =
        CreateSWSContext(codec_context_, target_width, target_height);
    if (!sws_context_) {
        if (kDebug)
            fprintf(stderr, "Trouble doing scaling to %dx%d :(\n", opts.width,
                    opts.height);
        return false;
    }

    // Framebuffer to interface with the timg TerminalCanvas
    terminal_fb_ = new timg::Framebuffer(target_width, target_height);
    return true;
}

void VideoLoader::AlphaBlendFramebuffer() {
    if (!maybe_transparent_) return;
    terminal_fb_->AlphaComposeBackground(
        options_.bgcolor_getter, options_.bg_pattern_color,
        options_.pattern_size * options_.cell_x_px,
        options_.pattern_size * options_.cell_y_px / 2);
}

void VideoLoader::SendFrames(const Duration &duration, int loops,
                             const volatile sig_atomic_t &interrupt_received,
                             const Renderer::WriteFramebufferFun &sink) {
    const bool frame_limit = (frame_count_ > 0);

    if (frame_count_ == 1)  // If there is only one frame, nothing to repeat.
        loops = 1;

    // Unlike animated images, in which a not set value in loops means
    // 'infinite' repeat, it feels more sensible to show videos exactly once
    // then. A negative value otherwise is considered 'forever'
    const bool animated_png =
        filename().size() > 3 &&
        (strcasecmp(filename().c_str() + filename().size() - 3, "png") == 0);
    const bool loop_forever =
        (loops < 0) && (loops != timg::kNotInitialized || animated_png);

    if (loops == timg::kNotInitialized && !animated_png) loops = 1;

    AVPacket *packet = av_packet_alloc();
    bool is_first    = true;
    timg::Duration time_from_first_frame;

    // We made guesses above if something is potentially an animation, but
    // we don't know until we observe how many frames there are - we don't
    // know beforehand.
    // So we will only loop iff we do not observe exactly one frame.
    int observed_frame_count = 0;

    AVFrame *decode_frame = av_frame_alloc();  // Decode video into this
    for (int k = 0;
         ((loop_forever || k < loops) && observed_frame_count != 1) &&
         !interrupt_received && time_from_first_frame < duration;
         ++k) {
        if (k > 0) {
            // Rewind unless we're just starting.
            av_seek_frame(format_context_, video_stream_index_, 0,
                          AVSEEK_FLAG_ANY);
            avcodec_flush_buffers(codec_context_);
        }
        observed_frame_count = 0;
        int remaining_frames = frame_count_;
        int skip_offset      = frame_offset_;
        int decode_in_flight = 0;

        bool state_reading = true;

        while (!interrupt_received && time_from_first_frame < duration &&
               (!frame_limit || remaining_frames > 0)) {
            if (state_reading && av_read_frame(format_context_, packet) != 0) {
                state_reading = false;  // Ran out of packets from input
            }

            if (!state_reading && decode_in_flight == 0)
                break;  // Decoder fully drained.

            if (state_reading && packet->stream_index != video_stream_index_) {
                av_packet_unref(packet);
                continue;  // Not a packet we're interested in
            }

            if (state_reading) {
                if (avcodec_send_packet(codec_context_, packet) == 0) {
                    ++decode_in_flight;
                }
                av_packet_unref(packet);
            }
            else {
                avcodec_send_packet(codec_context_, nullptr);  // Trigger drain
            }

            while (decode_in_flight &&
                   avcodec_receive_frame(codec_context_, decode_frame) == 0) {
                --decode_in_flight;
                if (skip_offset > 0) {
                    // TODO: there is probably a faster/better way to skip
                    // ahead to the last keyframe first.
                    --skip_offset;
                    continue;
                }

                time_from_first_frame.Add(frame_duration_);
                // TODO: when frame skipping enabled, avoid this step if we're
                // falling behind.
                sws_scale(sws_context_, decode_frame->data,
                          decode_frame->linesize, 0, codec_context_->height,
                          terminal_fb_->row_data(), terminal_fb_->stride());
                AlphaBlendFramebuffer();
                const int dy = is_first ? 0 : -terminal_fb_->height();
                sink(center_indentation_, dy, *terminal_fb_,
                     is_first ? SeqType::StartOfAnimation
                              : SeqType::AnimationFrame,
                     time_from_first_frame);
                is_first = false;
                if (frame_limit) --remaining_frames;
                ++observed_frame_count;
            }
        }
    }

    av_frame_free(&decode_frame);
    av_packet_free(&packet);
}

}  // namespace timg
