#pragma once
#include <thread>

#include "duatic_ethercat_interface/precision_update_rate.hpp"
#include "duatic_ethercat_interface/realtime_utils.hpp"
#include "duatic_ethercat_interface/ethercat_bus.hpp"

namespace duatic::ethercat_interface
{

struct ExecutorParameters
{
  // Update step rate in nanoseconds (default 1kHz)
  std::chrono::nanoseconds update_rate{ 1000000 };

  // priority and desired cpu core for the update thread
  int realtime_priority{ 60 };
  int desired_cpu_core{ -1 };
};
class SingleThreadedExecutor
{
public:
  explicit SingleThreadedExecutor(const ExecutorParameters& params = ExecutorParameters{})
    : params_(params), update_rate_(params.update_rate)
  {
  }
  void add_bus(std::shared_ptr<EthercatBus> bus)
  {
    busses_.push_back(bus);
  }
  void spin()
  {
    update_thread_ = std::jthread([&](std::stop_token stoken) {
      if (!set_realtime_priority(params_.realtime_priority, params_.desired_cpu_core)) {
        logging::error("Failed to set realtime priority of spin thread - run a root or configure security.limits");
      }
      while (!stoken.stop_requested()) {
        for (auto& bus : busses_) {
          bus->update();
        }
        if (!this->update_rate_.step()) {
          logging::warning() << "Could not keep update rate: " << this->update_rate_.accumulated_delay_ns()
                             << std::endl;
        }
      }

      for (auto& bus : busses_) {
        bus->shutdown();
      }
    });
  }

private:
  const ExecutorParameters params_;
  std::vector<std::shared_ptr<EthercatBus>> busses_;
  std::jthread update_thread_;
  PrecisionUpdateRate update_rate_;
};
}  // namespace duatic::ethercat_interface
