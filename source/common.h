
#include <string>
#include <tuple>

std::tuple<int, std::string> CreateTunInterface();

bool SetInterfaceUp(const std::string &dev_name);

bool SetInterfaceAddress(const std::string &dev_name,
                         const std::string &ip_addr);

bool SetInterfaceMask(const std::string &dev_name, const std::string &ip_mask);

// Direct connect
bool AddRouteDirect(const std::string &dest_ip, const std::string &mask,
                    const std::string dev_name);

// Connect via gateway
bool AddRoute(const std::string &dest_ip, const std::string &mask,
              const std::string &gateway);

std::string DNS(const std::string domain);

std::string GetDefaultGateway();

bool EnableForward();
