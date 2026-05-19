#include "duatic_ethercat_interface/ethercat_interface.hpp"

/**
 * This is a minimal example which demonstrates how to use the api
 */

using namespace duatic::ethercat_interface;

struct RX
{
  int a;
};
struct TX
{
  int b;
};

class Drive
{
public:
  using Device = EthercatDevice<RX, TX>;
  using DevicePtr = std::shared_ptr<Device>;

  Drive() : device_(std::make_shared<Device>())
  {
  }
  auto& get_device()
  {
    return device_;
  }

  void do_something()
  {
  }
  int get_reading()
  {
    return 0;
  }
  void set_command(int cmd)
  {
  }

private:
  // Idea is to use composition instead of inheritence
  DevicePtr device_{};
};

Drive drive;
DeviceId device_id = 1;

int main(void)
{
  // This should happen in the same thread
  auto bus = std::make_shared<EthercatBus>(EthercatBus::Parameters{});
  bus->initialize();

  // API without device usage:
  std::array<uint8_t, 8> data;
  bus->read_sdo_untyped(data, 1, 1, 0);

  // Usage with device
  bus->attach_device(device_id, drive.get_device());

  bus->startup();
  bus->activate();

  // Executor pattern as we know it from ros (2nd thread)
  SingleThreadedExecutor executor;
  executor.add_bus(bus);
  executor.spin();

  // OR
  while (true) {
    bus->update();
  }
}

void second_thread()
{
  // Asynchronous sdo read/writes
  drive.do_something();

  // Or access readings
  drive.get_reading();

  // Or write commands
  drive.set_command(1);
}
