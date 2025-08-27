#include "STBlockingHTTPServer.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

STBlockingHTTPServer::STBlockingHTTPServer(int port, std::string html_path,
                                           int max_conn_queue)
    : port(port),
      html_path(html_path),
      server_fd(-1),
      max_conn_queue(max_conn_queue) {}

STBlockingHTTPServer::~STBlockingHTTPServer() {
  if (server_fd != -1) {
    close(server_fd);
  }
}

std::string STBlockingHTTPServer::parse_http_path(const std::string& request) {
  std::istringstream iss(request);
  std::string method, path, version;
  iss >> method >> path >> version;

  return path;
}

std::string STBlockingHTTPServer::get_content_type(const std::string& path) {
  if (path.ends_with(".html") || path.ends_with(".htm")) {
    return "text/html";
  } else if (path.ends_with(".css")) {
    return "text/css";
  } else if (path.ends_with(".js")) {
    return "application/javascript";
  } else if (path.ends_with(".json")) {
    return "application/json";
  } else if (path.ends_with(".png")) {
    return "image/png";
  } else if (path.ends_with(".jpg") || path.ends_with(".jpeg")) {
    return "image/jpeg";
  }
  return "text/plain";
}

void STBlockingHTTPServer::send_404(int client_fd) {
  std::string response =
      "HTTP/1.1 404 Not Found\r\n"
      "Content-Type: text/html\r\n"
      "Content-Length: 47\r\n"
      "Connection: close\r\n"
      "\r\n"
      "<html><body><h1>404 Not Found</h1></body></html>";

  send(client_fd, response.c_str(), response.length(), 0);
}

void STBlockingHTTPServer::send_500(int client_fd) {
  std::string response =
      "HTTP/1.1 500 Internal Server Error\r\n"
      "Content-Type: text/html\r\n"
      "Content-Length: 57\r\n"
      "Connection: close\r\n"
      "\r\n"
      "<html><body><h1>500 Internal Server Error</h1></body></html>";

  send(client_fd, response.c_str(), response.length(), 0);
}

void STBlockingHTTPServer::serve_file(int client_fd, std::string& path) {
  char buffer[4096] = {0};

  std::string full_path = html_path + path;

  struct stat file_stat;
  if (stat(full_path.c_str(), &file_stat) != 0) {
    std::cerr << "STBlockingHTTPServer: file not found: " << full_path
              << std::endl;
    send_404(client_fd);
    return;
  }

  int fd = open(full_path.c_str(), O_RDONLY);
  if (fd < 0) {
    std::cerr << "STBlockingHTTPServer: can't read " << full_path << std::endl;
    send_500(client_fd);
    return;
  }

  std::string content_type = get_content_type(path);
  ssize_t content_length = file_stat.st_size;

  std::ostringstream headers_stream;
  headers_stream << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: " << content_type << "\r\n"
                 << "Content-Length: " << content_length << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n";
  std::string headers = headers_stream.str();
  ssize_t headers_len = headers.size();
  if (send(client_fd, headers.c_str(), headers_len, 0) < 0) {
    std::cerr << "STBlockingHTTPServer: couldn't send headers" << std::endl;
    close(fd);
    return;
  }

  ssize_t bytes_read;
  while ((bytes_read = read(fd, buffer, 4096)) > 0) {
    ssize_t bytes_send = send(client_fd, buffer, bytes_read, 0);
    if (bytes_read != bytes_send) {
      std::cerr << "STBlockingHTTPServer: couldn't send all we've read"
                << std::endl;
      close(fd);
      return;
    }
  }

  if (bytes_read < 0) {
    std::cerr << "STBlockingHTTPServer: failed to read " << full_path
              << std::endl;
  }

  close(fd);
}

void STBlockingHTTPServer::handle_client(int client_fd) {
  char buffer[4096] = {0};

  ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
  if (bytes_read <= 0) {
    close(client_fd);
    return;
  }

  std::string request(buffer, bytes_read);
  std::string path = parse_http_path(request);

  serve_file(client_fd, path);

  close(client_fd);
}

bool STBlockingHTTPServer::start() {
  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    std::cerr << "STBlockingHTTPServer: failed to create socket" << std::endl;
    return false;
  }

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
    std::cerr << "STBlockingHTTPServer: failed to bind to *:" << port
              << std::endl;
    return false;
  }

  if (listen(server_fd, max_conn_queue) < 0) {
    std::cerr << "STBlockingHTTPServer: failed to listen on *:" << port
              << std::endl;
    return false;
  }

  std::cout << "STBlockingHTTPServer: listening on *:" << port << std::endl;
  return true;
}

void STBlockingHTTPServer::run() {
  for (;;) {
    struct sockaddr_in client_addr;
    socklen_t client_add_len = sizeof(client_addr);

    int client_fd =
        accept(server_fd, (struct sockaddr*)&client_addr, &client_add_len);
    if (client_fd < 0) {
      std::cerr << "STBlockingHTTPServer: failed to accept client connection"
                << std::endl;
      continue;
    }

    handle_client(client_fd);
  }
}