#include "kernel/video/videodecoderlz.h"
namespace hype::video { DecodeResult lz_decode_frame(const hcpp::u8*src,hcpp::size_t n,VideoFrame&out)noexcept{hcpp::size_t i=0,o=0;while(i+1<n&&o<out.size){hcpp::u8 count=src[i++],v=src[i++];for(hcpp::u8 k=0;k<count&&o<out.size;++k)out.data[o++]=v;}return{o!=0,i,o,CodecType::Lz};} }
