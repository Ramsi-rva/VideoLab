#include "MediaSource.h"

#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

bool MediaSource::open(const std::string& path, std::string& err) {
    close();

    if (avformat_open_input(&fmt_, path.c_str(), nullptr, nullptr) < 0) {
        fmt_ = nullptr;
        err = "No se pudo abrir el archivo.";
        return false;
    }
    if (avformat_find_stream_info(fmt_, nullptr) < 0) {
        err = "No se pudo leer la informacion del archivo.";
        close();
        return false;
    }

    const AVCodec* codec = nullptr;
    stream_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (stream_ < 0 || !codec) {
        err = "El archivo no contiene video.";
        close();
        return false;
    }

    AVStream* st = fmt_->streams[stream_];
    dec_ = avcodec_alloc_context3(codec);
    if (!dec_ || avcodec_parameters_to_context(dec_, st->codecpar) < 0) {
        err = "No se pudo preparar el decodificador.";
        close();
        return false;
    }
    dec_->thread_count = 0; // automatico
    if (avcodec_open2(dec_, codec, nullptr) < 0) {
        err = "No se pudo abrir el decodificador.";
        close();
        return false;
    }

    w_ = dec_->width;
    h_ = dec_->height;
    tb_ = av_q2d(st->time_base);
    startOffset_ = (st->start_time != AV_NOPTS_VALUE) ? st->start_time * tb_ : 0.0;
    AVRational fr = av_guess_frame_rate(fmt_, st, nullptr);
    fps_ = (fr.num > 0 && fr.den > 0) ? av_q2d(fr) : 30.0;

    double dur = (fmt_->duration > 0) ? fmt_->duration / (double)AV_TIME_BASE : 0.0;
    if (dur <= 0.0 && st->duration > 0) dur = st->duration * tb_;
    duration_ = dur;

    frame_ = av_frame_alloc();
    rx_ = av_frame_alloc();
    pkt_ = av_packet_alloc();
    haveFrame_ = eof_ = draining_ = false;
    curPts_ = -1.0;

    if (w_ <= 0 || h_ <= 0 || duration_ <= 0.0) {
        err = "No se pudo determinar el tamano o la duracion del video.";
        close();
        return false;
    }
    return true;
}

void MediaSource::close() {
    if (sws_) { sws_freeContext(sws_); sws_ = nullptr; }
    if (frame_) av_frame_free(&frame_);
    if (rx_) av_frame_free(&rx_);
    if (pkt_) av_packet_free(&pkt_);
    if (dec_) avcodec_free_context(&dec_);
    if (fmt_) avformat_close_input(&fmt_);
    stream_ = -1;
    w_ = h_ = 0;
    haveFrame_ = false;
}

bool MediaSource::decodeNext() {
    for (;;) {
        int r = avcodec_receive_frame(dec_, rx_);
        if (r == 0) {
            av_frame_unref(frame_);
            av_frame_move_ref(frame_, rx_);
            double t = (frame_->best_effort_timestamp != AV_NOPTS_VALUE)
                           ? frame_->best_effort_timestamp * tb_ - startOffset_
                           : (haveFrame_ ? curPts_ + 1.0 / fps_ : 0.0);
            curPts_ = t;
            haveFrame_ = true;
            return true;
        }
        if (r == AVERROR_EOF) { eof_ = true; return false; }
        if (r != AVERROR(EAGAIN)) return false;
        if (draining_) { eof_ = true; return false; }

        int rr = av_read_frame(fmt_, pkt_);
        if (rr < 0) {
            avcodec_send_packet(dec_, nullptr); // vaciar el decodificador
            draining_ = true;
        } else {
            if (pkt_->stream_index == stream_) avcodec_send_packet(dec_, pkt_);
            av_packet_unref(pkt_);
        }
    }
}

bool MediaSource::seek(double t) {
    int64_t ts = (int64_t)((std::max(0.0, t) + startOffset_) / tb_);
    if (av_seek_frame(fmt_, stream_, ts, AVSEEK_FLAG_BACKWARD) < 0) return false;
    avcodec_flush_buffers(dec_);
    eof_ = draining_ = false;
    haveFrame_ = false;
    curPts_ = -1.0;
    return decodeNext();
}

bool MediaSource::getFrame(double t, int dw, int dh, uint8_t* dst) {
    if (!fmt_ || dw <= 0 || dh <= 0) return false;
    const double half = 0.5 / fps_;

    bool needSeek = !haveFrame_ || t < curPts_ - half || t > curPts_ + 1.5;
    if (needSeek && !seek(t) && !haveFrame_) return false;

    // Avanzar hasta el frame que cubre el instante t
    int guard = 0;
    while (curPts_ < t - half && !eof_ && guard++ < 3000) {
        if (!decodeNext()) break;
    }
    if (!haveFrame_ || frame_->width <= 0) return false;

    sws_ = sws_getCachedContext(sws_, frame_->width, frame_->height, (AVPixelFormat)frame_->format,
                                dw, dh, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_) return false;
    uint8_t* planes[4] = { dst, nullptr, nullptr, nullptr };
    int strides[4] = { dw * 4, 0, 0, 0 };
    sws_scale(sws_, frame_->data, frame_->linesize, 0, frame_->height, planes, strides);
    return true;
}
