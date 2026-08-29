#include "NetworkDiagnostics.h"

#include <cstring>

#include <lwip/inet.h>
#include <lwip/inet_chksum.h>
#include <lwip/prot/icmp.h>
#include <lwip/sockets.h>

namespace {
    constexpr size_t PingPacketSize = 32;
    constexpr uint32_t ReplyTimeoutMs = 1000;
    constexpr uint32_t RequestIntervalMs = 1000;

    void Write(WiFiClient& client, const String& text) {
        if (client.connected()) {
            client.write(reinterpret_cast<const uint8_t*>(text.c_str()), text.length());
        }
    }
}

void NetworkDiagnostics::Ping(WiFiClient& client, const String& destination, uint16_t count) {
    IPAddress address;
    if (!address.fromString(destination) && !WiFi.hostByName(destination.c_str(), address)) {
        Write(client, "Ping           | Error: " + destination + " - name or service not known\r\n");
        return;
    }

    const String resolvedAddress = address.toString();
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = 0;
    target.sin_addr.s_addr = inet_addr(resolvedAddress.c_str());

    const bool dnsName = destination != resolvedAddress;
    Write(
        client,
        "Ping           | Destination: " + (dnsName ? resolvedAddress + " (" + destination + ")" : resolvedAddress) +
            " - " + String(PingPacketSize) + " bytes of data.\r\n\r\n"
    );

    const int socketDescriptor = lwip_socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (socketDescriptor < 0) {
        Write(client, "               | Error: Cannot create ICMP socket\r\n");
        return;
    }

    timeval timeout{};
    timeout.tv_sec = ReplyTimeoutMs / 1000U;
    timeout.tv_usec = (ReplyTimeoutMs % 1000U) * 1000U;
    (void)lwip_setsockopt(socketDescriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    const uint16_t identifier = static_cast<uint16_t>(ESP.getEfuseMac() & 0xffffU);
    uint32_t transmitted = 0;
    uint32_t received = 0;
    uint32_t minimumTime = UINT32_MAX;
    uint32_t maximumTime = 0;
    uint32_t totalTime = 0;

    for (uint16_t sequence = 0; sequence < count && client.connected(); ++sequence) {
        uint8_t packet[PingPacketSize]{};
        auto* header = reinterpret_cast<icmp_echo_hdr*>(packet);
        header->type = ICMP_ECHO;
        header->code = 0;
        header->id = htons(identifier);
        header->seqno = htons(sequence);
        for (size_t index = sizeof(icmp_echo_hdr); index < sizeof(packet); ++index) {
            packet[index] = static_cast<uint8_t>(index);
        }
        header->chksum = 0;
        header->chksum = inet_chksum(packet, sizeof(packet));

        const uint32_t startedAt = millis();
        const int sent = lwip_sendto(
            socketDescriptor,
            packet,
            sizeof(packet),
            0,
            reinterpret_cast<const sockaddr*>(&target),
            sizeof(target)
        );
        ++transmitted;

        if (sent != static_cast<int>(sizeof(packet))) {
            Write(client, "               | From " + resolvedAddress + " icmp_seq=" + String(sequence + 1) + " send failed\r\n");
        } else {
            uint8_t reply[128]{};
            sockaddr_in source{};
            socklen_t sourceLength = sizeof(source);
            const int replyLength = lwip_recvfrom(
                socketDescriptor,
                reply,
                sizeof(reply),
                0,
                reinterpret_cast<sockaddr*>(&source),
                &sourceLength
            );
            const uint32_t elapsed = millis() - startedAt;

            bool validReply = false;
            uint8_t ttl = 0;
            if (replyLength >= static_cast<int>(20 + sizeof(icmp_echo_hdr))) {
                const size_t ipHeaderLength = static_cast<size_t>(reply[0] & 0x0fU) * 4U;
                if (ipHeaderLength >= 20 && replyLength >= static_cast<int>(ipHeaderLength + sizeof(icmp_echo_hdr))) {
                    const auto* replyHeader = reinterpret_cast<const icmp_echo_hdr*>(reply + ipHeaderLength);
                    validReply = replyHeader->type == ICMP_ER && replyHeader->code == 0 &&
                        ntohs(replyHeader->id) == identifier && ntohs(replyHeader->seqno) == sequence;
                    ttl = reply[8];
                }
            }

            if (validReply) {
                ++received;
                totalTime += elapsed;
                minimumTime = min(minimumTime, elapsed);
                maximumTime = max(maximumTime, elapsed);

                char sourceAddress[INET_ADDRSTRLEN]{};
                inet_ntoa_r(source.sin_addr, sourceAddress, sizeof(sourceAddress));
                Write(
                    client,
                    "               | " + String(PingPacketSize) + " bytes from " + String(sourceAddress) +
                        ": icmp_seq=" + String(sequence + 1) + " ttl=" + String(ttl) + " time=" + String(elapsed) + " ms\r\n"
                );
            } else {
                Write(client, "               | Request timeout for icmp_seq=" + String(sequence + 1) + "\r\n");
            }
        }

        if (sequence + 1 < count && client.connected()) delay(RequestIntervalMs);
    }

    lwip_close(socketDescriptor);

    const uint32_t lost = transmitted - received;
    const uint32_t lossPercentage = transmitted == 0 ? 0 : (lost * 100U) / transmitted;
    String result = "\r\nStatistics     | Destination: " + destination + "\r\n";
    result += "               | Transmitted: " + String(transmitted) + " packets\r\n";
    result += "               | Received: " + String(received) + " packets\r\n";
    result += "               | Lost: " + String(lost) + " (" + String(lossPercentage) + "% packet loss)\r\n";
    if (received > 0) {
        result += "               | RTT min/avg/max: " + String(minimumTime) + "/" +
            String(totalTime / received) + "/" + String(maximumTime) + " ms\r\n";
    }
    Write(client, result);
}
