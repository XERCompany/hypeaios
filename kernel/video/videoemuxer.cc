#include "kernel/video/videoemuxer.h"
#include "kernel/video/videomp2.h"
namespace hype::video { bool emuxer_probe_packet(const StreamPacket&p)noexcept{return p.data&&p.size;} CodecType emuxer_codec(const StreamPacket&p)noexcept{return mp2_packet_start(p.data,p.size)?CodecType::Mpeg2:CodecType::Unknown;} }
