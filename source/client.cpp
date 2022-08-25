
#include "common.h"
#include <arpa/inet.h>
#include <chrono>
#include <fcntl.h>
#include <getopt.h>
#include <iostream>
#include <linux/if_tun.h>
#include <linux/ip.h>
#include <linux/route.h>
#include <netdb.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

std::string tun_ip = "10.0.0.100";
std::string tun_mask = "255.0.0.0";
std::string server_tun_ip = "10.0.0.1";
std::string server_adress = "";
unsigned short server_port = 22331;

void Usage() {
  std::cerr << "Options:" << std::endl;
  std::cerr << "\t--server-address IP: Server address" << std::endl;
  std::cerr << "\t--server-port PORT: Server listen port" << std::endl;
}

bool ParseCommandLine(int argc, char *argv[]) {
  std::vector<struct option> options;
  {
    struct option opt;
    opt.name = "server-address";
    opt.has_arg = required_argument;
    opt.flag = NULL;
    opt.val = 1;
    options.push_back(opt);
  }
  {
    struct option opt;
    opt.name = "server-port";
    opt.has_arg = required_argument;
    opt.flag = NULL;
    opt.val = 2;
    options.push_back(opt);
  }
  {
    struct option opt;
    memset(&opt, 0, sizeof(opt));
    options.push_back(opt);
  }

  int opt_ret;
  while ((opt_ret = getopt_long(argc, argv, "", options.data(), NULL)) != -1) {
    switch (opt_ret) {
    case 1:
      server_adress = optarg;
      break;
    case 2:
      server_port = std::stoi(optarg);
      break;
    case '?':
      return false;
    }
  }
  return true;
}

int main(int argc, char *argv[]) {
  if (!ParseCommandLine(argc, argv)) {
    Usage();
    return -1;
  }

  if (server_adress.empty() || tun_ip.empty()) {
    Usage();
    return -1;
  }

  // Get server ip by domain name
  std::string server_ip = DNS(server_adress);
  if (server_ip.empty()) {
    std::cerr << "Server ip parse failed." << std::endl;
    return -1;
  }

  std::string default_gateway_ip = GetDefaultGateway();
  if (default_gateway_ip.empty()) {
    std::cerr << "Get default gateway failed." << std::endl;
    return -1;
  }

  // Create tun interface
  auto fd_name = CreateTunInterface();
  int tun_fd = std::get<0>(fd_name);
  std::string dev_name = std::get<1>(fd_name);
  if (tun_fd == -1) {
    std::cerr << "Create tun interface failed." << std::endl;
    return -1;
  }

  // Add ip address
  if (!SetInterfaceUp(dev_name)) {
    std::cerr << "Set interface up failed." << std::endl;
    return -1;
  }
  if (!SetInterfaceAddress(dev_name, tun_ip)) {
    std::cerr << "Set interface address failed." << std::endl;
    return -1;
  }
  if (!SetInterfaceMask(dev_name, tun_mask)) {
    std::cerr << "Set interface mask failed." << std::endl;
    return -1;
  }

  // Add server route
  if (!AddRoute(server_ip, "255, 255, 255, 255", default_gateway_ip)) {
    std::cerr << "Add server route failed." << std::endl;
    return -1;
  }

  // Add default route
  if (!AddRoute("0.0.0.0", "0.0.0.0", "10.0.0.1")) {
    std::cerr << "Add default route failed." << std::endl;
    return -1;
  }

  int net_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (net_fd == -1) {
    std::cerr << "Socket failed." << std::endl;
    return -1;
  }
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(server_port);
  inet_aton(server_ip.c_str(), &server_addr.sin_addr);
  if (connect(net_fd, (sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    std::cerr << "Connect server failed. " << strerror(errno) << std::endl;
    return -1;
  }

  int epoll_fd = epoll_create(5);
  if (epoll_fd == -1) {
    std::cerr << "Epoll create failed." << std::endl;
    return -1;
  }

  struct epoll_event net_event;
  net_event.events = EPOLLIN;
  net_event.data.fd = net_fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, net_fd, &net_event) == -1) {
    std::cerr << "Epoll ctrl failed." << std::endl;
    return -1;
  }
  struct epoll_event tun_event;
  tun_event.events = EPOLLIN;
  tun_event.data.fd = tun_fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tun_fd, &tun_event) == -1) {
    std::cerr << "Epoll ctrl failed." << std::endl;
    return -1;
  }

  const int buffer_size = 2000;
  char buffer[buffer_size];
  while (true) {
    struct epoll_event event;
    int event_count = epoll_wait(epoll_fd, &event, 1, -1);
    struct timeval time;
    gettimeofday(&time, NULL);
    for (int idx = 0; idx < event_count; ++idx) {
      if (event.data.fd == tun_fd) {
        // Tun interface
        int read_len = read(tun_fd, buffer, buffer_size);
        if (read_len < 0) {
          std::cerr << "Read failed. " << strerror(errno) << std::endl;
          continue;
        }
        int send_len =
            sendto(net_fd, buffer, read_len, 0, (struct sockaddr *)&server_addr,
                   sizeof(server_addr));
        if (send_len < 0) {
          std::cerr << "Send failed. " << strerror(errno) << std::endl;
          continue;
        }
        printf("%ld.%ld TUN->NET %d.%d\n", time.tv_sec, time.tv_usec, read_len,
               send_len);
      } else {
        // Physical interface
        int read_len = recvfrom(net_fd, buffer, buffer_size, 0, NULL, NULL);
        if (read_len < 0) {
          std::cerr << "Recv failed. " << strerror(errno) << std::endl;
          continue;
        }
        int send_len = write(tun_fd, buffer, read_len);
        if (send_len < 0) {
          std::cerr << "Write failed. " << strerror(errno) << std::endl;
          continue;
        }
        printf("%ld.%ld NET->TUN %d.%d\n", time.tv_sec, time.tv_usec, read_len,
               send_len);
      }
    }
  }
  return 0;
}
