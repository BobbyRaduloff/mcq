#include "servers/STBlockingHTTPServer.hpp"

int main() {
  STBlockingHTTPServer server(81, "/root/mcq/html");

  if (!server.start()) {
    return -1;
  }

  server.run();

  return 0;
}
