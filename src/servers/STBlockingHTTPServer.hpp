#ifndef STBlockingHTTPServer_HPP
#define STBlockingHTTPServer_HTTP

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <sstream>

class STBlockingHTTPServer {
 private:
  int port;
  std::string html_path;
  int server_fd;
  int max_conn_queue;

  std::string parse_http_path(const std::string& request);
  std::string get_content_type(const std::string& path);
  void send_404(int client_fd);
  void send_500(int client_fd);
  void serve_file(int client_fd, std::string& path);
  void handle_client(int client_fd);

 public:
  STBlockingHTTPServer(int port, std::string html_path,
                       int max_conn_queue = 100);
  ~STBlockingHTTPServer();

  bool start();
  void run();
};
#endif