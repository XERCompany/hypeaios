#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

namespace hype::host {

    enum class DeviceType : hcpp::u8 { Unknown, Display, Input, Storage, Network, Service };

    struct Device;
    using DriverProbe = bool (*)(void *context, Device &device) noexcept;
    using DriverRemove = void (*)(void *context, Device &device) noexcept;

    struct DeviceDriver {
        hcpp::u32 id;
        hcpp::StringView name;
        DeviceType type;
        DriverProbe probe;
        DriverRemove remove;
        void *context;
    };

    struct Device {
        hcpp::u32 id;
        hcpp::StringView name;
        DeviceType type;
        hcpp::u32 capabilities;
        Device *parent;
        DeviceDriver *driver;
        bool registered;
    };

    struct HostEndpoint {
        hcpp::u32 id;
        hcpp::StringView name;
        hcpp::u32 capabilities;
    };

    void host_initialize() noexcept;
    bool device_register(Device &device) noexcept;
    bool device_unregister(hcpp::u32 id) noexcept;
    Device *device_find(hcpp::u32 id) noexcept;
    bool driver_register(DeviceDriver &driver) noexcept;
    bool driver_unregister(hcpp::u32 id) noexcept;
    hcpp::size_t device_count() noexcept;
    hcpp::size_t driver_count() noexcept;
    bool host_register(const HostEndpoint &endpoint) noexcept;
    const HostEndpoint *host_find(hcpp::u32 id) noexcept;
    hcpp::size_t host_count() noexcept;
    const HostEndpoint *host_find_capability(hcpp::u32 capability) noexcept;

}  // namespace hype::host
