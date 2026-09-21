#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <cstdlib>
#include "utils.hpp"
#include "constants.hpp"

void HandleClient(MySocket& server_socket) {
    std::cout << "Waiting for connection..." << std::endl;
    auto accepted_socket_fd = accept(server_socket.GetSocket(), nullptr, nullptr);
    if (accepted_socket_fd == -1 && errno == EINTR) {
        return;
    } else if (accepted_socket_fd == -1) {
        std::cerr << "accept error: " << std::strerror(errno) << '\n';
        return;
    }
    auto client_socket = MySocket(accepted_socket_fd);
    std::cout << "Connection accepted" << std::endl;

    while (true) {
        std::cout << "Waiting for message from client..." << std::endl;
        auto msg_size_result = client_socket.ReceiveMessageSize();
        if (msg_size_result.code == Code::Error) {
            std::cerr << "ReceiveMessageSize failed with error: " << msg_size_result.error_message << std::endl;
            return;
        }
        if (msg_size_result.code == Code::Disconnected) {
            std::cout << "Peer disconnected" << std::endl;
            return;
        }

        auto message_result = client_socket.ReceiveMessage(msg_size_result.message_size);
        if (message_result.code == Code::Ok) {
            std::cout << "Received from client: " << message_result.message << std::endl;
            std::string response = "Hello Client who said: " + std::string(message_result.message);
            auto message_opt = GetMessage(response);
            if (!message_opt.has_value()) {
                std::cerr << "Invalid payload, failed answer to client, check payload size in (1, " << MAX_PAYLOAD_SIZE << "]\n";
                return;
            }
            auto send_result = client_socket.Send(message_opt.value());
            if (send_result.code == Code::Error) {
                std::cerr << "send failed with error: " << send_result.error_message << std::endl;
                return;
            }
        } else if (message_result.code == Code::Disconnected) {
            std::cout << "Peer closed the connection\n";
            return;
        } else {
            std::cerr << "recv failed with error " << message_result.error_message << std::endl;
            return;
        }
    }
}

int RunServer() {
    in_addr server_in_addr;
    if (!handle_inet_pton(SERVER_ADDRESS, server_in_addr)) {
        return EXIT_FAILURE;
    }

    auto server_socket = MySocket();
    int bind_return_code = server_socket.Bind(SERVER_PORT, server_in_addr);
    if (bind_return_code != 0) {
        std::cerr << "bind error: " << std::strerror(errno) << '\n';
        return EXIT_FAILURE;
    }

    int listen_return_code = listen(server_socket.GetSocket(), 1);
    if (listen_return_code != 0) {
        std::cerr << "listen error: " << std::strerror(errno) << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "Server listening on port: " << SERVER_PORT << std::endl;
    while (true) {
        try {
            HandleClient(server_socket);
        } catch (const std::exception& e) {
            std::cerr << "Failed to accept connection with error " << e.what() << '\n';
            continue;
        }
    }

    return 0;
}


int main() {
    try {
        return RunServer();
    } catch (const std::exception& e) {
        std::cerr << "Server failed with error " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
