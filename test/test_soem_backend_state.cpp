#include <array>
#include <span>

#include <gtest/gtest.h>

#include "duatic_ethercat_interface/internal/soem/soem_backend_state.hpp"

using namespace duatic::ethercat_interface;                  // NOLINT(build/namespaces)
using namespace duatic::ethercat_interface::internal::soem;  // NOLINT(build/namespaces)

TEST(SoemBackendStateTest, AttachIsOnlyAllowedAfterInitialization)
{
  EXPECT_FALSE(can_attach_device(BusState::PreInit));
  EXPECT_TRUE(can_attach_device(BusState::Initialized));
  EXPECT_FALSE(can_attach_device(BusState::Configured));
  EXPECT_FALSE(can_attach_device(BusState::Activated));
  EXPECT_FALSE(can_attach_device(BusState::Operational));
  EXPECT_FALSE(can_attach_device(BusState::Shutdown));
}

TEST(SoemBackendStateTest, DeviceIdsFollowSoemSlaveAddressing)
{
  EXPECT_FALSE(has_device_on_bus(0, 3));
  EXPECT_TRUE(has_device_on_bus(1, 3));
  EXPECT_TRUE(has_device_on_bus(2, 3));
  EXPECT_TRUE(has_device_on_bus(3, 3));
  EXPECT_FALSE(has_device_on_bus(4, 3));
  EXPECT_FALSE(has_device_on_bus(1, 0));
}

TEST(SoemBackendStateTest, ManagedDeviceLookupReturnsTrueOnlyForExistingIds)
{
  const std::array<DeviceId, 3> devices{ 1, 3, 7 };
  const std::span<const DeviceId> device_ids(devices);

  EXPECT_TRUE(has_managed_device(device_ids, 1));
  EXPECT_TRUE(has_managed_device(device_ids, 3));
  EXPECT_TRUE(has_managed_device(device_ids, 7));
  EXPECT_FALSE(has_managed_device(device_ids, 0));
  EXPECT_FALSE(has_managed_device(device_ids, 2));
}

TEST(SoemBackendStateTest, QueuedSdoReadRequiresPositiveWorkingCounterAndExpectedSize)
{
  EXPECT_EQ(queued_sdo_read_direction(), SDOTransferDirection::Read);

  EXPECT_TRUE(queued_sdo_read_success(1, 4, 4));
  EXPECT_FALSE(queued_sdo_read_success(0, 4, 4));
  EXPECT_FALSE(queued_sdo_read_success(-1, 4, 4));
  EXPECT_FALSE(queued_sdo_read_success(1, 3, 4));
  EXPECT_FALSE(queued_sdo_read_success(1, 5, 4));
}

TEST(SoemBackendStateTest, QueuedSdoWriteRequiresPositiveWorkingCounter)
{
  EXPECT_EQ(queued_sdo_write_direction(), SDOTransferDirection::Write);

  EXPECT_TRUE(queued_sdo_write_success(1));
  EXPECT_FALSE(queued_sdo_write_success(0));
  EXPECT_FALSE(queued_sdo_write_success(-1));
}

TEST(SoemBackendStateTest, ShutdownViaPreopCoversConfiguredAndRuntimeStates)
{
  EXPECT_FALSE(should_shutdown_via_preop(BusState::PreInit));
  EXPECT_FALSE(should_shutdown_via_preop(BusState::Initialized));
  EXPECT_TRUE(should_shutdown_via_preop(BusState::Configured));
  EXPECT_TRUE(should_shutdown_via_preop(BusState::Activated));
  EXPECT_TRUE(should_shutdown_via_preop(BusState::Operational));
  EXPECT_FALSE(should_shutdown_via_preop(BusState::Shutdown));
}
