#ifndef MTBlockingHTTPServer_HPP
#define MTBlockingHTTPServer_HTTP

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <condition_variable>
#include <cstdio>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>

class MTBlockingHTTPServer {
private:
    int port;
    std::string html_path;

    int n_threads;
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;

    int server_fd;
    int max_conn_queue;

    template <class F>
    void enqueue_task(F&& f);
    std::string parse_http_path(const std::string& request);
    std::string get_content_type(const std::string& path);
    void send_404(int client_fd);
    void send_500(int client_fd);
    void serve_file(int client_fd, std::string& path);
    void handle_client(int client_fd);

public:
    MTBlockingHTTPServer(int port, std::string html_path, int n_threads, int max_conn_queue = 100);
    ~MTBlockingHTTPServer();

    bool start();
    void run();
};
#endif