#pragma once

#include <soem_vendor/ethercat.h>

namespace duatic::ethercat_interface::internal::some
{
/**
 * @brief Simply wraps the some provided context into something more usable
 */
struct EthercatContext
{
  // EtherCAT context data elements:

  // Port reference.
  ecx_portt ecat_port{};
  // List of slave data. Index 0 is reserved for the master, higher indices for the slaves.
  ec_slavet ecatSlavelist_[EC_MAXSLAVE];
  // Number of slaves found in the network.
  int ecatSlavecount_ = 0;
  // Slave group structure.
  ec_groupt ecatGrouplist_[EC_MAXGROUP];
  // Internal, reference to EEPROM cache buffer.
  uint8 ecatEsiBuf_[EC_MAXEEPBUF];
  // Internal, reference to EEPROM cache map.
  uint32 ecatEsiMap_[EC_MAXEEPBITMAP];
  // Internal, reference to error list.
  ec_eringt ecatEList_{};
  // Internal, reference to processdata stack buffer info.
  ec_idxstackT ecatIdxStack_{};
  // Boolean indicating if an error is available in error stack.
  boolean ecatError_ = FALSE;
  // Reference to last DC time from slaves.
  int64 ecatDcTime_ = 0;
  // Internal, SM buffer.
  ec_SMcommtypet ecatSmCommtype_[EC_MAX_MAPT];
  // Internal, PDO assign list.
  ec_PDOassignt ecatPdoAssign_[EC_MAX_MAPT];
  // Internal, PDO description list.
  ec_PDOdesct ecatPdoDesc_[EC_MAX_MAPT];
  // Internal, SM list from EEPROM.
  ec_eepromSMt ecatSm_{};
  // Internal, FMMU list from EEPROM.
  ec_eepromFMMUt ecatFmmu_{};

  // EtherCAT context data.
  ecx_contextt context = { &ecat_port,
                           &ecatSlavelist_[0],
                           &ecatSlavecount_,
                           EC_MAXSLAVE,
                           &ecatGrouplist_[0],
                           EC_MAXGROUP,
                           &ecatEsiBuf_[0],
                           &ecatEsiMap_[0],
                           0,
                           &ecatEList_,
                           &ecatIdxStack_,
                           &ecatError_,
                           0,
                           0,
                           &ecatDcTime_,
                           &ecatSmCommtype_[0],
                           &ecatPdoAssign_[0],
                           &ecatPdoDesc_[0],
                           &ecatSm_,
                           &ecatFmmu_,
                           nullptr,
                           nullptr,
                           0 };
};
}  // namespace duatic::ethercat_interface::internal::some
