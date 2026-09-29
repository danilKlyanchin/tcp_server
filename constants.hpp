#pragma once

#include <cstdint>

const int SERVER_PORT = 12345;
const char SERVER_ADDRESS[] = "127.0.0.1";
const uint32_t MAX_PAYLOAD_SIZE = 1024 * 1024;
constexpr int MAX_CONCURRENT_CLIENTS = 64;
constexpr int CLIENT_IO_TIMEOUT_SECONDS = 30;
