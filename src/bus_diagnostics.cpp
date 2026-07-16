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
#include "duatic_ethercat_interface/bus_diagnostics.hpp"

#include <sstream>
#include <iomanip>

namespace duatic::ethercat_interface
{
std::string to_string(const DiagnosticsSnapshot& snap)
{
  std::ostringstream os;

  os << "=== EtherCAT Diagnostics ===\n";
  os << "snapshot timestamp: " << snap.timestamp.time_since_epoch().count() << " ns (monotonic)\n";

  // Bus-wide
  os << "\n-- Bus --\n";
  os << "  master link:    " << (snap.bus.link_up ? "up" : "down") << "\n";
  os << "  frames sent:    " << snap.bus.frames_sent << "\n";
  os << "  frames lost:    " << snap.bus.frames_lost << "\n";
  os << "  wkc mismatches: " << snap.bus.wkc_mismatches << "\n";

  // Per-slave
  os << "\n-- Slaves (" << snap.slaves.size() << ") --\n";
  for (const auto& s : snap.slaves) {
    os << "  [" << std::setw(2) << s.position << "] " << std::setw(11) << std::left << to_string(s.state) << std::right
       << " | " << (s.online ? "online " : "OFFLINE") << " | AL 0x" << std::setfill('0') << std::setw(4) << std::hex
       << s.al_status << std::dec << std::setfill(' ') << " (" << to_string(s.al_status) << ")\n";

    os << "        ports (updated " << s.ports_update_timestamp.time_since_epoch().count() << " ns):\n";
    for (std::size_t p = 0; p < s.ports.size(); ++p) {
      const auto& port = s.ports[p];
      // Skip fully-quiet ports (down, no errors) to keep the output focused on wired ports.
      if (!port.link_up && port.invalid_frames == 0 && port.rx_errors == 0 && port.lost_links == 0) {
        continue;
      }
      os << "          port " << p << ": " << (port.link_up ? "up  " : "down") << "  invalid=" << port.invalid_frames
         << "  rx_err=" << port.rx_errors << "  lost_links=" << port.lost_links << "\n";
    }
  }

  // Executor (optional)
  if (snap.executor) {
    const auto& e = *snap.executor;
    os << "\n-- Executor --\n";
    os << "  spin thread:       " << (e.spin_thread_running ? "running" : "stopped") << "\n";
    os << "  missed rate steps: " << e.missed_rate_steps << "\n";
    os << "  accumulated delay: " << e.accumulated_delay.count() << " ns\n";
    os << "  avg update rate:   " << e.average_update_rate_Hz << " Hz\n";
    os << "  last update:       " << e.last_update_tp.time_since_epoch().count() << " ns (monotonic)\n";
  }

  return os.str();
}
std::ostream& operator<<(std::ostream& os, const DiagnosticsSnapshot& snap)
{
  return os << to_string(snap);
}
}  // namespace duatic::ethercat_interface
