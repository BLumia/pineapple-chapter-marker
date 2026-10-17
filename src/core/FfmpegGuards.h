#pragma once

#include <type_traits>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

// RAII guards for FFmpeg objects (shared by several core translation units)
namespace pcm::ff {

// Takes over after avformat_open_input succeeds; closes on destruction
class FormatContextGuard {
public:
    explicit FormatContextGuard(AVFormatContext** ctx) : m_ctx(ctx) {}
    ~FormatContextGuard()
    {
        if (m_ctx && *m_ctx)
            avformat_close_input(m_ctx);
    }
    AVFormatContext*& get() { return *m_ctx; }

private:
    AVFormatContext** m_ctx;
};

// Paired release for avcodec_alloc_context3 / av_packet_alloc / av_frame_alloc
template <typename T>
class AllocGuard {
public:
    explicit AllocGuard(T** p) : m_p(p) {}
    ~AllocGuard()
    {
        if (m_p && *m_p) {
            if constexpr (std::is_same_v<T, AVCodecContext>)
                avcodec_free_context(m_p);
            else if constexpr (std::is_same_v<T, AVPacket>)
                av_packet_free(m_p);
            else if constexpr (std::is_same_v<T, AVFrame>)
                av_frame_free(m_p);
            *m_p = nullptr;
        }
    }

private:
    T** m_p;
};

} // namespace pcm::ff
