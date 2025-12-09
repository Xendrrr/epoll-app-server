#include <iostream>

#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <cstring>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>
#include <format>

int total_clients = 0;
int curr_clients = 0;
bool server_is_running = true;

std::string curr_time() {
  auto now = std::chrono::system_clock::now();
  return std::format("{:%Y-%m-%d %H:%M:%S}", now);
}

void mk_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// функции, относящиеся к TCP части сервака
int tcp_start_server(int port) {
  int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);

  int option = 1;
  setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);

  bind(tcp_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  listen(tcp_fd, 52);

  mk_nonblocking(tcp_fd);

  return tcp_fd;
}

void tcp_answer_command(int fd, std::string_view message) {
  std::string output;
  if (message.starts_with("/time")) {
    output = curr_time() + "\n";
  } else if (message.starts_with("/stats")) {
    output = "total clients = " + std::to_string(total_clients) + ", curr clients = " + std::to_string(curr_clients) + "\n";
  } else if (message.starts_with("/shutdown")) {
    output = "Passengers, Titanic is sinking, glhf\n";
    server_is_running = false;
  } else {
    output = "wtf_this_was_not_intended\n";
  }
  send(fd, output.c_str(), output.size(), 0);
}

void tcp_handle_client(int fd) {
  char buffer[5252];
  std::memset(buffer, 0, sizeof(buffer));

  ssize_t n = recv(fd, buffer, sizeof(buffer) - 1, 0);
  if (n <= 0) {
    --curr_clients;
    close(fd);
    return;
  }
  buffer[n] = '\0';
  std::string_view msg(buffer);
  if (msg.empty()) {
    return;
  }
  if (msg[0] == '/') {
    tcp_answer_command(fd, msg);
  } else {
    send(fd, buffer, msg.size(), 0);
  }
}


// функции, относящиеся к UDP части сервака

int udp_start_server(int port) {
  int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
  int option = 1;
  setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);

  bind(udp_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  mk_nonblocking(udp_fd);

  return udp_fd;
}

void udp_answer_command(int udp_fd, const sockaddr_in& client, socklen_t client_len, std::string_view message) {
  std::string output;
  if (message.starts_with("/time")) {
    output = curr_time() + "\n";
  } else if (message.starts_with("/stats")) {
    output = "total clients = " + std::to_string(total_clients) + ", curr clients = " + std::to_string(curr_clients) + "\n";
  } else if (message.starts_with("/shutdown")) {
    output = "Passengers, Titanic is sinking, glhf\n";
    server_is_running = false;
  } else {
    output = "wtf_this_was_not_intended\n";
  }
  sendto(udp_fd, output.c_str(), output.size(), 0, reinterpret_cast<const sockaddr*>(&client), client_len);
}

void udp_handle_client(int fd) {
  char buffer[5252];
  std::memset(buffer, 0, sizeof(buffer));
  sockaddr_in client{};
  socklen_t client_len = sizeof(client);
  ssize_t n = recvfrom(fd, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<sockaddr*>(&client), &client_len);
  if (n <= 0) {
    return;
  }
  buffer[n] = '\0';
  std::string_view msg(buffer);
  if (msg.empty()) {
    return;
  }
  if (msg[0] == '/') {
    udp_answer_command(fd, client, client_len, msg);
  } else {
    sendto(fd, buffer, msg.size(), 0, reinterpret_cast<sockaddr*>(&client), client_len);
  }
}

int main(int argc, char* argv[]) {
  int port = argc > 1 ? std::stoi(argv[1]) : 5252;
  int epoll_fd = epoll_create1(0);


  std::cout << "Trying to start TCP server...\n";
  int tcp_fd = tcp_start_server(port);
  std::cout << "Trying to start UDP server...\n";
  int udp_fd = udp_start_server(port);

  epoll_event event{};
  event.events = EPOLLIN;
  event.data.fd = tcp_fd;

  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tcp_fd, &event);
  std::cout << "TCP server successfully started\n";

  epoll_event event_2{};
  event_2.events = EPOLLIN;
  event_2.data.fd = udp_fd;
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, udp_fd, &event_2);

  std::cout << "UDP server successfully started\n";
  int max_number_of_events = 52;
  std::vector<epoll_event> events(max_number_of_events);

  while (server_is_running) {
    int n = epoll_wait(epoll_fd, events.data(), static_cast<int>(events.size()), -1);

    for (int i = 0; i < n; ++i) {
      int fd = events[i].data.fd;
      uint64_t event_in_cycle = events[i].events;
      if (fd == tcp_fd) {
        while (true) {
          sockaddr_in client_address{};
          socklen_t client_len = sizeof(client_address);
          int client_fd = accept(fd, reinterpret_cast<sockaddr*>(&client_address), &client_len);

          if (client_fd < 0) {
            break;
          }
          mk_nonblocking(client_fd);

          epoll_event client_event{};
          client_event.events = EPOLLIN;
          client_event.data.fd = client_fd;

          epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event);
          ++total_clients;
          ++curr_clients;
        }
      } else if (fd == udp_fd) {
        udp_handle_client(fd);
      } else {
        if (event_in_cycle & EPOLLHUP) {
          close(fd);
          --curr_clients;
        } else {
          tcp_handle_client(fd);
        }
      }
    }
  }
  close(tcp_fd);
  close(udp_fd);
  close(epoll_fd);
  std::cout << "Titanic has successfully sunk my friends\n";
}
