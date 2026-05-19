/*
 * Copyright 2026 Duatic AG
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
 * following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
 * disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
 * following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote
 * products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once
#include <thread>
#include <vector>
#include <memory>

#include "duatic_ethercat_interface/precision_update_rate.hpp"
#include "duatic_ethercat_interface/exceptions.hpp"
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

/**
 * @brief SingleThreadedExecutor - an executor which performs the rate limited update of one or more ethercat bus
 * objects
 * @note All busses are spun with the same update rate
 */
class SingleThreadedExecutor
{
public:
  explicit SingleThreadedExecutor(const ExecutorParameters& params = ExecutorParameters{})
    : params_(params), update_rate_(params.update_rate)
  {
  }
  /**
   * @brief add_bus - Add a bus object to the list of busses which is handled by this exector
   * @note After calling spin you cannot add additional bus objects anymore
   */
  void add_bus(std::shared_ptr<EthercatBus>& bus)
  {
    if (spinning_) {
      throw ExecutorError("Executor already spinning - cannot add additional bus");
    }
    busses_.push_back(bus);
  }
  /**
   * @brief spin - Start to spin (call update) on all given bus instances
   * @note this will shutdown the bus instances at exit
   */
  void spin()
  {
    spinning_ = true;
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
  /**
   * @brief stop - Stop spinning the given bus instances
   */
  void stop()
  {
    if (spinning_) {
      update_thread_.request_stop();
      update_thread_.join();
    }
    spinning_ = false;
  }

private:
  const ExecutorParameters params_;
  std::vector<std::shared_ptr<EthercatBus>> busses_;
  std::jthread update_thread_;
  PrecisionUpdateRate update_rate_;
  bool spinning_{ false };
};
}  // namespace duatic::ethercat_interface
