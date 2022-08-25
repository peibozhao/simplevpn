
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <linux/if_tun.h>
#include <linux/route.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sstream>
#include <string.h>
#include <string>
#include <sys/ioctl.h>
#include <tuple>
#include <unistd.h>

std::tuple<int, std::string> CreateTunInterface() {
  const char *tun_dev = "/dev/net/tun";
  int tun_fd = open(tun_dev, O_RDWR);
  if (tun_fd < 0) {
    return {-1, ""};
  }
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
  int err = ioctl(tun_fd, TUNSETIFF, &ifr);
  if (err < 0) {
    close(tun_fd);
    return {-1, ""};
  }
  std::string dev_name = ifr.ifr_name;
  return {tun_fd, dev_name};
}

bool SetInterfaceUp(const std::string &dev_name) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  memcpy(ifr.ifr_name, dev_name.c_str(), dev_name.size());
  if (ioctl(sock, SIOCGIFFLAGS, &ifr) == -1) {
    return false;
  }
  if (!(ifr.ifr_flags & IFF_UP)) {
    ifr.ifr_flags |= IFF_UP;
    if (ioctl(sock, SIOCSIFFLAGS, &ifr) == -1) {
      close(sock);
      return false;
    }
  }
  close(sock);
  return true;
}

bool SetInterfaceAddress(const std::string &dev_name,
                         const std::string &ip_addr) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  memcpy(ifr.ifr_name, dev_name.c_str(), dev_name.size());
  struct sockaddr_in *addr = (struct sockaddr_in *)&ifr.ifr_addr;
  addr->sin_family = AF_INET;
  if (inet_aton(ip_addr.c_str(), &addr->sin_addr) == 0) {
    return false;
  }
  if (ioctl(sock, SIOCSIFADDR, &ifr) == -1) {
    close(sock);
    return false;
  }
  close(sock);
  return true;
}

bool SetInterfaceMask(const std::string &dev_name, const std::string &ip_mask) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  memcpy(ifr.ifr_name, dev_name.c_str(), dev_name.size());
  struct sockaddr_in *mask = (struct sockaddr_in *)&ifr.ifr_addr;
  mask->sin_family = AF_INET;
  if (inet_aton(ip_mask.c_str(), &mask->sin_addr) == 0) {
    close(sock);
    return false;
  }
  if (ioctl(sock, SIOCSIFNETMASK, &ifr) == -1) {
    close(sock);
    return false;
  }
  close(sock);
  return true;
}

bool AddRouteDirect(const std::string &dest_ip, const std::string &mask,
                    const std::string dev_name) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  struct rtentry rt;
  memset(&rt, 0, sizeof(rt));
  rt.rt_dev = (char *)dev_name.c_str();
  rt.rt_metric = 0;
  rt.rt_flags = RTF_UP;
  sockaddr_in *rt_dest_addr = (sockaddr_in *)&rt.rt_dst;
  rt_dest_addr->sin_family = AF_INET;
  rt_dest_addr->sin_addr.s_addr = inet_addr(dest_ip.c_str());
  sockaddr_in *rt_mask = (sockaddr_in *)&rt.rt_genmask;
  rt_mask->sin_family = AF_INET;
  rt_mask->sin_addr.s_addr = inet_addr(mask.c_str());
  if (ioctl(sock, SIOCADDRT, &rt) == -1) {
    close(sock);
    return false;
  }
  close(sock);
  return true;
}

bool AddRoute(const std::string &dest_ip, const std::string &mask,
              const std::string &gateway) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  struct rtentry rt;
  memset(&rt, 0, sizeof(rt));
  rt.rt_metric = 0;
  rt.rt_flags = RTF_UP | RTF_GATEWAY;
  sockaddr_in *rt_dest_addr = (sockaddr_in *)&rt.rt_dst;
  rt_dest_addr->sin_family = AF_INET;
  rt_dest_addr->sin_addr.s_addr = inet_addr(dest_ip.c_str());
  sockaddr_in *rt_mask = (sockaddr_in *)&rt.rt_genmask;
  rt_mask->sin_family = AF_INET;
  rt_mask->sin_addr.s_addr = inet_addr(mask.c_str());
  sockaddr_in *rt_gateway = (sockaddr_in *)&rt.rt_gateway;
  rt_gateway->sin_family = AF_INET;
  rt_gateway->sin_addr.s_addr = inet_addr(gateway.c_str());
  if (ioctl(sock, SIOCADDRT, &rt) == -1 && errno != EEXIST) {
    close(sock);
    return false;
  }
  close(sock);
  return true;
}

std::string DNS(const std::string domain) {
  struct hostent *host = gethostbyname(domain.c_str());
  if (host == NULL) {
    return "";
  }
  return inet_ntoa(*(struct in_addr *)host->h_addr_list[0]);
}

// 0000FEA9 -> 169.254.0.0
static std::string ConvertIpFormat(const std::string &hex_ip) {
  std::string ret;
  for (int idx = hex_ip.length() - 2; idx >= 0; idx -= 2) {
    int hex_num;
    std::stringstream ss;
    auto tmp = hex_ip.substr(idx, 2);
    ss << tmp;
    ss >> std::hex >> hex_num;
    ret += std::to_string(hex_num);

    if (idx != 0) {
      ret += '.';
    }
  }
  return ret;
}

std::string GetDefaultGateway() {
  static const std::string route_fname = "/proc/net/route";
  std::ifstream route_stream;
  route_stream.open(route_fname);
  for (std::string line; getline(route_stream, line);) {
    std::string interface, dest, gateway;
    std::stringstream line_stream(line);
    getline(line_stream, interface, '\t');
    if (interface == "Iface") {
      // Header line
      continue;
    }
    getline(line_stream, dest, '\t');
    if (dest != "00000000") {
      continue;
    }
    getline(line_stream, gateway, '\t');
    return ConvertIpFormat(gateway);
  }
  return "";
}

bool EnableForward() {
  static const std::string forward_fname = "/proc/sys/net/ipv4/ip_forward";
  std::fstream forward_file(forward_fname);
  if (!forward_file.is_open()) {
    return false;
  }
  int forward = 0;
  forward_file >> forward;
  if (forward) {
    return true;
  }
  forward_file.seekg(std::ios::beg);
  forward_file << "1";
  return true;
}
