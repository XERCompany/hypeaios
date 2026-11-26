#include "kernel/video/videomcodec.h"
namespace hype::video { namespace{struct R{CodecType c;DecodeFn f;};R r[8];hcpp::size_t n;} bool mcodec_register(CodecType c,DecodeFn f)noexcept{if(!f||n>=8)return false;r[n++]={c,f};return true;} DecodeFn mcodec_find(CodecType c)noexcept{for(hcpp::size_t i=0;i<n;++i)if(r[i].c==c)return r[i].f;return nullptr;} hcpp::size_t mcodec_count()noexcept{return n;} }
