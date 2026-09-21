#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <cstdlib>
#include <stdexcept>
#include <exception>
#include <utility>
#include <cerrno>
#include <optional>
#include <system_error>
#include "constants.hpp"


bool handle_inet_pton(const char* server_address, in_addr& in_addr) {
    int inet_pton_return_code = inet_pton(AF_INET, server_address, &in_addr.s_addr);
    if (inet_pton_return_code == -1) {
        std::cerr << "inet_pton failed return_code: " << inet_pton_return_code << std::endl;
        return false;
    } else if (inet_pton_return_code == 0) {
        std::cerr << "inet_pton wrong address and family" << std::endl;
        return false;
    }
    return true;
}


std::optional<std::string> GetMessage(const std::string& payload) {
    if (payload.empty()) {
        return std::nullopt;
    }
    if (payload.size() > MAX_PAYLOAD_SIZE) {
        return std::nullopt;
    }
    std::string message;
    auto msg_size = static_cast<uint32_t>(payload.size());
    auto msg_size_htonl = htonl(msg_size);
    message.append(reinterpret_cast<char*>(&msg_size_htonl), sizeof(msg_size_htonl));
    message.append(payload);
    return message;
}

enum class Code {
    Ok,
    Disconnected,
    Error
};

struct MessageSizeResult {
    Code code;
    size_t message_size;
    std::string error_message;
};

struct MessageResult {
    Code code;
    std::string message;
    std::string error_message;
};

struct SendResult {
    Code code;
    std::string error_message;
};

class MySocket {
public:
    MySocket() : socket_fd(socket(AF_INET, SOCK_STREAM, 0)) {
        CheckSocket();
        SetSockOpt();
    }
    MySocket(int created_socket_fd) : socket_fd(created_socket_fd) {
        CheckSocket();
        SetSockOpt();
    }
    MySocket(const MySocket&) = delete;
    MySocket& operator=(const MySocket&) = delete;

    ~MySocket() {
        CloseSocket();
    }

    int GetSocket() const { return socket_fd; }

    int Connect(int port, in_addr addr) {
        auto prepared_addr = PrepareAddress(port, addr);
        auto prepared_addr_ptr = reinterpret_cast<const sockaddr*>(&prepared_addr);
        return connect(socket_fd, prepared_addr_ptr, sizeof(prepared_addr));
    }

    int Bind(int port, in_addr addr) {
        auto prepared_addr = PrepareAddress(port, addr);
        auto prepared_addr_ptr = reinterpret_cast<const sockaddr*>(&prepared_addr);
        return bind(socket_fd, prepared_addr_ptr, sizeof(prepared_addr));
    }

    MessageSizeResult ReceiveMessageSize() {
        std::cout << "Receiving message size..." << std::endl;
        size_t num_received_bytes = 0;
        uint32_t message_size;
        while (num_received_bytes < sizeof(message_size)) {
            ssize_t recv_return_code = recv(socket_fd, reinterpret_cast<char*>(&message_size) + num_received_bytes, sizeof(message_size) - num_received_bytes, 0);
            if (recv_return_code > 0) {
                std::cout << "Received " << recv_return_code << " bytes" << std::endl;
                num_received_bytes += static_cast<size_t>(recv_return_code);
            } else if (recv_return_code == 0) {
                if (num_received_bytes == 0) {
                    return {.code = Code::Disconnected};
                }
                return {.code = Code::Error, .error_message = "Peer disconnected in the middle of message header"};
            } else if (errno == EINTR) {
                std::cout << "errno is EINTR with msg: " << std::strerror(errno) << ", continue receiving msg_size..." << std::endl;
            } else {
                return {.code = Code::Error, .error_message = std::strerror(errno)};
            }
        }
        auto message_size_ntohl = ntohl(message_size);
        if (message_size_ntohl == 0 || message_size_ntohl > MAX_PAYLOAD_SIZE) {
            return {
                .code = Code::Error,
                .error_message = "Message size " + std::to_string(message_size_ntohl)
                    + " is outside the allowed range [1, "
                    + std::to_string(MAX_PAYLOAD_SIZE) + "]"
            };
        }
        return {.code = Code::Ok, .message_size = static_cast<size_t>(message_size_ntohl)};
    }

    MessageResult ReceiveMessage(size_t message_size) {
        std::cout << "Receiving message..." << std::endl;
        std::string buffer(message_size, 0);
        size_t num_received_bytes = 0;
        while (num_received_bytes < message_size) {
            ssize_t recv_return_code = recv(socket_fd, buffer.data() + num_received_bytes, buffer.size() - num_received_bytes, 0);
            if (recv_return_code > 0) {
                std::cout << "Received " << recv_return_code << " bytes" << std::endl;
                num_received_bytes += static_cast<size_t>(recv_return_code);
            } else if (recv_return_code == 0) {
                if (num_received_bytes == 0) {
                    return {.code = Code::Error, .error_message = "Peer disconnected while header with msg_size was sent"};
                }
                return {.code = Code::Error, .error_message = "Peer disconnected in the middle of message"};
            } else if (errno == EINTR) {
                std::cout << "errno is EINTR with msg: " << std::strerror(errno) << ", continue receiving message..." << std::endl;
            } else {
                return {.code = Code::Error, .error_message = std::strerror(errno)};
            }
        }
        return {.code = Code::Ok, .message = std::move(buffer)};
    }

    SendResult Send(const std::string& message) {
        size_t num_sent_bytes = 0;
        while (num_sent_bytes < message.size()) {
            ssize_t send_return_code = send(socket_fd, message.data() + num_sent_bytes, message.size() - num_sent_bytes, 0);
            if (send_return_code > 0) {
                std::cout << "Sent " << send_return_code << " bytes" << std::endl;
                num_sent_bytes += static_cast<size_t>(send_return_code);
            } else if (send_return_code == 0) {
                return {.code = Code::Error, .error_message = "Send 0 bytes => no progress error"};
            } else if (errno == EINTR) {
                std::cout << "errno is EINTR with msg: " << std::strerror(errno) << ", continue sending message..." << std::endl;
            } else {
                return {.code = Code::Error, .error_message = std::strerror(errno)};
            }
        }
        return {.code = Code::Ok};
    }

private:
    void CloseSocket() {
        if (IsValidSocket()) {
            int close_return_code = close(socket_fd);
            if (close_return_code != 0) {
                std::cerr << "Socket close error: " << std::strerror(errno) << std::endl;
            }
            socket_fd = -1;
        }
    }

    bool IsValidSocket() const {
        return socket_fd != -1;
    }

    sockaddr_in PrepareAddress(int port, in_addr addr) {
        return {
            .sin_family = AF_INET,
            .sin_port = htons(port),
            .sin_addr = addr,
        };
    }

    void CheckSocket() {
        if (!IsValidSocket()) {
            throw std::runtime_error(std::strerror(errno));
        }
    }

    void SetSockOpt() {
        int enabled = 1;
        int result = setsockopt(
            socket_fd,
            SOL_SOCKET,
            SO_NOSIGPIPE,
            &enabled,
            sizeof(enabled)
        );
        if (result == 0) {
            std::cout << "SO_NOSIGPIPE set" << std::endl;
        } else {
            int saved_errno = errno;
            CloseSocket();
            throw std::system_error(
                saved_errno,
                std::generic_category(),
                "setsockopt(SO_NOSIGPIPE)"
            );
        }
    }

    int socket_fd;
};
