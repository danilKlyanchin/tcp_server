#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <string>
#include <cstdlib>
#include <memory>
#include <semaphore>
#include <thread>
#include "utils.hpp"
#include "constants.hpp"
#include "logging.hpp"

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

void HandleClient(MySocket client_socket, uint64_t client_id) {
    LogInfo("[client ", client_id, "] connection accepted");

    while (true) {
        LogInfo("[client ", client_id, "] waiting for message");
        auto msg_size_result = client_socket.ReceiveMessageSize();
        if (msg_size_result.code == Code::Error) {
            LogError("[client ", client_id, "] failed to receive message size: ", msg_size_result.error_message);
            return;
        }
        if (msg_size_result.code == Code::Disconnected) {
            LogInfo("[client ", client_id, "] disconnected");
            return;
        }

        auto message_result = client_socket.ReceiveMessage(msg_size_result.message_size);
        if (message_result.code == Code::Ok) {
            LogInfo("[client ", client_id, "] received payload bytes=", message_result.message.size());
            std::string response = "Hello Client who said: " + std::string(message_result.message);
            auto message_opt = GetMessage(response);
            if (!message_opt.has_value()) {
                LogError("[client ", client_id, "] response payload is outside range (1, ", MAX_PAYLOAD_SIZE, "]");
                return;
            }
            auto send_result = client_socket.Send(message_opt.value());
            if (send_result.code == Code::Error) {
                LogError("[client ", client_id, "] send failed: ", send_result.error_message);
                return;
            }
            LogInfo("[client ", client_id, "] sent response payload bytes=", response.size());
        } else if (message_result.code == Code::Disconnected) {
            LogInfo("[client ", client_id, "] disconnected while receiving payload");
            return;
        } else {
            LogError("[client ", client_id, "] receive failed: ", message_result.error_message);
            return;
        }
    }
}

void HandleClientSafely(
    MySocket client_socket,
    std::shared_ptr<ClientSlots> client_slots,
    uint64_t client_id
) noexcept {
    ClientSlotGuard client_slot_guard(std::move(client_slots));
    try {
        HandleClient(std::move(client_socket), client_id);
    } catch (const std::exception& e) {
        LogError("[client ", client_id, "] handler failed: ", e.what());
    } catch (...) {
        LogError("[client ", client_id, "] handler failed with an unknown exception");
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
        LogError("[server] bind failed: ", std::strerror(errno));
        return EXIT_FAILURE;
    }

    int listen_return_code = listen(
        server_socket.GetSocket(),
        MAX_CONCURRENT_CLIENTS
    );
    if (listen_return_code != 0) {
        LogError("[server] listen failed: ", std::strerror(errno));
        return EXIT_FAILURE;
    }

    LogInfo("[server] listening on port ", SERVER_PORT);
    auto client_slots = std::make_shared<ClientSlots>(MAX_CONCURRENT_CLIENTS);
    uint64_t next_client_id = 1;
    while (true) {
        LogInfo("[server] waiting for connection");
        int accepted_socket_fd = accept(server_socket.GetSocket(), nullptr, nullptr);
        if (accepted_socket_fd == -1 && errno == EINTR) {
            continue;
        }
        if (accepted_socket_fd == -1) {
            LogError("[server] accept failed: ", std::strerror(errno));
            continue;
        }

        const uint64_t client_id = next_client_id++;
        bool client_slot_acquired = false;
        try {
            MySocket client_socket(accepted_socket_fd);
            if (!client_slots->try_acquire()) {
                LogError("[client ", client_id, "] rejected: too many clients");
                continue;
            }
            client_slot_acquired = true;

            std::thread client_thread(
                HandleClientSafely,
                std::move(client_socket),
                client_slots,
                client_id
            );
            client_slot_acquired = false;
            client_thread.detach();
        } catch (const std::exception& e) {
            if (client_slot_acquired) {
                client_slots->release();
            }
            LogError("[client ", client_id, "] failed to start handler thread: ", e.what());
        }
    }

    return 0;
}


int main() {
    try {
        return RunServer();
    } catch (const std::exception& e) {
        LogError("[server] fatal error: ", e.what());
        return EXIT_FAILURE;
    }
}
