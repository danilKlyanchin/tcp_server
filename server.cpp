#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <cstdlib>
#include <memory>
#include <semaphore>
#include <thread>
#include "utils.hpp"
#include "constants.hpp"

using ClientSlots = std::counting_semaphore<MAX_CONCURRENT_CLIENTS>;

class ClientSlotGuard {
public:
    explicit ClientSlotGuard(std::shared_ptr<ClientSlots> client_slots)
        : client_slots_(std::move(client_slots)) {}

    ClientSlotGuard(const ClientSlotGuard&) = delete;
    ClientSlotGuard& operator=(const ClientSlotGuard&) = delete;

    ~ClientSlotGuard() {
        client_slots_->release();
    }

private:
    std::shared_ptr<ClientSlots> client_slots_;
};

void HandleClient(MySocket client_socket) {
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

void HandleClientSafely(
    MySocket client_socket,
    std::shared_ptr<ClientSlots> client_slots
) noexcept {
    ClientSlotGuard client_slot_guard(std::move(client_slots));
    try {
        HandleClient(std::move(client_socket));
    } catch (const std::exception& e) {
        std::cerr << "Client handler failed: " << e.what() << '\n';
    } catch (...) {
        std::cerr << "Client handler failed with an unknown exception\n";
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

    int listen_return_code = listen(
        server_socket.GetSocket(),
        MAX_CONCURRENT_CLIENTS
    );
    if (listen_return_code != 0) {
        std::cerr << "listen error: " << std::strerror(errno) << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "Server listening on port: " << SERVER_PORT << std::endl;
    auto client_slots = std::make_shared<ClientSlots>(MAX_CONCURRENT_CLIENTS);
    while (true) {
        std::cout << "Waiting for connection..." << std::endl;
        int accepted_socket_fd = accept(server_socket.GetSocket(), nullptr, nullptr);
        if (accepted_socket_fd == -1 && errno == EINTR) {
            continue;
        }
        if (accepted_socket_fd == -1) {
            std::cerr << "accept error: " << std::strerror(errno) << '\n';
            continue;
        }

        bool client_slot_acquired = false;
        try {
            MySocket client_socket(accepted_socket_fd);
            if (!client_slots->try_acquire()) {
                std::cerr << "Too many clients, connection rejected\n";
                continue;
            }
            client_slot_acquired = true;

            std::thread client_thread(
                HandleClientSafely,
                std::move(client_socket),
                client_slots
            );
            client_slot_acquired = false;
            client_thread.detach();
        } catch (const std::exception& e) {
            if (client_slot_acquired) {
                client_slots->release();
            }
            std::cerr << "Failed to start client thread: " << e.what() << '\n';
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
