#include "core.hpp"
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>
using namespace ad;
namespace {
struct Server {
    SOCKET listener = INVALID_SOCKET;
    std::thread thread;
    std::string received;
    uint16_t port = 0;
    explicit Server(std::string response) {
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET)
            throw std::runtime_error("test socket failed");
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener, (sockaddr *)&addr, sizeof addr) || listen(listener, 1))
            throw std::runtime_error("test bind failed");
        int len = sizeof addr;
        getsockname(listener, (sockaddr *)&addr, &len);
        port = ntohs(addr.sin_port);
        thread = std::thread([this, response] {
            auto client = accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET)
                return;
            DWORD timeout = 5000;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof timeout);
            char buffer[4096];
            int n;
            while ((n = recv(client, buffer, sizeof buffer, 0)) > 0) {
                received.append(buffer, n);
                auto split = received.find("\r\n\r\n");
                if (split != std::string::npos) {
                    auto length = received.find("Content-Length: ");
                    size_t count =
                        length == std::string::npos ? 0 : std::stoull(received.substr(length + 16));
                    if (received.size() >= split + 4 + count)
                        break;
                }
            }
            size_t sent = 0;
            while (sent < response.size()) {
                int n = send(client, response.data() + sent, (int)(response.size() - sent), 0);
                if (n <= 0)
                    break;
                sent += n;
            }
            shutdown(client, SD_BOTH);
            closesocket(client);
        });
    }
    void join() {
        if (thread.joinable())
            thread.join();
    }
    ~Server() {
        closesocket(listener);
        join();
    }
    std::string base() {
        return "http://127.0.0.1:" + std::to_string(port) + "/v1";
    }
};
std::string reply(const J &value, int status = 200) {
    auto body = value.dump();
    return "HTTP/1.1 " + std::to_string(status) +
           " Test\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\nConnection: close\r\n\r\n" + body;
}
} // namespace
int network_tests(Models &models) {
    WSADATA ws{};
    if (WSAStartup(MAKEWORD(2, 2), &ws))
        throw std::runtime_error("Winsock init failed");
    int count = 0;
    auto check = [&](bool result, const char *label) {
        if (!result)
            throw std::runtime_error(label);
        ++count;
    };
    J p = {{"model", "mock"}, {"protocol", "chat"}, {"auth", "api-key"}};
    {
        Server server(reply({{"choices", J::array({{{"message", {{"content", "OK"}}}}})}}));
        p["base_url"] = server.base();
        auto result =
            models.request(p, "fake-network-key", "chat/completions", Models::payload(p, "s", "p", 128));
        server.join();
        check(server.received.find("POST /v1/chat/completions") != std::string::npos, "network request path");
        check(server.received.find("api-key: fake-network-key") != std::string::npos &&
                  server.received.find("Authorization:") == std::string::npos,
              "Azure header");
        check(Models::extract(result, "chat") == "OK", "network response extraction");
    }
    {
        Server server("HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:1/leak\r\nContent-Length: "
                      "0\r\nConnection: close\r\n\r\n");
        p["base_url"] = server.base();
        bool refused = false;
        try {
            models.request(p, "secret", "models");
        } catch (const std::exception &) {
            refused = true;
        }
        check(refused, "redirect must be refused");
    }
    {
        Server server(reply({{"error", "fake-network-key must not appear"}}, 401));
        p["base_url"] = server.base();
        bool refused = false;
        try {
            models.request(p, "fake-network-key", "models");
        } catch (const std::exception &e) {
            refused = true;
            check(std::string(e.what()).find("fake-network-key") == std::string::npos,
                  "server credential echo");
        }
        check(refused, "HTTP error surfaced");
    }
    {
        Server server(reply({{"content", J::array({{{"type", "text"}, {"text", "OK"}}})}}));
        p["base_url"] = server.base();
        p["protocol"] = "anthropic";
        p["auth"] = "x-api-key";
        models.request(p, "fake-anthropic", "messages", Models::payload(p, "sys", "prompt", 128));
        server.join();
        check(server.received.find("x-api-key: fake-anthropic") != std::string::npos &&
                  server.received.find("anthropic-version: 2023-06-01") != std::string::npos,
              "Anthropic headers");
    }
    {
        std::string draft =
            "---\nname: enhanced-skill\ndescription: Review supplied records for quality problems and "
            "provide a reproducible report when the user requests validation.\n---\n\n# Workflow\n\nCheck "
            "inputs and output findings with evidence.\n";
        Server server(reply(
            {{"choices", J::array({{{"message", {{"content", draft}}}, {"finish_reason", "length"}}})}}));
        auto preset = models.save({{"name", "Generator fixture"},
                                   {"base_url", server.base()},
                                   {"protocol", "chat"},
                                   {"model", "mock"}});
        J options = {{"detail", "detailed"},
                     {"language", "en"},
                     {"tokens", "4096"},
                     {"context", "Output CSV with file and reason columns."},
                     {"existing", draft}};
        auto generated = models.generate(preset["id"], "Improve this records validation skill.", options);
        server.join();
        auto body = J::parse(server.received.substr(server.received.find("\r\n\r\n") + 4));
        auto requirements = J::parse(body["messages"][1]["content"].get<std::string>());
        check(requirements["待完善草稿"] == draft && requirements["补充背景与约束"] == options["context"],
              "generator retains existing draft and supplied constraints");
        check(body["max_tokens"] == 4096 && generated["valid"] == true,
              "generator output budget and skill parsing");
        check(generated["truncated"] == true && !generated["quality"]["suggestions"].empty(),
              "generator surfaces truncation and sparse draft");
        models.remove(preset["id"]);
    }
    WSACleanup();
    return count;
}
