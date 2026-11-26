#pragma once
#include "hcpp/types.hpp"
#include "kernel/error.h"
namespace hype::kernel {

    enum class ObjectType : hcpp::u16 { None, Process, Thread, File, Device, Service, Memory, User };
    using ObjectHandle = hcpp::u32;
    constexpr ObjectHandle invalid_object = 0;

    struct ObjectInfo {
        ObjectType type;
        hcpp::u32 access;
        hcpp::u32 references;
        void *object;
    };

    void object_manager_initialize() noexcept;
    ObjectHandle object_create(ObjectType type, void *object, hcpp::u32 access) noexcept;
    Error object_reference(ObjectHandle handle) noexcept;
    Error object_close(ObjectHandle handle) noexcept;
    void *object_resolve(ObjectHandle handle, ObjectType type, hcpp::u32 required_access) noexcept;
    hcpp::size_t object_count() noexcept;
    bool object_info(ObjectHandle handle, ObjectInfo &output) noexcept;

}  // namespace hype::kernel
