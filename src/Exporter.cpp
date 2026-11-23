#include "Exporter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "AudioMix.h"
#include "Renderer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

bool exportProject(const Project& p, const AudioBank& bank, const std::string& path, std::atomic<bool>& cancel,
                   const std::function<void(int)>& onProgress, std::string& err) {
    const int W = p.outW & ~1, H = p.outH & ~1;
    const int FPS = std::max(1, (int)std::lround(p.fps));
    const int total = (int)std::ceil(p.duration() * FPS);
    if (total <= 0) { err = "El proyecto esta vacio."; return false; }

    AVFormatContext* oc = nullptr;
    AVCodecContext* enc = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws = nullptr;
    AVStream* st = nullptr;
    AVCodecContext* aenc = nullptr;
    AVFrame* aframe = nullptr;
    AVStream* ast = nullptr;
    bool fileOpened = false;
    const bool withAudio = p.hasAudio();

    auto cleanup = [&]() {
        if (sws) sws_freeContext(sws);
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        if (enc) avcodec_free_context(&enc);
        if (aframe) av_frame_free(&aframe);
        if (aenc) avcodec_free_context(&aenc);
        if (oc) {
            if (fileOpened && oc->pb) avio_closep(&oc->pb);
            avformat_free_context(oc);
        }
    };
    auto fail = [&](const char* m) { err = m; cleanup(); return false; };

    if (avformat_alloc_output_context2(&oc, nullptr, nullptr, path.c_str()) < 0 || !oc)
        return fail("No se pudo crear el contenedor de salida.");

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    if (!codec) return fail("No se encontro el codificador MPEG-4.");

    st = avformat_new_stream(oc, nullptr);
    enc = avcodec_alloc_context3(codec);
    if (!st || !enc) return fail("No se pudo crear el flujo de video.");

    enc->width = W;
    enc->height = H;
    enc->pix_fmt = AV_PIX_FMT_YUV420P;
    enc->time_base = AVRational{ 1, FPS };
    enc->framerate = AVRational{ FPS, 1 };
    enc->bit_rate = (p.bitrateKbps > 0) ? (int64_t)p.bitrateKbps * 1000 : (int64_t)W * H * FPS / 5;
    enc->gop_size = FPS * 2;
    enc->max_b_frames = 0;
    if (oc->oformat->flags & AVFMT_GLOBALHEADER) enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(enc, codec, nullptr) < 0) return fail("No se pudo abrir el codificador.");
    if (avcodec_parameters_from_context(st->codecpar, enc) < 0) return fail("Error de parametros del codec.");
    st->time_base = enc->time_base;

    // --- audio AAC (solo si el proyecto tiene audio audible)
    if (withAudio) {
        const AVCodec* acodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!acodec) return fail("No se encontro el codificador de audio AAC.");
        ast = avformat_new_stream(oc, nullptr);
        aenc = avcodec_alloc_context3(acodec);
        if (!ast || !aenc) return fail("No se pudo crear el flujo de audio.");
        aenc->sample_fmt = AV_SAMPLE_FMT_FLTP;
        aenc->sample_rate = AudioSource::kRate;
        av_channel_layout_default(&aenc->ch_layout, 2);
        aenc->bit_rate = 192000;
        aenc->time_base = AVRational{ 1, AudioSource::kRate };
        if (oc->oformat->flags & AVFMT_GLOBALHEADER) aenc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(aenc, acodec, nullptr) < 0) return fail("No se pudo abrir el codificador de audio.");
        if (avcodec_parameters_from_context(ast->codecpar, aenc) < 0) return fail("Error de parametros del audio.");
        ast->time_base = aenc->time_base;
        aframe = av_frame_alloc();
        if (!aframe) return fail("Sin memoria.");
    }

    if (!(oc->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) < 0)
            return fail("No se pudo crear el archivo de salida (revisa la ruta y permisos).");
        fileOpened = true;
    }
    if (avformat_write_header(oc, nullptr) < 0) return fail("No se pudo escribir la cabecera.");

    frame = av_frame_alloc();
    pkt = av_packet_alloc();
    if (!frame || !pkt) return fail("Sin memoria.");
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = W;
    frame->height = H;
    if (av_frame_get_buffer(frame, 0) < 0) return fail("Sin memoria para el frame.");

    sws = sws_getContext(W, H, AV_PIX_FMT_BGRA, W, H, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) return fail("No se pudo crear el convertidor de color.");

    auto drainEnc = [&](AVCodecContext* c, AVStream* s) -> bool {
        for (;;) {
            int r = avcodec_receive_packet(c, pkt);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
            if (r < 0) return false;
            av_packet_rescale_ts(pkt, c->time_base, s->time_base);
            pkt->stream_index = s->index;
            if (av_interleaved_write_frame(oc, pkt) < 0) return false;
        }
    };
    auto drain = [&]() -> bool { return drainEnc(enc, st); };

    // Audio: mezcla 'chunk' muestras desde audioPos y las codifica
    const int64_t audioTotal = withAudio ? (int64_t)std::ceil(p.duration() * AudioSource::kRate) : 0;
    int64_t audioPos = 0;
    std::vector<float> mixbuf;
    auto encodeAudio = [&](int64_t upTo) -> bool {
        const int chunk = aenc ? (aenc->frame_size > 0 ? aenc->frame_size : 1024) : 1024;
        while (aenc && audioPos < std::min(upTo, audioTotal)) {
            int n = (int)std::min<int64_t>(chunk, audioTotal - audioPos);
            mixbuf.resize((size_t)n * 2);
            mixAudio(p, bank, audioPos, n, mixbuf.data());
            aframe->nb_samples = n;
            aframe->format = AV_SAMPLE_FMT_FLTP;
            aframe->sample_rate = AudioSource::kRate;
            av_channel_layout_copy(&aframe->ch_layout, &aenc->ch_layout);
            if (!aframe->buf[0]) { if (av_frame_get_buffer(aframe, 0) < 0) return false; }
            else if (av_frame_make_writable(aframe) < 0) return false;
            float* l = (float*)aframe->data[0];
            float* r = (float*)aframe->data[1];
            for (int i = 0; i < n; ++i) { l[i] = mixbuf[(size_t)i * 2]; r[i] = mixbuf[(size_t)i * 2 + 1]; }
            aframe->pts = audioPos;
            if (avcodec_send_frame(aenc, aframe) < 0) return false;
            if (!drainEnc(aenc, ast)) return false;
            audioPos += n;
        }
        return true;
    };

    // Lienzo con el tamano de salida; el render del proyecto se hace en este hilo
    Canvas canvas;
    if (!canvas.create(W, H)) return fail("No se pudo crear el lienzo de render.");
    Renderer renderer;

    int lastPct = -1;
    for (int i = 0; i < total; ++i) {
        if (cancel.load()) {
            err = "Exportacion cancelada.";
            cleanup();
            std::remove(path.c_str());
            return false;
        }
        renderer.render(p, (double)i / FPS, canvas);

        if (av_frame_make_writable(frame) < 0) return fail("Error interno de memoria.");
        const uint8_t* src[1] = { canvas.bits };
        int srcStride[1] = { W * 4 };
        sws_scale(sws, src, srcStride, 0, H, frame->data, frame->linesize);
        frame->pts = i;

        if (avcodec_send_frame(enc, frame) < 0) return fail("Error al codificar un frame.");
        if (!drain()) return fail("Error al escribir el video.");
        if (withAudio && !encodeAudio((int64_t)(i + 1) * AudioSource::kRate / FPS + 2048))
            return fail("Error al codificar el audio.");

        int pct = (int)((i + 1) * 100LL / total);
        if (pct != lastPct) { lastPct = pct; if (onProgress) onProgress(pct); }
    }

    avcodec_send_frame(enc, nullptr); // vaciar el codificador
    if (!drain()) return fail("Error al finalizar el video.");
    if (withAudio) {
        if (!encodeAudio(audioTotal)) return fail("Error al codificar el audio.");
        avcodec_send_frame(aenc, nullptr);
        if (!drainEnc(aenc, ast)) return fail("Error al finalizar el audio.");
    }
    if (av_write_trailer(oc) < 0) return fail("No se pudo finalizar el archivo.");

    cleanup();
    return true;
}
