#include <Device/Protocol/DeviceInterface.hpp>
#include <Device/Protocol/DeviceSettings.hpp>
#include <ossia/network/generic/generic_device.hpp>
#include <ossia/network/base/protocol.hpp>
#include <ossia/network/local/local.hpp>
#include <catch2/catch_test_macros.hpp>
#include <wobjectimpl.h>

class InPlaceSettingsDevice final : public Device::OwningDeviceInterface
{
  W_OBJECT(InPlaceSettingsDevice)
public:
  explicit InPlaceSettingsDevice(Device::DeviceSettings settings)
      : OwningDeviceInterface{std::move(settings)}
  {
    m_dev = std::make_shared<ossia::net::generic_device>(
        std::make_unique<ossia::net::multiplex_protocol>(), "test");
  }
  bool reconnect() override { ++reconnects; return true; }
  void disconnect() override
  {
    ++disconnects;
    OwningDeviceInterface::disconnect();
  }
  bool applySettingsInPlace(const Device::DeviceSettings& settings)
  {
    ++attempts;
    if(!allowInPlace) return false;
    m_settings = settings;
    return true;
  }
  W_INVOKABLE(applySettingsInPlace)
  bool allowInPlace{true};
  int attempts{}, reconnects{}, disconnects{};
};
W_OBJECT_IMPL(InPlaceSettingsDevice)

TEST_CASE("Device settings can update without replacing a live device", "[device][settings]")
{
  Device::DeviceSettings original;
  original.name = "test";
  InPlaceSettingsDevice device{original};
  const auto* live = device.getDevice();
  auto changed = original;
  changed.deviceSpecificSettings = 42;
  Device::DeviceInterface& base = device;
  base.updateSettings(changed);
  CHECK(device.attempts == 1);
  CHECK(device.disconnects == 0);
  CHECK(device.reconnects == 0);
  CHECK(device.getDevice() == live);
  CHECK(device.settings().deviceSpecificSettings.toInt() == 42);
  // Undo takes exactly the same path, retaining the device the execution uses.
  base.updateSettings(original);
  CHECK(device.getDevice() == live);
  CHECK(device.disconnects == 0);
  CHECK(device.attempts == 2);
}

TEST_CASE("An in-place settings hook can require the normal reconnect path", "[device][settings]")
{
  Device::DeviceSettings settings;
  settings.name = "test";
  InPlaceSettingsDevice device{settings};
  device.allowInPlace = false;
  device.updateSettings(settings);
  CHECK(device.attempts == 1);
  CHECK(device.disconnects == 1);
  CHECK(device.reconnects == 1);
}
