#include <iostream>
#include <array>
#include <chrono>
#include <thread>
#include <cxxopts.hpp>

#include <duatic_ethercat_interface/ethercat_bus.hpp>

using namespace duatic::ethercat_interface;

void handle_list_interfaces()
{
  std::cout << "Available buses:" << std::endl;

  const auto interfaces = EthercatBus::list_interfaces();

  for (const auto& interface : interfaces) {
    std::cout << "  " << interface << std::endl;
  }
}
void handle_scan(const std::string& interface)
{
  EthercatBus bus(EthercatBus::Parameters{ .interface = interface });
  std::cout << "scanning bus: " << interface << " for devices" << std::endl;

  const auto device_count = bus.initialize();
  std::cout << "found: " << device_count << " devices on the bus" << std::endl;

  const auto devices = bus.scan();

  for (const auto& info : devices) {
    std::cout << info << std::endl;
  }
}
void handle_sdo(const std::string& interface)
{
  EthercatBus bus(EthercatBus::Parameters{ .interface = interface });

  const auto device_count = bus.initialize();
  std::cout << "found: " << device_count << " devices on the bus" << std::endl;

  const auto devices = bus.scan();

  for (const auto& info : devices) {
    const auto od = bus.read_od(info.id, true);

    std::cout << info << std::endl;
    for (const auto& [index, sdo] : od.entries()) {
      std::cout << "  [" << std::hex << "0x" << sdo.index << std::dec << "]" << std::endl;
      std::cout << "   name: " << sdo.name << std::endl;

      std::cout << "   object type: " << sdo.obj_type << std::endl;
      if(sdo.sub_entries.empty()){
            std::cout << "   data type: " << sdo.data_type << std::endl;
      }

      for (const auto& sub : sdo.sub_entries) {
        std::cout << "     sub index: " << static_cast<int>(sub.index) << std::endl;
        std::cout << "     name: " << sub.name << std::endl;
        std::cout << "     data_type: " << sub.data_type << std::endl;
      }
    }
  }
}

int main(int argc, char** argv)
{
  cxxopts::Options options("duadrive_scanner", "Find and identify ");

  // clang-format off
    options.add_options()
        ("verb", "Actions to perform [scan, list_interfaces, sdo]", cxxopts::value<std::string>())
        ("b,bus", "Ethercat Bus", cxxopts::value<std::string>()->default_value("eth0"))
        ("h,help", "Print usage");
  // clang-format on

  options.parse_positional({ "verb" });
  options.show_positional_help();
  options.set_width(200);

  const auto args = options.parse(argc, argv);

  // Check if help was passed or no verb was passed
  if (args.count("help") || !args.count("verb")) {
    std::cout << options.help() << std::endl;
    return 0;
  }

  const auto verb = args["verb"].as<std::string>();
  // Check if the right verb was passed
  if (verb != "scan" && verb != "list_interfaces" && verb != "sdo") {
    std::cout << "Please pass a valid action" << std::endl;
    std::cout << options.help() << std::endl;
    return -1;
  }

  try {
    if (verb == "list_interfaces") {
      handle_list_interfaces();
      return 0;
    }
    const auto bus = args["bus"].as<std::string>();

    if (verb == "scan") {
      handle_scan(bus);
      return 0;
    }

    if (verb == "sdo") {
      handle_sdo(bus);
      return 0;
    }
  } catch (const std::exception& ex) {
    std::cerr << "Error: " << ex.what() << std::endl;
    return -2;
  }

  return 0;
}
