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
#include "duatic_ethercat_interface/bus_diagnostics.hpp"

namespace duatic::ethercat_interface
{

struct ExecutorParameters
{
  // priority and desired cpu core for the update thread
  int realtime_priority{ 60 };
  int desired_cpu_core{ -1 };
};

/**
 * @brief SingleBusExecutor - an executor which performs the rate limited update on one bus
 * objects
 */
class SingleBusExecutor
{
public:
  explicit SingleBusExecutor(std::shared_ptr<EthercatBus>& bus, const ExecutorParameters& params = ExecutorParameters{})
    : bus_(bus), params_(params), update_rate_(bus->get_parameters().dc_cycle_time), logger_(logging::get_logger_with_default_sink("SingleBusExecutor"))
  {
  }
  ~SingleBusExecutor()
  {
    stop();
  }
  /**
   * @brief spin - Start to spin (call update) on all given bus instances
   * @note this will shutdown the bus instances at exit
   */
  void spin()
  {
    if (spinning_) {
      throw ExecutorError("Executor already spinning");
    }
    logging::info(logger_) << "Setting up Executor with update frequency: " << 1.0/std::chrono::duration_cast<std::chrono::duration<double>>(bus_->get_parameters().dc_cycle_time).count() << "Hz" << std::endl;

    spinning_ = true;
    update_thread_ = std::jthread([&](std::stop_token stoken) {
      if (!set_realtime_priority(params_.realtime_priority, params_.desired_cpu_core)) {
        logging::error("Failed to set realtime priority of spin thread - run as root or configure security.limits");
      }
      while (!stoken.stop_requested()) {
        // In case the distributed clock is enabled we can synchronize our clock accordingly
        // The bus reports an offset we then feed into the PrecisionUpdateRate
        // In case dc is disabled std::nullopt is returned
        const auto dc_correction_offset = bus_->update();

        if (!this->update_rate_.step(dc_correction_offset.value_or(std::chrono::nanoseconds{ 0 }))) {
          logging::warning(logger_) << "Could not keep update rate: " << this->update_rate_.last_delay_ns()
                             << std::endl;
        }
      }

      bus_->shutdown();
    });
  }
  /**
   * @brief stop - Stop spinning the given bus instances
   */
  void stop()
  {
    if (spinning_ && update_thread_.joinable()) {
      update_thread_.request_stop();
      update_thread_.join();
    }
    spinning_ = false;
  }

  /**
   * @brief full_diagnostics - obtain a full diagnostics snapshot of the ethercat bus and icnlude executor diagnostics
   * @note see @ref EthercatBus::diagnostics for parameter meaning
   */
  DiagnosticsSnapshot full_diagnostics(bool force_update = false)
  {
    auto snapshot = bus_->diagnostics(force_update);
    snapshot.executor = ExecutionStatus{ .spin_thread_running = spinning_,
                                         .missed_rate_steps = this->update_rate_.overrun_count(),
                                         .accumulated_delay = this->update_rate_.accumulated_delay_ns() };
    return snapshot;
  }

private:
logging::Logger logger_;
  std::shared_ptr<EthercatBus> bus_;
  const ExecutorParameters params_;
  std::jthread update_thread_;
  PrecisionUpdateRate update_rate_;
  bool spinning_{ false };
};
}  // namespace duatic::ethercat_interface
