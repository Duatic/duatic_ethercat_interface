#pragma once

#include "duatic_ethercat_interface/types.hpp"

namespace duatic::ethercat_interface
{

// AL Status Code as defined by ETG.1000/ETG.1020 — the vendor-independent
// reason code a slave reports alongside its state (e.g. why it refused a
// state transition). 0x0000 conventionally means "no error".
using AlStatus = uint16_t;

// Decodes an AlStatus into a human-readable description via a static
// ETG.1020 lookup table. Unknown/vendor-specific codes should return a
// generic "unknown (0x....)" string rather than throwing.
std::string to_string(const AlStatus& status);

// Per-port health of a slave's ESC (EtherCAT Slave Controller). Sourced
// directly from ESC DL-layer registers (DL Status, RX Error Counters) —
// not from the slave's application layer.
struct ESCPortHealth
{
  // True if this physical port currently has an active link (cable
  // connected, PHY synced). Ports not physically wired on a given slave
  // will always read false.
  bool link_up{ false };

  // Cumulative count of frames received on this port that failed
  // EtherCAT frame validation (e.g. bad length/format), as reported by
  // the ESC's invalid-frame counter.
  uint32_t invalid_frames{ 0 };

  // Cumulative count of frames received on this port with a physical
  // layer/CRC error, as reported by the ESC's RX error counter. Distinct
  // from invalid_frames: this indicates link-quality issues (cabling,
  // EMI), whereas invalid_frames indicates protocol-level malformation.
  uint32_t rx_errors{ 0 };

  // Number of times the link on this port has dropped and re-established
  // since the master started monitoring this slave. Useful for catching
  // intermittent connections that a single link_up snapshot would miss.
  uint32_t lost_links{ 0 };
};

// Diagnostic status of a single slave's ESC. All fields except `online`
// are sourced from ESC registers (AL Status, AL Status Code, DL layer);
// `online` is inferred by the master, not read from the slave.
struct ESCStatus
{
  // The slave's position in the bus ring (auto-increment address order).
  // Used to correlate this entry with the slave's configuration.
  uint16_t position = 0;

  // Current EtherCAT state machine (ESM) state of this slave, as reported
  // in its AL Status register.
  EthercatDeviceState state = EthercatDeviceState::None;

  // AL Status Code accompanying `state` — explains *why* the slave is in
  // its current state, particularly relevant after a fault or a rejected
  // state transition request.
  AlStatus al_status{};

  // True if the master is currently receiving valid responses from this
  // slave (i.e. it responded and contributed to the working counter on
  // the last cycle). False if the slave is known/configured but not
  // currently answering (unplugged, powered off, dropped from the ring).
  bool online = false;

  // Per-port ESC health. Slaves expose up to 4 ports as defined by the
  // ESC register map, regardless of how many are physically wired;
  // unused ports simply report link_up == false.
  std::array<ESCPortHealth, 4> ports{};
};

// Bus-wide diagnostics, sourced from the master's own telemetry rather
// than from any single slave's ESC.
struct BusStatus
{
  // Physical link state of the master's own network interface. Distinct
  // from any slave's ESCPortHealth::link_up.
  bool link_up = false;

  // Cumulative number of EtherCAT frames sent by the master since
  // [master start / activation — confirm & document the reset point].
  uint64_t frames_sent = 0;

  // Cumulative number of frames for which no valid response was received
  // before timeout, as determined by the master's cyclic exchange.
  uint64_t frames_lost = 0;

  // Cumulative number of cycles where the returned working counter did
  // not match the expected value for the process data exchanged.
  uint64_t wkc_mismatches = 0;
};

// A consistent, point-in-time view of bus and slave diagnostics.
struct DiagnosticsSnapshot
{
  BusStatus bus;
  std::vector<ESCStatus> slaves;

  // Time this snapshot was captured, using a monotonic clock — suitable
  // for measuring elapsed time between snapshots, not wall-clock display.
  HighPrecisionTimeStamp timestamp;
};
}  // namespace duatic::ethercat_interface
