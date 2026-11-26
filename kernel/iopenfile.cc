#include "kernel/iopenfile.h"
namespace hype::fs { void open_file_reset(OpenFile& file) noexcept {file={};} bool open_file_valid(const OpenFile& file) noexcept{return file.open&&file_id_valid(file.id);} }
