// Decodificador de un archivo de video (FFmpeg): entrega el frame de un instante dado.
#pragma once
#include <cstdint>
#include <string>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

class MediaSource {
public:
    MediaSource() = default;
    ~MediaSource() { close(); }
    MediaSource(const MediaSource&) = delete;
    MediaSource& operator=(const MediaSource&) = delete;

    bool open(const std::string& utf8Path, std::string& err);
    void close();

    // Escribe en dst (dw*dh*4 bytes, BGRA) el frame correspondiente al tiempo t (s).
    bool getFrame(double t, int dw, int dh, uint8_t* dst);

    int    width() const { return w_; }
    int    height() const { return h_; }
    double fps() const { return fps_; }
    double duration() const { return duration_; }

private:
    bool decodeNext();
    bool seek(double t);

    AVFormatContext* fmt_ = nullptr;
    AVCodecContext*  dec_ = nullptr;
    SwsContext*      sws_ = nullptr;
    AVFrame*         frame_ = nullptr;  // ultimo frame valido
    AVFrame*         rx_ = nullptr;     // buffer de recepcion
    AVPacket*        pkt_ = nullptr;
    int    stream_ = -1;
    int    w_ = 0, h_ = 0;
    double fps_ = 30.0, tb_ = 0.0, startOffset_ = 0.0, duration_ = 0.0;
    double curPts_ = -1.0;
    bool   haveFrame_ = false, eof_ = false, draining_ = false;
};
