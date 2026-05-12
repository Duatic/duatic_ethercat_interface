cmake_minimum_required(VERSION 3.22)
project(duatic_ethercat_interface)

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(ament_cmake_ros REQUIRED)

set(PACKAGE_DEPENDENCIES
  soem_vendor
  #etherlab_master_vendor
  duatic_message_logger
)

foreach(Dependency IN ITEMS ${PACKAGE_DEPENDENCIES})
  find_package(${Dependency} REQUIRED)
endforeach()


add_library(duatic_ethercat_interface
  src/internal/soem/soem_backend.cpp
  src/device_context.cpp
)
add_library(duatic_ethercat_interface::duatic_ethercat_interface ALIAS duatic_ethercat_interface)
target_compile_features(duatic_ethercat_interface PUBLIC c_std_99 cxx_std_20)  # Require C99 and C++20
target_include_directories(duatic_ethercat_interface PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/${PROJECT_NAME}>)

ament_target_dependencies(duatic_ethercat_interface ${PACKAGE_DEPENDENCIES})
target_link_libraries(duatic_ethercat_interface duatic_message_logger::duatic_message_logger)
# enable additional warnings
target_compile_options(duatic_ethercat_interface PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wnon-virtual-dtor
  -Wconversion
  -Wundef
  -Wformat=2
  -Wsign-conversion
  -Wnull-dereference
  -Werror=return-type
)

# Causes the visibility macros to use dllexport rather than dllimport,
# which is appropriate when building the dll but not consuming it.
target_compile_definitions(duatic_ethercat_interface PRIVATE "duatic_ethercat_interface_BUILDING_LIBRARY")


# Tooling
find_package(cxxopts REQUIRED)
add_executable(bus_scanner ./src/tools/bus_scanner.cpp)
target_compile_features(bus_scanner PUBLIC c_std_99 cxx_std_20)
target_include_directories(bus_scanner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bus_scanner duatic_ethercat_interface cxxopts::cxxopts)



# Installation


install(
  DIRECTORY include/
  DESTINATION include/${PROJECT_NAME}
)

install(
  TARGETS duatic_ethercat_interface
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(
  TARGETS bus_scanner
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)

endif()

ament_export_include_directories(
  "include/${PROJECT_NAME}"
)
ament_export_libraries(
  duatic_ethercat_interface
)
ament_export_targets(
  export_${PROJECT_NAME}
)

ament_export_dependencies(${PACKAGE_DEPENDENCIES})



ament_package()
