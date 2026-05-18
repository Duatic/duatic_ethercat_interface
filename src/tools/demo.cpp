#include "duatic_ethercat_interface/ethercat_bus.hpp"
#include "duatic_ethercat_interface/ethercat_device.hpp"

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
  EthercatBus bus(EthercatBus::Parameters{});
  bus.initialize();

  bus.attach_device(device_id, drive.get_device());

  bus.startup();
  bus.activate();

  // Automatic setup
  bus.spin();

  // OR
  while (true) {
    bus.update();
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
