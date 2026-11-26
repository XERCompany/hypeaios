#include "kernel/immtbz.h"
#include "hcpp/memory.hpp"
namespace hype::immt {
void buffer_zero(BoundedBuffer& buffer) noexcept { if(buffer.data!=nullptr) hcpp::set(buffer.data,0,buffer.capacity); buffer.size=0; }
bool buffer_write(BoundedBuffer& buffer,const void* source,hcpp::size_t size) noexcept { if(buffer.data==nullptr||source==nullptr||size>buffer.capacity)return false; hcpp::copy(buffer.data,source,size); buffer.size=size; return true; }
} // namespace hype::immt
