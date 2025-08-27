#include "servers/MTBlockingHTTPServer.hpp"

int main()
{
    MTBlockingHTTPServer server(8080, "/var/www/raduloff.dev", 8);

    if (!server.start()) {
        return -1;
    }

    server.run();

    return 0;
}
