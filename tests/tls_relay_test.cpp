#ifndef _WIN32
#include "../common/TlsRelay.h"

#include <openssl/err.h>
#include <openssl/x509v3.h>

#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
bool WriteAll(int fd, const char* data, std::size_t len) {
    std::size_t off = 0;
    while (off < len) {
        const auto n = send(fd, data + off, len - off, MSG_NOSIGNAL);
        if (n <= 0) return false;
        off += static_cast<std::size_t>(n);
    }
    return true;
}
}

int main(int argc, char** argv) {
    if (argc != 6) {
        std::cerr << "usage: tls_relay_test CA.crt SERVER.crt SERVER.key CLIENT.crt CLIENT.key\n";
        return 2;
    }
    SSL_CTX* server_ctx = SSL_CTX_new(TLS_server_method());
    SSL_CTX* client_ctx = SSL_CTX_new(TLS_client_method());
    if (!server_ctx || !client_ctx) return 3;
    SSL_CTX_set_min_proto_version(server_ctx, TLS1_2_VERSION);
    SSL_CTX_set_min_proto_version(client_ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(server_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    SSL_CTX_set_verify(client_ctx, SSL_VERIFY_PEER, nullptr);
    if (SSL_CTX_use_certificate_chain_file(server_ctx, argv[2]) != 1 ||
        SSL_CTX_use_PrivateKey_file(server_ctx, argv[3], SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_load_verify_locations(server_ctx, argv[1], nullptr) != 1 ||
        SSL_CTX_load_verify_locations(client_ctx, argv[1], nullptr) != 1 ||
        SSL_CTX_use_certificate_chain_file(client_ctx, argv[4]) != 1 ||
        SSL_CTX_use_PrivateKey_file(client_ctx, argv[5], SSL_FILETYPE_PEM) != 1) {
        std::cerr << "could not load test PKI\n";
        return 4;
    }

    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(listener, 1) != 0) return 5;
    socklen_t addr_len = sizeof(addr);
    getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &addr_len);
    const int client_net = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(client_net, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return 6;
    const int server_net = accept(listener, nullptr, nullptr);
    close(listener);
    if (server_net < 0) return 7;

    SSL* server_ssl = SSL_new(server_ctx);
    SSL* client_ssl = SSL_new(client_ctx);
    SSL_set_fd(server_ssl, server_net);
    SSL_set_fd(client_ssl, client_net);
    SSL_set_tlsext_host_name(client_ssl, "localhost");
    SSL_set1_host(client_ssl, "localhost");
    std::atomic_bool server_handshake_ok{false};
    std::thread handshake([&] { server_handshake_ok.store(SSL_accept(server_ssl) == 1); });
    const bool client_handshake_ok = SSL_connect(client_ssl) == 1;
    handshake.join();
    if (!client_handshake_ok || !server_handshake_ok.load() ||
        SSL_get_verify_result(client_ssl) != X509_V_OK || SSL_get_verify_result(server_ssl) != X509_V_OK) {
        std::cerr << "mutual TLS handshake failed\n";
        return 8;
    }

    int local_pair[2]{};
    int target_pair[2]{};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, local_pair) != 0 ||
        socketpair(AF_UNIX, SOCK_STREAM, 0, target_pair) != 0) return 9;

    const std::string payload = "WAProxy TLS relay end-to-end payload 0123456789";
    std::string echoed;
    std::thread application([&] {
        WriteAll(local_pair[0], payload.data(), payload.size());
        shutdown(local_pair[0], SHUT_WR);
        std::array<char, 256> buffer{};
        for (;;) {
            const auto n = recv(local_pair[0], buffer.data(), buffer.size(), 0);
            if (n <= 0) break;
            echoed.append(buffer.data(), static_cast<std::size_t>(n));
        }
        close(local_pair[0]);
    });
    std::thread echo_target([&] {
        std::array<char, 256> buffer{};
        for (;;) {
            const auto n = recv(target_pair[0], buffer.data(), buffer.size(), 0);
            if (n <= 0) break;
            if (!WriteAll(target_pair[0], buffer.data(), static_cast<std::size_t>(n))) break;
        }
        close(target_pair[0]);
    });

    std::atomic_bool stop{false};
    wa::RelayStats server_stats;
    wa::RelayStats client_stats;
    std::string server_reason;
    std::string client_reason;
    std::thread server_relay([&] {
        wa::RelayTlsAndSocket(server_ssl, target_pair[1], 10000, stop, server_stats, server_reason);
        close(target_pair[1]);
    });
    const bool client_ok = wa::RelayTlsAndSocket(client_ssl, local_pair[1], 10000, stop, client_stats, client_reason);
    close(local_pair[1]);
    server_relay.join();
    application.join();
    echo_target.join();

    const bool payload_ok = echoed == payload;
    SSL_free(client_ssl);
    SSL_free(server_ssl);
    close(client_net);
    close(server_net);
    SSL_CTX_free(client_ctx);
    SSL_CTX_free(server_ctx);

    if (!client_ok || !payload_ok) {
        std::cerr << "relay failed client_ok=" << client_ok << " echoed=" << echoed
                  << " client_reason=" << client_reason << " server_reason=" << server_reason << '\n';
        return 10;
    }
    std::cout << "PASS: mutual TLS stream relayed bidirectionally through TLS and TCP\n";
    std::cout << "PASS: half-close and close-notify completed\n";
    return 0;
}
#else
int main() { return 77; }
#endif
