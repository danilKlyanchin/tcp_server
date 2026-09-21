#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include <string>
#include "utils.hpp"
#include "constants.hpp"

std::string GetPayload() {
    std::string payload;
    std::cout << "Me: ";
    std::getline(std::cin, payload);
    return payload;
}

int RunClient()  {
    auto client_socket = MySocket();

    in_addr server_in_addr;
    if (!handle_inet_pton(SERVER_ADDRESS, server_in_addr)) {
        return EXIT_FAILURE;
    }
    int connect_return_code = client_socket.Connect(SERVER_PORT, server_in_addr);
    if (connect_return_code != 0) {
        std::cerr << "connect failed with error " << std::strerror(errno) << std::endl;
        return EXIT_FAILURE;
    }

    while (true) {
        std::string payload = GetPayload();
        if (payload.empty()) {
            break;
        }

        auto message_opt = GetMessage(payload);
        if (!message_opt.has_value()) {
            std::cerr << "Invalid payload, check its size in (1, " << MAX_PAYLOAD_SIZE << "]\n";
            continue;
        }

        auto send_result = client_socket.Send(message_opt.value());
        if (send_result.code == Code::Error) {
            std::cerr << "send failed with error: " << send_result.error_message << std::endl;
            return EXIT_FAILURE;
        }

        auto msg_size_result = client_socket.ReceiveMessageSize();
        if (msg_size_result.code == Code::Error) {
            std::cerr << "ReceiveMessageSize failed with error: " << msg_size_result.error_message << std::endl;
            return EXIT_FAILURE;
        }
        if (msg_size_result.code == Code::Disconnected) {
            std::cout << "Peer disconnected" << std::endl;
            break;
        }

        auto message_result = client_socket.ReceiveMessage(msg_size_result.message_size);
        if (message_result.code == Code::Ok) {
            std::cout << "Server: " << message_result.message << std::endl;
        } else if (message_result.code == Code::Disconnected) {
            std::cout << "Peer closed the connection\n";
            break;
        } else {
            std::cerr << "recv failed with error " << message_result.error_message << std::endl;
            return EXIT_FAILURE;
        }
    }
    return 0;
}

int main() {
    try {
        return RunClient();
    } catch (const std::exception& e) {
        std::cerr << "Client failed with error " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
