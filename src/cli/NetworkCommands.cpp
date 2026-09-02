#include "NetworkCommands.h"

#include <Arduino.h>
#include <cstdlib>

#include <lwip/inet.h>
#include <lwip/inet_chksum.h>
#include <lwip/prot/icmp.h>
#include <lwip/sockets.h>

#include "CommandHelpers.h"
#include "core/Globals.h"

namespace {
    using cli::AppendSetting;
    using cli::BoolValue;
    using cli::JoinParameters;
    using cli::ParseBool;
    using cli::ParseUInt16;
    using cli::PasswordState;

    struct NetworkSettingsSnapshot {
        bool dhcpClient;
        String hostname;
        IPAddress ipAddress;
        IPAddress gateway;
        IPAddress netmask;
        IPAddress dns[2];
        String ssid;
        String passphrase;
        uint16_t connectionTimeout;
        bool reconnectEnabled;
        uint16_t reconnectInitialInterval;
        uint16_t reconnectMaximumInterval;
        bool fallbackAPEnabled;
        String fallbackAPSSID;
        String fallbackAPPassword;
        uint16_t fallbackAPRetention;
    };

    String ModeName(network::APMode mode) {
        switch (mode) {
            case network::APMode::WifiClient: return "WiFi client";
            case network::APMode::SoftAP: return "fallback AP";
            default: return "offline";
        }
    }

    bool ParseIP(const String& value, IPAddress& parsed) {
        return !value.isEmpty() && parsed.fromString(value);
    }

    bool IsBroadcast(const IPAddress& address) {
        return address == IPAddress(255, 255, 255, 255);
    }

    bool IsMulticast(const IPAddress& address) {
        return address[0] >= 224 && address[0] <= 239;
    }

    bool IsValidHostAddress(const IPAddress& address, bool allowZero) {
        return (allowZero || address != IPAddress(0, 0, 0, 0)) && !IsBroadcast(address) && !IsMulticast(address);
    }

    bool IsValidNetmask(const IPAddress& address) {
        const uint32_t mask = (uint32_t(address[0]) << 24) | (uint32_t(address[1]) << 16) |
            (uint32_t(address[2]) << 8) | uint32_t(address[3]);
        return mask != 0 && ((mask | (mask - 1)) == 0xFFFFFFFFu);
    }

    bool IsPrintableASCII(const String& value) {
        for (size_t index = 0; index < value.length(); ++index) {
            const unsigned char character = static_cast<unsigned char>(value[index]);
            if (character < 0x20 || character > 0x7E) return false;
        }
        return true;
    }

    bool IsHex64(const String& value) {
        if (value.length() != 64) return false;
        for (size_t index = 0; index < value.length(); ++index) {
            const char character = value[index];
            if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
                (character >= 'A' && character <= 'F'))) return false;
        }
        return true;
    }

    bool IsValidStationPassword(const String& value) {
        return value.isEmpty() || IsHex64(value) ||
            (value.length() >= 8 && value.length() <= 63 && IsPrintableASCII(value));
    }

    bool IsValidFallbackPassword(const String& value) {
        return value.isEmpty() ||
            (value.length() >= 8 && value.length() <= 63 && IsPrintableASCII(value));
    }

    NetworkSettingsSnapshot CaptureSettings() {
        return {
            Settings.Network.DHCPClient(),
            Settings.Network.Hostname(),
            Settings.Network.IP_Address(),
            Settings.Network.Gateway(),
            Settings.Network.Netmask(),
            { Settings.Network.DNS(0), Settings.Network.DNS(1) },
            Settings.Network.SSID(),
            Settings.Network.Passphrase(),
            Settings.Network.ConnectionTimeout(),
            Settings.Network.ReconnectEnabled(),
            Settings.Network.ReconnectInitialInterval(),
            Settings.Network.ReconnectMaximumInterval(),
            Settings.Network.FallbackAPEnabled(),
            Settings.Network.FallbackAPSSID(),
            Settings.Network.FallbackAPPassword(),
            Settings.Network.FallbackAPRetention()
        };
    }

    void RestoreSettings(const NetworkSettingsSnapshot& snapshot) {
        Settings.Network.DHCPClient(snapshot.dhcpClient);
        Settings.Network.Hostname(snapshot.hostname);
        Settings.Network.IP_Address(snapshot.ipAddress.toString());
        Settings.Network.Gateway(snapshot.gateway.toString());
        Settings.Network.Netmask(snapshot.netmask.toString());
        Settings.Network.DNS(0, snapshot.dns[0].toString());
        Settings.Network.DNS(1, snapshot.dns[1].toString());
        Settings.Network.SSID(snapshot.ssid);
        Settings.Network.Passphrase(snapshot.passphrase);
        Settings.Network.ConnectionTimeout(snapshot.connectionTimeout);
        Settings.Network.ReconnectEnabled(snapshot.reconnectEnabled);
        Settings.Network.ReconnectInitialInterval(snapshot.reconnectInitialInterval);
        Settings.Network.ReconnectMaximumInterval(snapshot.reconnectMaximumInterval);
        Settings.Network.FallbackAPEnabled(snapshot.fallbackAPEnabled);
        Settings.Network.FallbackAPSSID(snapshot.fallbackAPSSID);
        Settings.Network.FallbackAPPassword(snapshot.fallbackAPPassword);
        Settings.Network.FallbackAPRetention(snapshot.fallbackAPRetention);
    }

    void ShowAll(String& output, bool isAdmin) {
        const network::APMode mode = Network.ConnectionMode();
        AppendSetting(output, "Mode", ModeName(mode));
        AppendSetting(output, "MAC", Network.MAC_Address());
        AppendSetting(output, "Current SSID", mode == network::APMode::Offline ? "unavailable" : Network.SSID());
        AppendSetting(output, "RSSI", mode == network::APMode::WifiClient ? String(Network.RSSI()) + " dBm" : "unavailable");
        AppendSetting(output, "Current IP", Network.IP_Address().toString());
        AppendSetting(output, "Current gateway", Network.Gateway().toString());
        AppendSetting(output, "Current netmask", Network.CurrentNetmask().toString());
        AppendSetting(output, "Current DNS 1", Network.CurrentDNS_Server(0).toString());
        AppendSetting(output, "Current DNS 2", Network.CurrentDNS_Server(1).toString());
        output += "\r\n";
        AppendSetting(output, "DHCP Client", BoolValue(Settings.Network.DHCPClient()));
        AppendSetting(output, "Hostname", Settings.Network.Hostname());
        AppendSetting(output, "IP Address", Settings.Network.IP_Address().toString());
        AppendSetting(output, "Gateway", Settings.Network.Gateway().toString());
        AppendSetting(output, "Netmask", Settings.Network.Netmask().toString());
        AppendSetting(output, "DNS 1", Settings.Network.DNS(0).toString());
        AppendSetting(output, "DNS 2", Settings.Network.DNS(1).toString());
        AppendSetting(output, "SSID", Settings.Network.SSID());
        AppendSetting(output, "Passphrase", isAdmin ? Settings.Network.Passphrase() : PasswordState(Settings.Network.Passphrase()));
        AppendSetting(output, "Connection Timeout", String(Settings.Network.ConnectionTimeout()) + " s");
        AppendSetting(output, "Reconnect Enabled", BoolValue(Settings.Network.ReconnectEnabled()));
        AppendSetting(output, "Reconnect Initial Interval", String(Settings.Network.ReconnectInitialInterval()) + " s");
        AppendSetting(output, "Reconnect Maximum Interval", String(Settings.Network.ReconnectMaximumInterval()) + " s");
        AppendSetting(output, "Fallback AP Enabled", BoolValue(Settings.Network.FallbackAPEnabled()));
        AppendSetting(output, "Fallback AP SSID", Settings.Network.FallbackAPSSID().isEmpty() ? "hostname" : Settings.Network.FallbackAPSSID());
        AppendSetting(output, "Fallback AP Password", isAdmin ? Settings.Network.FallbackAPPassword() : PasswordState(Settings.Network.FallbackAPPassword()));
        AppendSetting(output, "Fallback AP Retention", String(Settings.Network.FallbackAPRetention()) + " s");
    }

    bool SaveChange(const NetworkSettingsSnapshot& snapshot, const String& name, const String& value, String& output) {
        if (!Settings.Save()) {
            RestoreSettings(snapshot);
            output = "Unable to save network configuration. No changes were kept.\r\n";
            return false;
        }

        AppendSetting(output, name, value);
        output += "Configuration saved to " + String(Defaults.ConfigFileName) + ".\r\n";
        output += "Restart required to apply the change.\r\n";
        return true;
    }

    bool Usage(String& output) {
        output = "Usage: net [show|ssid|rssi|ip|hostname|dhcp|gateway|netmask|dns|passphrase|connection-timeout|reconnect|reconnect-initial|reconnect-maximum|fallback|fallback-ssid|fallback-password|fallback-retention]\r\n";
        return false;
    }

    bool IsNetworkMutation(String* parameters) noexcept {
        String command = parameters[0];
        command.toLowerCase();
        if (command == "dns") return !parameters[2].isEmpty();
        if (command == "show" || command == "rssi" || command.isEmpty()) return false;
        return !parameters[1].isEmpty();
    }

    bool ExecuteNetworkCommand(String* parameters, bool isAdmin, String& output) noexcept {
        output.clear();
        String command = parameters[0];
        command.toLowerCase();

        if (command.isEmpty() || command == "show") {
            if (!parameters[1].isEmpty()) return Usage(output);
            ShowAll(output, isAdmin);
            return true;
        }

        if (command == "rssi") {
            if (!parameters[1].isEmpty()) return Usage(output);
            output = Network.ConnectionMode() == network::APMode::WifiClient
                ? String(Network.RSSI()) + " dBm\r\n"
                : "Unavailable: WiFi station is not connected.\r\n";
            return true;
        }

        if (command == "ip" && parameters[1].isEmpty()) {
            AppendSetting(output, "Configured", Settings.Network.IP_Address().toString());
            AppendSetting(output, "Current", Network.IP_Address().toString());
            AppendSetting(output, "DHCP Client", BoolValue(Settings.Network.DHCPClient()));
            return true;
        }

        if (command == "dns" && parameters[1].isEmpty()) {
            AppendSetting(output, "DNS 1", Settings.Network.DNS(0).toString());
            AppendSetting(output, "DNS 2", Settings.Network.DNS(1).toString());
            return true;
        }

        if (command == "dns" && parameters[2].isEmpty()) {
            const int index = parameters[1].toInt();
            if ((index != 1 && index != 2) || parameters[1] != String(index)) {
                output = "Usage: net dns [1|2] [address]\r\n";
                return false;
            }
            AppendSetting(output, "DNS " + String(index), Settings.Network.DNS(index - 1).toString());
            return true;
        }

        if (parameters[1].isEmpty()) {
            if (command == "ssid") AppendSetting(output, "SSID", Settings.Network.SSID());
            else if (command == "hostname") AppendSetting(output, "Hostname", Settings.Network.Hostname());
            else if (command == "dhcp") AppendSetting(output, "DHCP Client", BoolValue(Settings.Network.DHCPClient()));
            else if (command == "gateway") AppendSetting(output, "Gateway", Settings.Network.Gateway().toString());
            else if (command == "netmask") AppendSetting(output, "Netmask", Settings.Network.Netmask().toString());
            else if (command == "passphrase") AppendSetting(output, "Passphrase", isAdmin ? Settings.Network.Passphrase() : PasswordState(Settings.Network.Passphrase()));
            else if (command == "connection-timeout") AppendSetting(output, "Connection Timeout", String(Settings.Network.ConnectionTimeout()) + " s");
            else if (command == "reconnect") AppendSetting(output, "Reconnect Enabled", BoolValue(Settings.Network.ReconnectEnabled()));
            else if (command == "reconnect-initial") AppendSetting(output, "Reconnect Initial Interval", String(Settings.Network.ReconnectInitialInterval()) + " s");
            else if (command == "reconnect-maximum") AppendSetting(output, "Reconnect Maximum Interval", String(Settings.Network.ReconnectMaximumInterval()) + " s");
            else if (command == "fallback") AppendSetting(output, "Fallback AP Enabled", BoolValue(Settings.Network.FallbackAPEnabled()));
            else if (command == "fallback-ssid") AppendSetting(output, "Fallback AP SSID", Settings.Network.FallbackAPSSID().isEmpty() ? "hostname" : Settings.Network.FallbackAPSSID());
            else if (command == "fallback-password") AppendSetting(output, "Fallback AP Password", isAdmin ? Settings.Network.FallbackAPPassword() : PasswordState(Settings.Network.FallbackAPPassword()));
            else if (command == "fallback-retention") AppendSetting(output, "Fallback AP Retention", String(Settings.Network.FallbackAPRetention()) + " s");
            else return Usage(output);
            return true;
        }

        const NetworkSettingsSnapshot snapshot = CaptureSettings();

        if (command == "ssid") {
            String value = JoinParameters(parameters, 1);
            if (value.equalsIgnoreCase("clear")) value.clear();
            if (value.length() > 32 || !IsPrintableASCII(value)) {
                output = "SSID must contain at most 32 printable ASCII characters, or use 'clear'.\r\n";
                return false;
            }
            Settings.Network.SSID(value);
            return SaveChange(snapshot, "SSID", Settings.Network.SSID().isEmpty() ? "not set" : Settings.Network.SSID(), output);
        }

        if (command == "hostname") {
            Settings.Network.Hostname(JoinParameters(parameters, 1));
            return SaveChange(snapshot, "Hostname", Settings.Network.Hostname(), output);
        }

        if (command == "passphrase" || command == "fallback-password") {
            String value = JoinParameters(parameters, 1);
            if (value.equalsIgnoreCase("clear")) value.clear();
            const bool valid = command == "passphrase" ? IsValidStationPassword(value) : IsValidFallbackPassword(value);
            if (!valid) {
                output = command == "passphrase"
                    ? "Passphrase must be empty, 8-63 printable ASCII characters, or 64 hexadecimal characters. Use 'clear' to empty it.\r\n"
                    : "Fallback AP password must be empty or 8-63 printable ASCII characters. Use 'clear' to empty it.\r\n";
                return false;
            }
            if (command == "passphrase") Settings.Network.Passphrase(value);
            else Settings.Network.FallbackAPPassword(value);
            // Reached only after the admin check for mutations, so it's safe
            // to echo the value that was just set.
            return SaveChange(snapshot, command == "passphrase" ? "Passphrase" : "Fallback AP Password", value.isEmpty() ? "not set" : value, output);
        }

        if (command == "fallback-ssid") {
            String value = JoinParameters(parameters, 1);
            if (value.equalsIgnoreCase("clear")) value.clear();
            if (value.length() > 32 || !IsPrintableASCII(value)) {
                output = "Fallback AP SSID must contain at most 32 printable ASCII characters, or use 'clear'.\r\n";
                return false;
            }
            Settings.Network.FallbackAPSSID(value);
            return SaveChange(snapshot, "Fallback AP SSID", value.isEmpty() ? "hostname" : value, output);
        }

        if (command == "dhcp" || command == "reconnect" || command == "fallback") {
            bool value = false;
            if (!ParseBool(parameters[1], value) || !parameters[2].isEmpty()) {
                output = "Value must be on or off.\r\n";
                return false;
            }
            if (command == "dhcp") Settings.Network.DHCPClient(value);
            else if (command == "reconnect") Settings.Network.ReconnectEnabled(value);
            else Settings.Network.FallbackAPEnabled(value);
            const String name = command == "dhcp" ? "DHCP Client" : (command == "reconnect" ? "Reconnect Enabled" : "Fallback AP Enabled");
            return SaveChange(snapshot, name, BoolValue(value), output);
        }

        if (command == "ip" || command == "gateway" || command == "netmask" || command == "dns") {
            const String source = command == "dns" ? parameters[2] : parameters[1];
            if (command != "dns" && !parameters[2].isEmpty()) {
                output = "Only one IPv4 address may be supplied.\r\n";
                return false;
            }
            IPAddress value;
            if (!ParseIP(source, value)) {
                output = "Invalid IPv4 address.\r\n";
                return false;
            }
            if (command == "netmask" && !IsValidNetmask(value)) {
                output = "Invalid IPv4 netmask.\r\n";
                return false;
            }
            if (command != "netmask" && !IsValidHostAddress(value, true)) {
                output = "Broadcast and multicast addresses are not allowed.\r\n";
                return false;
            }
            if (command == "ip") Settings.Network.IP_Address(source);
            else if (command == "gateway") Settings.Network.Gateway(source);
            else if (command == "netmask") Settings.Network.Netmask(source);
            else {
                const int index = parameters[1].toInt();
                if ((index != 1 && index != 2) || parameters[1] != String(index) || !parameters[3].isEmpty()) {
                    output = "Usage: net dns [1|2] [address]\r\n";
                    return false;
                }
                Settings.Network.DNS(index - 1, source);
            }
            const String name = command == "ip" ? "IP Address" : (command == "gateway" ? "Gateway" : (command == "netmask" ? "Netmask" : "DNS " + parameters[1]));
            return SaveChange(snapshot, name, value.toString(), output);
        }

        if (command == "connection-timeout" || command == "reconnect-initial" || command == "reconnect-maximum" || command == "fallback-retention") {
            uint16_t value = 0;
            const bool allowZero = command == "fallback-retention";
            if (!ParseUInt16(parameters[1], value, allowZero) || !parameters[2].isEmpty()) {
                output = "Value must be " + String(allowZero ? "0-65535" : "1-65535") + " seconds.\r\n";
                return false;
            }
            if (command == "connection-timeout") Settings.Network.ConnectionTimeout(value);
            else if (command == "reconnect-initial") Settings.Network.ReconnectInitialInterval(value);
            else if (command == "reconnect-maximum") Settings.Network.ReconnectMaximumInterval(value);
            else Settings.Network.FallbackAPRetention(value);
            const String name = command == "connection-timeout" ? "Connection Timeout" :
                (command == "reconnect-initial" ? "Reconnect Initial Interval" :
                    (command == "reconnect-maximum" ? "Reconnect Maximum Interval" : "Fallback AP Retention"));
            return SaveChange(snapshot, name, String(value) + " s", output);
        }

        return Usage(output);
    }

    constexpr uint16_t DefaultPingCount = 4;
    constexpr uint16_t MaximumPingCount = 20;
    constexpr size_t PingPacketSize = 32;
    constexpr uint32_t ReplyTimeoutMs = 1000;
    constexpr uint32_t RequestIntervalMs = 1000;

    void Write(WiFiClient& client, const String& text) {
        if (client.connected()) {
            client.write(reinterpret_cast<const uint8_t*>(text.c_str()), text.length());
        }
    }

    void PingNetworkDiagnostic(WiFiClient& client, const String& destination, uint16_t count) {
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
}

bool cli::RegisterNetworkCommands() {
    const bool pingRegistered = TelnetServer.OnCommand(
        "ping",
        "Ping an IP address or host\r\n\r\nping [destination] [-n count]\r\n"
        "count must be between 1 and 20 (default: 4)",
        [](WiFiClient& client, String* parameters) {
            if (parameters[0].isEmpty()) {
                client.write(telnetserver::FormatLine("Ping", "Error: Missing destination").c_str());
                return;
            }

            uint16_t count = DefaultPingCount;
            if (!parameters[1].isEmpty()) {
                if (!parameters[1].equalsIgnoreCase("-n") || parameters[2].isEmpty() || !parameters[3].isEmpty()) {
                    client.write(telnetserver::FormatLine("Ping", "Usage: ping [destination] [-n count]").c_str());
                    return;
                }

                char* end = nullptr;
                const long parsed = std::strtol(parameters[2].c_str(), &end, 10);
                if (end == parameters[2].c_str() || *end != '\0' || parsed < 1 || parsed > MaximumPingCount) {
                    client.write(telnetserver::FormatLine("Ping", "Error: count must be between 1 and 20").c_str());
                    return;
                }
                count = static_cast<uint16_t>(parsed);
            }

            PingNetworkDiagnostic(client, parameters[0], count);
        },
        false
    );

    const bool netRegistered = TelnetServer.OnCommand(
        "net",
        "Show and configure network settings\r\n\r\n"
        "net [show]\r\n"
        "net ssid [value|clear]\r\n"
        "net rssi\r\n"
        "net ip [address]\r\n"
        "net hostname [value]\r\n"
        "net dhcp [on|off]\r\n"
        "net gateway [address]\r\n"
        "net netmask [address]\r\n"
        "net dns [1|2] [address]\r\n"
        "net passphrase [value|clear]\r\n"
        "net connection-timeout [seconds]\r\n"
        "net reconnect [on|off]\r\n"
        "net reconnect-initial [seconds]\r\n"
        "net reconnect-maximum [seconds]\r\n"
        "net fallback [on|off]\r\n"
        "net fallback-ssid [value|clear]\r\n"
        "net fallback-password [value|clear]\r\n"
        "net fallback-retention [seconds]\r\n"
        "Omit a value to show it. Changes require an administrative session and a restart.",
        [](WiFiClient& client, String* parameters) {
            const bool mutation = IsNetworkMutation(parameters);
            if (mutation && !TelnetServer.IsSessionAdmin(client)) {
                Logger.Log("CLI network mutation denied for " + client.remoteIP().toString(), logger::LogLevels::Warning);
                client.write(telnetserver::FormatLine("Network", "Permission denied.").c_str());
                return;
            }

            String output;
            output.reserve(1024);
            const bool success = ExecuteNetworkCommand(parameters, TelnetServer.IsSessionAdmin(client), output);
            if (mutation) {
                telnetserver::SessionInfo session;
                const String identity = TelnetServer.SessionInformation(client, session) ? session.user : String("unknown");
                Logger.Log(
                    "CLI network mutation " + String(success ? "accepted" : "rejected") + " for " + identity + "@" +
                        client.remoteIP().toString() + ": net " + parameters[0],
                    success ? logger::LogLevels::Information : logger::LogLevels::Warning
                );
            }
            const String formatted = telnetserver::FormatBlock("Network", output);
            client.write(reinterpret_cast<const uint8_t*>(formatted.c_str()), formatted.length());
        },
        false
    );

    return pingRegistered && netRegistered;
}
