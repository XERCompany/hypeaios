#include "kernel/video/videomp2.h"
namespace hype::video { bool mp2_packet_start(const hcpp::u8*p,hcpp::size_t n)noexcept{return n>=4&&p[0]==0&&p[1]==0&&p[2]==1;} DecodeResult mp2_probe(const hcpp::u8*p,hcpp::size_t n)noexcept{return{mp2_packet_start(p,n),0,0,CodecType::Mpeg2};} }
