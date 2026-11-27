#include "kernel/video/videodecoderpcie.h"
namespace hype::video {
    namespace {
        DecodeJob job_queue[decodejob_queue_depth];
        bool job_slot_free(const DecodeJob& j) noexcept {
            return j.status == DecodeJobStatus::Free || j.status == DecodeJobStatus::Done ||
                   j.status == DecodeJobStatus::Failed ||
                   j.status == DecodeJobStatus::Unsupported;
        }
        void job_run(DecodeJob& j) noexcept {
            if ((j.flags & decodejob_flag_hw) != 0) {
                j.status = DecodeJobStatus::Unsupported;
                j.result = {false, j.in_len, 0, j.codec_id};
                return;
            }
            if (j.out_frame == nullptr || j.in_buf == nullptr || j.in_len == 0) {
                j.status = DecodeJobStatus::Failed;
                j.result = {false, 0, 0, j.codec_id};
                return;
            }
            j.status = DecodeJobStatus::Running;
            j.result = video_decode(j.codec_id, j.in_buf, j.in_len, *j.out_frame);
            j.status = j.result.ok ? DecodeJobStatus::Done : DecodeJobStatus::Failed;
        }
    }
    void decodejob_reset() noexcept {
        for (hcpp::size_t i = 0; i < decodejob_queue_depth; ++i)
            job_queue[i].status = DecodeJobStatus::Free;
    }
    bool decodejob_submit(DecodeJob& j) noexcept {
        for (hcpp::size_t i = 0; i < decodejob_queue_depth; ++i) {
            if (!job_slot_free(job_queue[i])) continue;
            job_queue[i] = j;
            job_queue[i].status = DecodeJobStatus::Queued;
            job_run(job_queue[i]);
            j = job_queue[i];
            return true;
        }
        return false;
    }
    hcpp::size_t decodejob_pending() noexcept {
        hcpp::size_t count = 0;
        for (hcpp::size_t i = 0; i < decodejob_queue_depth; ++i)
            if (job_queue[i].status == DecodeJobStatus::Queued ||
                job_queue[i].status == DecodeJobStatus::Running)
                ++count;
        return count;
    }
    bool pcie_decoder_available() noexcept { return false; }
    DecodeResult pcie_decode_frame(const hcpp::u8* src, hcpp::size_t n, VideoFrame& out) noexcept {
        DecodeJob j{CodecType::Hardware, src, n, &out, decodejob_flag_hw,
                    DecodeJobStatus::Free, {}};
        decodejob_submit(j);
        return j.result;
    }
}  // namespace hype::video
