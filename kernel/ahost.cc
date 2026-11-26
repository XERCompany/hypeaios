#include "kernel/ahost.h"

namespace hype::host {
namespace {
constexpr hcpp::size_t max_devices = 16, max_drivers = 8, max_endpoints = 8;
Device *devices[max_devices];
DeviceDriver *drivers[max_drivers];
HostEndpoint endpoints[max_endpoints];
hcpp::size_t devices_count, drivers_count, endpoints_count;
bool type_matches(const DeviceDriver &d, const Device &dev) noexcept {
  return d.type == DeviceType::Unknown || d.type == dev.type;
}
void bind_driver(DeviceDriver &driver, Device &device) noexcept {
  if (driver.probe != nullptr && driver.probe(driver.context, device))
    device.driver = &driver;
}
} // namespace
void host_initialize() noexcept {
  devices_count = drivers_count = endpoints_count = 0;
  for (auto &d : devices)
    d = nullptr;
  for (auto &d : drivers)
    d = nullptr;
  for (auto &e : endpoints)
    e = {};
}
bool device_register(Device &device) noexcept {
  if (device.id == 0 || devices_count >= max_devices ||
      device_find(device.id) != nullptr)
    return false;
  device.registered = true;
  devices[devices_count++] = &device;
  for (hcpp::size_t i = 0; i < drivers_count; ++i)
    if (type_matches(*drivers[i], device))
      bind_driver(*drivers[i], device);
  return true;
}
bool device_unregister(hcpp::u32 id) noexcept {
  for (hcpp::size_t i = 0; i < devices_count; ++i)
    if (devices[i]->id == id) {
      if (devices[i]->driver != nullptr &&
          devices[i]->driver->remove != nullptr)
        devices[i]->driver->remove(devices[i]->driver->context, *devices[i]);
      devices[i]->registered = false;
      devices[i] = devices[devices_count - 1];
      --devices_count;
      return true;
    }
  return false;
}
Device *device_find(hcpp::u32 id) noexcept {
  for (hcpp::size_t i = 0; i < devices_count; ++i)
    if (devices[i]->id == id)
      return devices[i];
  return nullptr;
}
bool driver_register(DeviceDriver &driver) noexcept {
  if (driver.id == 0 || drivers_count >= max_drivers)
    return false;
  drivers[drivers_count++] = &driver;
  for (hcpp::size_t i = 0; i < devices_count; ++i)
    if (type_matches(driver, *devices[i]))
      bind_driver(driver, *devices[i]);
  return true;
}
bool driver_unregister(hcpp::u32 id) noexcept {
  for (hcpp::size_t i = 0; i < drivers_count; ++i)
    if (drivers[i]->id == id) {
      for (hcpp::size_t j = 0; j < devices_count; ++j)
        if (devices[j]->driver == drivers[i])
          devices[j]->driver = nullptr;
      drivers[i] = drivers[drivers_count - 1];
      --drivers_count;
      return true;
    }
  return false;
}
hcpp::size_t device_count() noexcept { return devices_count; }
hcpp::size_t driver_count() noexcept { return drivers_count; }
bool host_register(const HostEndpoint &endpoint) noexcept {
  if (endpoint.id == 0 || endpoints_count >= max_endpoints)
    return false;
  endpoints[endpoints_count++] = endpoint;
  return true;
}
const HostEndpoint *host_find(hcpp::u32 id) noexcept {
  for (hcpp::size_t i = 0; i < endpoints_count; ++i)
    if (endpoints[i].id == id)
      return &endpoints[i];
  return nullptr;
}
hcpp::size_t host_count() noexcept { return endpoints_count; }
const HostEndpoint *host_find_capability(hcpp::u32 capability) noexcept {
  for (hcpp::size_t i = 0; i < endpoints_count; ++i)
    if ((endpoints[i].capabilities & capability) != 0)
      return &endpoints[i];
  return nullptr;
}
} // namespace hype::host
