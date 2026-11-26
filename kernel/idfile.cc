#include "kernel/idfile.h"
namespace hype::fs { bool file_id_valid(const FileId& id) noexcept{return id.volume!=0&&id.inode!=0;} bool file_id_equals(const FileId& left,const FileId& right) noexcept{return left.volume==right.volume&&left.inode==right.inode&&left.generation==right.generation;} }
