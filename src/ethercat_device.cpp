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

#include "duatic_ethercat_interface/ethercat_device.hpp"
#include "duatic_ethercat_interface/ethercat_bus.hpp"

namespace duatic::ethercat_interface
{
EthercatDeviceBase::EthercatDeviceBase(const Hooks& hooks) : hooks_(hooks)
{
}
void EthercatDeviceBase::configure(EthercatBus* bus, DeviceInfo device_info)
{
  device_info_ = device_info;
  bus_ = bus;
  on_configure();
}

ObjectDictionary EthercatDeviceBase::read_od(bool full_read) const
{
  return bus_->read_od(get_device_id(), full_read);
}

void GenericEthercatDevice::update_write()
{
  bus_->write_rx_pdo(get_device_id(), rx_pdo_);
}
void GenericEthercatDevice::update_read()
{
  tx_pdo_ = bus_->read_tx_pdo(get_device_id());
};

}  // namespace duatic::ethercat_interface
