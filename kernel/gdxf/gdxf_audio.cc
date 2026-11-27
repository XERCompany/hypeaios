#include "kernel/gdxf/gdxf.h"

#include "kernel/audio/hasio.h"
#include "kernel/audio/pullmix.h"
#include "kernel/gdxf/gdxfint.h"

namespace hype::gdxf {

    namespace {

        constexpr hcpp::u32 ring_frames = 4096;

        struct GdxfStream {
            bool used = false;
            hcpp::u32 source_id = audio::pullmix_invalid_id;
            hcpp::u8 channels = 2;
            hcpp::u32 rate = 48000;
            hcpp::u32 head = 0;
            hcpp::u32 tail = 0;
            hcpp::i16 ring[ring_frames * 2];
        };

        GdxfStream streams[gdxf_max_audio_streams];

        void stream_fill(hcpp::i16* dst, hcpp::u32 frames, void* ctx) noexcept {
            auto* stream = static_cast<GdxfStream*>(ctx);
            const hcpp::u32 channels = stream->channels;
            const hcpp::u32 avail = stream->tail - stream->head;
            const hcpp::u32 take = avail < frames ? avail : frames;
            for (hcpp::u32 f = 0; f < take; ++f) {
                const hcpp::u32 index =
                    (stream->head + f) % ring_frames * channels;
                for (hcpp::u32 c = 0; c < channels; ++c) {
                    dst[f * channels + c] = stream->ring[index + c];
                }
            }
            stream->head += take;
            for (hcpp::u32 f = take; f < frames; ++f) {
                for (hcpp::u32 c = 0; c < channels; ++c) {
                    dst[f * channels + c] = 0;  // underrun -> silence
                }
            }
        }

        GdxfStream* stream_for(hcpp::u32 handle) noexcept {
            if (handle == gdxf_invalid_stream ||
                handle > gdxf_max_audio_streams) {
                return nullptr;
            }
            GdxfStream& stream = streams[handle - 1];
            return stream.used ? &stream : nullptr;
        }

    }  // namespace

    hcpp::u32 gdxf_audio_open(const AudioFormat& fmt,
                              hcpp::u32 period_frames) noexcept {
        // wrap hasio: the device stream is owned by the hasio service, so a
        // gdxf stream is a pullmix source draining a per-stream write ring
        if (!audio::hasio_ready() || fmt.channels == 0 || fmt.channels > 2 ||
            fmt.rate == 0 || period_frames < 128 || period_frames > 512) {
            return gdxf_invalid_stream;
        }
        for (hcpp::u32 i = 0; i < gdxf_max_audio_streams; ++i) {
            GdxfStream& stream = streams[i];
            if (stream.used) {
                continue;
            }
            stream = GdxfStream{};
            stream.used = true;
            stream.channels = fmt.channels;
            stream.rate = fmt.rate;
            stream.source_id = audio::pullmix_add_source(
                &stream_fill, &stream, fmt.channels, fmt.rate);
            if (stream.source_id == audio::pullmix_invalid_id) {
                stream.used = false;
                return gdxf_invalid_stream;
            }
            return i + 1;
        }
        return gdxf_invalid_stream;
    }

    bool gdxf_audio_write(hcpp::u32 stream, const hcpp::i16* frames,
                          hcpp::u32 count) noexcept {
        GdxfStream* target = stream_for(stream);
        if (target == nullptr || (frames == nullptr && count != 0)) {
            return false;
        }
        const hcpp::u32 channels = target->channels;
        const hcpp::u32 space = ring_frames - (target->tail - target->head);
        if (count > space) {
            return false;  // never partial: caller retries when drained
        }
        for (hcpp::u32 f = 0; f < count; ++f) {
            const hcpp::u32 index =
                (target->tail + f) % ring_frames * channels;
            for (hcpp::u32 c = 0; c < channels; ++c) {
                target->ring[index + c] = frames[f * channels + c];
            }
        }
        target->tail += count;
        return true;
    }

    hcpp::u32 gdxf_audio_queued_frames(hcpp::u32 stream) noexcept {
        const GdxfStream* target = stream_for(stream);
        return target != nullptr ? target->tail - target->head : 0;
    }

    void gdxf_audio_close(hcpp::u32 stream) noexcept {
        GdxfStream* target = stream_for(stream);
        if (target == nullptr) {
            return;
        }
        audio::pullmix_remove_source(target->source_id);
        *target = GdxfStream{};
    }

}  // namespace hype::gdxf
