#pragma once

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using WaSocket = SOCKET;
constexpr WaSocket WA_INVALID_SOCKET = INVALID_SOCKET;
#else
#include <sys/socket.h>
using WaSocket = int;
constexpr WaSocket WA_INVALID_SOCKET = -1;
#endif

namespace wa {
bool InitializeSockets(std::string& error);
void CleanupSockets();
void CloseSocket(WaSocket socket);
int LastSocketError();
bool SetSocketNonBlocking(WaSocket socket, bool enabled, std::string& error);
WaSocket ConnectTcp(const std::string& host, unsigned short port,
                    unsigned int timeout_ms, std::string& error);
bool SendAll(WaSocket socket, const void* data, std::size_t size, std::string& error);
void ShutdownSend(WaSocket socket);
std::string OpenSslError();
} // namespace wa
