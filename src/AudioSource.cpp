#include "AudioSource.h"

#include <algorithm>
#include <cstdlib>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

bool probeStreams(const std::string& path, StreamInfo& info, std::string& err) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) {
        err = "No se pudo abrir el archivo.";
        return false;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        err = "No se pudo leer la informacion del archivo.";
        return false;
    }
    info = StreamInfo{};
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const AVCodecParameters* cp = fmt->streams[i]->codecpar;
        if (cp->codec_type == AVMEDIA_TYPE_VIDEO && !(fmt->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC))
            info.hasVideo = true;
        else if (cp->codec_type == AVMEDIA_TYPE_AUDIO)
            info.hasAudio = true;
    }
    if (fmt->duration > 0) info.duration = fmt->duration / (double)AV_TIME_BASE;
    avformat_close_input(&fmt);
    return true;
}

bool AudioSource::load(const std::string& path, std::string& err) {
    pcm_.clear();
    peaks_.clear();

    AVFormatContext* fmt = nullptr;
    AVCodecContext* dec = nullptr;
    SwrContext* swr = nullptr;
    AVFrame* fr = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();
    AVChannelLayout outLayout{};
    av_channel_layout_default(&outLayout, 2);
    bool ok = false;
    int stream = -1;

    auto cleanup = [&]() {
        if (swr) swr_free(&swr);
        if (dec) avcodec_free_context(&dec);
        if (fmt) avformat_close_input(&fmt);
        if (fr) av_frame_free(&fr);
        if (pkt) av_packet_free(&pkt);
        av_channel_layout_uninit(&outLayout);
    };

    do {
        if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) { fmt = nullptr; err = "No se pudo abrir el archivo."; break; }
        if (avformat_find_stream_info(fmt, nullptr) < 0) { err = "No se pudo leer la informacion del archivo."; break; }
        const AVCodec* codec = nullptr;
        stream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
        if (stream < 0 || !codec) { err = "El archivo no contiene audio."; break; }
        dec = avcodec_alloc_context3(codec);
        if (!dec || avcodec_parameters_to_context(dec, fmt->streams[stream]->codecpar) < 0) { err = "No se pudo preparar el decodificador de audio."; break; }
        if (avcodec_open2(dec, codec, nullptr) < 0) { err = "No se pudo abrir el decodificador de audio."; break; }

        // reservar segun la duracion conocida (si la hay)
        if (fmt->duration > 0) pcm_.reserve((size_t)(fmt->duration / (double)AV_TIME_BASE * kRate * 2) + 4096);

        std::vector<int16_t> conv;
        bool eof = false, draining = false;
        auto handleFrame = [&]() -> bool {
            if (!swr) {
                AVChannelLayout inL{};
                if (fr->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC || fr->ch_layout.nb_channels <= 0)
                    av_channel_layout_default(&inL, std::max(1, fr->ch_layout.nb_channels));
                else
                    av_channel_layout_copy(&inL, &fr->ch_layout);
                int r = swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_S16, kRate, &inL, (AVSampleFormat)fr->format,
                                            fr->sample_rate, 0, nullptr);
                av_channel_layout_uninit(&inL);
                if (r < 0 || !swr || swr_init(swr) < 0) { err = "No se pudo preparar el convertidor de audio."; return false; }
            }
            int maxOut = swr_get_out_samples(swr, fr->nb_samples) + 64;
            conv.resize((size_t)maxOut * 2);
            uint8_t* outp[1] = { (uint8_t*)conv.data() };
            int n = swr_convert(swr, outp, maxOut, (const uint8_t**)fr->extended_data, fr->nb_samples);
            if (n < 0) { err = "Error al convertir el audio."; return false; }
            pcm_.insert(pcm_.end(), conv.begin(), conv.begin() + (size_t)n * 2);
            return true;
        };

        bool fail = false;
        while (!eof && !fail) {
            int r = avcodec_receive_frame(dec, fr);
            if (r == 0) { if (!handleFrame()) fail = true; av_frame_unref(fr); continue; }
            if (r == AVERROR_EOF) { eof = true; break; }
            if (r != AVERROR(EAGAIN)) break;
            if (draining) { eof = true; break; }
            int rr = av_read_frame(fmt, pkt);
            if (rr < 0) { avcodec_send_packet(dec, nullptr); draining = true; }
            else { if (pkt->stream_index == stream) avcodec_send_packet(dec, pkt); av_packet_unref(pkt); }
        }
        if (fail) break;
        if (swr) { // vaciar el remuestreador
            int maxOut = swr_get_out_samples(swr, 0) + 4096;
            conv.resize((size_t)maxOut * 2);
            uint8_t* outp[1] = { (uint8_t*)conv.data() };
            int n = swr_convert(swr, outp, maxOut, nullptr, 0);
            if (n > 0) pcm_.insert(pcm_.end(), conv.begin(), conv.begin() + (size_t)n * 2);
        }
        if (pcm_.size() < 2 * 64) { err = "El audio esta vacio."; break; }
        ok = true;
    } while (false);

    cleanup();
    if (!ok) { pcm_.clear(); return false; }

    // picos para dibujar la forma de onda
    const int64_t n = frames();
    peaks_.assign((size_t)((n + kPeakWin - 1) / kPeakWin), 0);
    for (int64_t w = 0; w < (int64_t)peaks_.size(); ++w) {
        int64_t a = w * kPeakWin, b = std::min(n, a + kPeakWin);
        int m = 0;
        for (int64_t i = a * 2; i < b * 2; ++i) m = std::max(m, std::abs((int)pcm_[(size_t)i]));
        peaks_[(size_t)w] = (uint8_t)std::min(255, m >> 7);
    }
    return true;
}
