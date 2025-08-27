#include "MTBlockingHTTPServer.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

MTBlockingHTTPServer::MTBlockingHTTPServer(int port, std::string html_path, int n_threads,
    int max_conn_queue)
    : port(port)
    , html_path(html_path)
    , n_threads(n_threads)
    , stop(false)
    , server_fd(-1)
    , max_conn_queue(max_conn_queue)
{
    for (int i = 0; i < n_threads; i++) {
        workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    // prevents spurious wakeup
                    condition.wait(lock, [this] { return stop || !tasks.empty(); });

                    if (stop && tasks.empty())
                        return;

                    task = std::move(tasks.front());
                    tasks.pop();
                }
                task();
            }
        });
    }
}

MTBlockingHTTPServer::~MTBlockingHTTPServer()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }

    condition.notify_all();
    for (std::thread& worker : workers) {
        worker.join();
    }

    if (server_fd != -1) {
        close(server_fd);
    }
}

// i hate templates but this allows us to move the task without copy
template <class F>
void MTBlockingHTTPServer::enqueue_task(F&& f)
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");
        tasks.emplace(std::forward<F>(f));
    }
    condition.notify_one();
}

std::string MTBlockingHTTPServer::parse_http_path(const std::string& request)
{
    std::istringstream iss(request);
    std::string method, path, version;
    iss >> method >> path >> version;

    return path;
}

std::string MTBlockingHTTPServer::get_content_type(const std::string& path)
{
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
    } else if (path.ends_with(".webp")) {
        return "image/webp";
    }

    return "text/plain";
}

void MTBlockingHTTPServer::send_404(int client_fd)
{
    std::string response = "HTTP/1.1 404 Not Found\r\n"
                           "Content-Type: text/html\r\n"
                           "Content-Length: 47\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "<html><body><h1>404 Not Found</h1></body></html>";

    send(client_fd, response.c_str(), response.length(), 0);
}

void MTBlockingHTTPServer::send_500(int client_fd)
{
    std::string response = "HTTP/1.1 500 Internal Server Error\r\n"
                           "Content-Type: text/html\r\n"
                           "Content-Length: 57\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "<html><body><h1>500 Internal Server Error</h1></body></html>";

    send(client_fd, response.c_str(), response.length(), 0);
}

void MTBlockingHTTPServer::serve_file(int client_fd, std::string& path)
{
    std::string full_path = html_path + path;

    struct stat file_stat;
    if (stat(full_path.c_str(), &file_stat) != 0) {
        std::cerr << "MTBlockingHTTPServer: file not found: " << full_path
                  << std::endl;
        send_404(client_fd);
        return;
    }

    int fd = open(full_path.c_str(), O_RDONLY);
    if (fd < 0) {
        std::cerr << "MTBlockingHTTPServer: can't read " << full_path << std::endl;
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
        std::cerr << "MTBlockingHTTPServer: couldn't send headers" << std::endl;
        close(fd);
        return;
    }

    ssize_t offset;
    ssize_t left_to_send = content_length;
    ssize_t bytes_read;
    while (left_to_send > 0 && (bytes_read = sendfile(client_fd, fd, &offset, left_to_send)) > 0) {
        left_to_send -= bytes_read;
    }

    if (bytes_read < 0) {
        std::cerr << "MTBlockingHTTPServer: couldn't send the whole file" << std::endl;
        close(fd);
        return;
    }

    close(fd);
}

void MTBlockingHTTPServer::handle_client(int client_fd)
{
    char buffer[4096] = { 0 };

    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }

    std::string request(buffer, bytes_read);
    std::string path = parse_http_path(request);
    std::filesystem::path parsed_path(path);

    if (parsed_path.extension().string().size() == 0) {
        if (path.size() == 1) {
            path = "/index.html";
        } else {
            path = path + "/index.html";
        }
    }

    serve_file(client_fd, path);

    close(client_fd);
}

bool MTBlockingHTTPServer::start()
{
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "MTBlockingHTTPServer: failed to create socket" << std::endl;
        return false;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "MTBlockingHTTPServer: failed to bind to *:" << port
                  << std::endl;
        return false;
    }

    if (listen(server_fd, max_conn_queue) < 0) {
        std::cerr << "MTBlockingHTTPServer: failed to listen on *:" << port
                  << std::endl;
        return false;
    }

    std::cout << "MTBlockingHTTPServer: listening on *:" << port << std::endl;
    return true;
}

void MTBlockingHTTPServer::run()
{
    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t client_add_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_add_len);
        if (client_fd < 0) {
            std::cerr << "MTBlockingHTTPServer: failed to accept client connection"
                      << std::endl;
            continue;
        }

        enqueue_task([this, client_fd] {
            handle_client(client_fd);
        });
    }
}