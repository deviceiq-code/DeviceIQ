#include "TelnetServer.h"

#include <cstdlib>

#include "Defaults.h"
#include "Globals.h"
#include "NetworkDiagnostics.h"

namespace {
    constexpr const char* GuestUser = "guest";
    constexpr const char* Prompt = "> ";
}

telnetserver::telnetserver() noexcept : pMutex(xSemaphoreCreateMutexStatic(&pMutexStorage)) {
    configASSERT(pMutex != nullptr);
    pPort = Defaults.TelnetServer.Port;
    pEnabled = Defaults.TelnetServer.Enabled;
    RegisterBuiltInCommands();
}

telnetserver::~telnetserver() {
    Stop();
}

bool telnetserver::Start() {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return false;
    if (!pEnabled || pTaskHandle != nullptr) return true;

    return xTaskCreate(TaskEntry, "TelnetServer", TASK_STACK_SIZE, this, TASK_PRIORITY, &pTaskHandle) == pdPASS;
}

void telnetserver::Stop() {
    TaskHandle_t task = nullptr;
    {
        Lock lock(pMutex);
        if (!lock.IsLocked()) return;
        task = pTaskHandle;
    }

    if (task == nullptr || task == xTaskGetCurrentTaskHandle()) return;
    xTaskNotify(task, STOP_NOTIFICATION, eSetBits);

    const TickType_t startedAt = xTaskGetTickCount();
    while ((xTaskGetTickCount() - startedAt) < pdMS_TO_TICKS(1000)) {
        {
            Lock lock(pMutex);
            if (!lock.IsLocked() || pTaskHandle == nullptr) return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void telnetserver::Enabled(bool value) noexcept {
    Lock lock(pMutex);
    if (lock.IsLocked() && pTaskHandle == nullptr) pEnabled = value;
}

bool telnetserver::Enabled() const noexcept {
    Lock lock(pMutex);
    return lock.IsLocked() && pEnabled;
}

void telnetserver::Port(uint16_t value) noexcept {
    Lock lock(pMutex);
    if (lock.IsLocked() && pTaskHandle == nullptr) pPort = value == 0 ? Defaults.TelnetServer.Port : value;
}

uint16_t telnetserver::Port() const noexcept {
    Lock lock(pMutex);
    return lock.IsLocked() ? pPort : 0;
}

void telnetserver::WelcomeMessage(String value) {
    value.trim();
    Lock lock(pMutex);
    if (lock.IsLocked() && pTaskHandle == nullptr) pWelcomeMessage = std::move(value);
}

String telnetserver::WelcomeMessage() const {
    Lock lock(pMutex);
    return lock.IsLocked() ? pWelcomeMessage : String();
}

void telnetserver::OnSessionBegin(session_callback_t callback) {
    Lock lock(pMutex);
    if (lock.IsLocked() && pTaskHandle == nullptr) pOnSessionBegin = std::move(callback);
}

void telnetserver::OnSessionEnd(session_callback_t callback) {
    Lock lock(pMutex);
    if (lock.IsLocked() && pTaskHandle == nullptr) pOnSessionEnd = std::move(callback);
}

bool telnetserver::OnCommand(String command, String helpMessage, command_callback_t callback, bool admin) {
    command.trim();
    command.toLowerCase();
    if (command.isEmpty() || !callback) return false;

    Lock lock(pMutex);
    if (!lock.IsLocked() || pTaskHandle != nullptr || pCommandCount >= MAX_COMMANDS) return false;

    for (size_t index = 0; index < pCommandCount; ++index) {
        if (pCommands[index].name == command) return false;
    }

    Command& destination = pCommands[pCommandCount++];
    destination.name = std::move(command);
    destination.help = std::move(helpMessage);
    destination.callback = std::move(callback);
    destination.admin = admin;
    return true;
}

bool telnetserver::SetSessionIdentity(WiFiClient& client, String username, bool admin) {
    username.trim();
    if (username.isEmpty() || xTaskGetCurrentTaskHandle() != pTaskHandle) return false;

    Session* session = FindSession(client);
    if (session == nullptr) return false;

    session->user = std::move(username);
    session->admin = admin;
    return true;
}

bool telnetserver::IsSessionAdmin(WiFiClient& client) {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return false;

    Session* session = FindSession(client);
    return session != nullptr && session->admin;
}

bool telnetserver::SessionInformation(WiFiClient& client, SessionInfo& info) {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return false;
    Session* session = FindSession(client);
    if (session == nullptr) return false;
    info = GetSessionInfo(*session);
    return true;
}

void telnetserver::RegisterBuiltInCommands() {
    (void)OnCommand("clear", "Clear terminal screen\r\n\r\nclear", [](WiFiClient& client, String*) {
        client.write("\x1B[2J\x1B[H");
    });

    (void)OnCommand("exit", "Close session and exit terminal\r\n\r\nexit", [this](WiFiClient& client, String*) {
        client.write("Session closed.\r\n");
        Session* session = FindSession(client);
        if (session != nullptr) CloseSession(*session);
        else client.stop();
    });

    (void)OnCommand("sessions", "Show current Telnet sessions\r\n\r\nsessions", [this](WiFiClient& client, String*) {
        client.write("Current sessions:\r\n\r\n");
        for (const Session& session : pSessions) {
            if (!session.active) continue;
            const String line = session.user + "@" + session.client.remoteIP().toString() + ":" + String(session.client.remotePort()) + (session.admin ? " - Admin" : "") + "\r\n";
            client.write(reinterpret_cast<const uint8_t*>(line.c_str()), line.length());
        }
    });

    (void)OnCommand("whoami", "Show information about current user\r\n\r\nwhoami", [this](WiFiClient& client, String*) {
        Session* session = FindSession(client);
        if (session == nullptr) return;
        const String line = session->user + "@" + session->client.remoteIP().toString() + ":" + String(session->client.remotePort()) + (session->admin ? " - Admin" : "") + "\r\n";
        client.write(reinterpret_cast<const uint8_t*>(line.c_str()), line.length());
    });

    (void)OnCommand("echo", "Display a text message\r\n\r\necho message", [](WiFiClient& client, String* parameters) {
        String output;
        for (size_t index = 0; index < MAX_COMMAND_PARAMETERS; ++index) {
            if (parameters[index].isEmpty()) continue;
            if (!output.isEmpty()) output += ' ';
            output += parameters[index];
        }
        output += "\r\n";
        client.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());
    });

    (void)OnCommand(
        "ping",
        "Ping an IP address or host\r\n\r\nping [destination] [-n count]\r\n"
        "count must be between 1 and 20 (default: 4)",
        [](WiFiClient& client, String* parameters) {
            if (parameters[0].isEmpty()) {
                client.write("Ping           | Error: Missing destination\r\n");
                return;
            }

            uint16_t count = NetworkDiagnostics::DefaultPingCount;
            if (!parameters[1].isEmpty()) {
                if (!parameters[1].equalsIgnoreCase("-n") || parameters[2].isEmpty() || !parameters[3].isEmpty()) {
                    client.write("Usage: ping [destination] [-n count]\r\n");
                    return;
                }

                char* end = nullptr;
                const long parsed = std::strtol(parameters[2].c_str(), &end, 10);
                if (end == parameters[2].c_str() || *end != '\0' || parsed < 1 || parsed > NetworkDiagnostics::MaximumPingCount) {
                    client.write("Ping           | Error: count must be between 1 and 20\r\n");
                    return;
                }
                count = static_cast<uint16_t>(parsed);
            }

            NetworkDiagnostics::Ping(client, parameters[0], count);
        }
    );

    (void)OnCommand("help", "Show available commands\r\n\r\nhelp [command]", [this](WiFiClient& client, String* parameters) {
        if (!parameters[0].isEmpty()) {
            for (size_t index = 0; index < pCommandCount; ++index) {
                if (pCommands[index].name == parameters[0]) {
                    const String output = pCommands[index].help + "\r\n";
                    client.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());
                    return;
                }
            }
            const String output = parameters[0] + " - Invalid command.\r\n";
            client.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());
            return;
        }

        client.write("Available commands:\r\n\r\n");
        for (size_t index = 0; index < pCommandCount; ++index) {
            client.write(reinterpret_cast<const uint8_t*>(pCommands[index].name.c_str()), pCommands[index].name.length());
            client.write(index + 1 == pCommandCount ? "\r\n" : "  ");
        }
        client.write("\r\nUse . to repeat the last command.\r\n");
    });
}

void telnetserver::TaskEntry(void* parameter) {
    static_cast<telnetserver*>(parameter)->Task();
}

void telnetserver::Task() {
    uint16_t port = 0;
    {
        Lock lock(pMutex);
        if (!lock.IsLocked()) {
            vTaskDelete(nullptr);
            return;
        }
        port = pPort;
    }

    WiFiServer server(port);
    bool serverStarted = false;

    while (true) {
        uint32_t notifications = 0;
        xTaskNotifyWait(0, UINT32_MAX, &notifications, 0);
        if ((notifications & STOP_NOTIFICATION) != 0) break;

        const network::APMode networkMode = Network.ConnectionMode();

        if (!serverStarted && networkMode != network::APMode::Offline) {
            server.begin();
            server.setNoDelay(true);
            serverStarted = true;
        }

        if (serverStarted) {
            if (networkMode != network::APMode::Offline) AcceptClient(server);
            PollSessions();
        }

        vTaskDelay(TASK_DELAY);
    }

    for (Session& session : pSessions) CloseSession(session);
    if (serverStarted) server.stop();

    {
        Lock lock(pMutex);
        if (lock.IsLocked()) pTaskHandle = nullptr;
    }

    vTaskDelete(nullptr);
}

void telnetserver::AcceptClient(WiFiServer& server) {
    WiFiClient client = server.available();
    if (!client) return;

    Session* availableSession = nullptr;
    for (Session& session : pSessions) {
        if (!session.active) {
            availableSession = &session;
            break;
        }
    }

    if (availableSession == nullptr) {
        client.write("Server busy.\r\n");
        client.stop();
        return;
    }

    Session& session = *availableSession;
    session.client = client;
    session.client.setNoDelay(true);
    session.input.clear();
    session.input.reserve(MAX_INPUT_LENGTH);
    session.lastInput.clear();
    session.user = GuestUser;
    session.parserState = ParserState::Data;
    session.negotiationCommand = 0;
    session.admin = false;
    session.lastWasCarriageReturn = false;
    session.active = true;

    session_callback_t callback;
    String welcomeMessage;
    {
        Lock lock(pMutex);
        if (lock.IsLocked()) {
            callback = pOnSessionBegin;
            welcomeMessage = pWelcomeMessage;
        }
    }

    if (callback) callback(session.client, GetSessionInfo(session));
    const String output = "\r\n" + welcomeMessage + "\r\n\r\n";
    session.client.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());
    WritePrompt(session);
}

void telnetserver::PollSessions() {
    for (Session& session : pSessions) {
        if (!session.active) continue;
        if (!session.client.connected()) {
            CloseSession(session);
            continue;
        }

        size_t processed = 0;
        while (session.client.available() > 0 && processed < MAX_BYTES_PER_CYCLE) {
            const int value = session.client.read();
            if (value < 0) break;
            ProcessByte(session, static_cast<uint8_t>(value));
            ++processed;
            if (!session.client.connected()) break;
        }

        if (!session.client.connected()) CloseSession(session);
    }
}

void telnetserver::ProcessByte(Session& session, uint8_t value) {
    if (session.parserState != ParserState::Data || value == TELNET_IAC) {
        ProcessNegotiation(session, value);
        return;
    }

    if (value == 8 || value == 127) {
        if (!session.input.isEmpty()) session.input.remove(session.input.length() - 1);
        session.lastWasCarriageReturn = false;
        return;
    }

    if (value == '\r' || value == '\n') {
        if (value == '\n' && session.lastWasCarriageReturn) {
            session.lastWasCarriageReturn = false;
            return;
        }

        session.lastWasCarriageReturn = value == '\r';
        session.client.write("\r\n");
        if (!session.input.isEmpty()) {
            ProcessLine(session);
            session.input.clear();
        } else {
            WritePrompt(session);
        }
        return;
    }

    session.lastWasCarriageReturn = false;
    if (value >= 32 && value <= 126 && session.input.length() < MAX_INPUT_LENGTH) {
        session.input += static_cast<char>(value);
    }
}

void telnetserver::ProcessNegotiation(Session& session, uint8_t value) {
    switch (session.parserState) {
        case ParserState::Data:
            if (value == TELNET_IAC) session.parserState = ParserState::Command;
            break;

        case ParserState::Command:
            if (value == TELNET_IAC) {
                session.parserState = ParserState::Data;
            } else if (value == TELNET_WILL || value == TELNET_WONT || value == TELNET_DO || value == TELNET_DONT) {
                session.negotiationCommand = value;
                session.parserState = ParserState::Option;
            } else if (value == TELNET_SB) {
                session.parserState = ParserState::Subnegotiation;
            } else {
                session.parserState = ParserState::Data;
            }
            break;

        case ParserState::Option: {
            uint8_t response[3] = {TELNET_IAC, 0, value};
            if (session.negotiationCommand == TELNET_WILL) response[1] = TELNET_DONT;
            if (session.negotiationCommand == TELNET_DO) response[1] = TELNET_WONT;
            if (response[1] != 0) session.client.write(response, sizeof(response));
            session.parserState = ParserState::Data;
            break;
        }

        case ParserState::Subnegotiation:
            if (value == TELNET_IAC) session.parserState = ParserState::SubnegotiationIAC;
            break;

        case ParserState::SubnegotiationIAC:
            session.parserState = value == TELNET_SE ? ParserState::Data : ParserState::Subnegotiation;
            break;
    }
}

void telnetserver::ProcessLine(Session& session) {
    String input = session.input;
    input.trim();

    if (input == ".") input = session.lastInput;
    else if (!input.isEmpty()) session.lastInput = input;

    if (input.isEmpty()) {
        WritePrompt(session);
        return;
    }

    const int firstSpace = input.indexOf(' ');
    String commandName = firstSpace < 0 ? input : input.substring(0, firstSpace);
    String remaining = firstSpace < 0 ? String() : input.substring(firstSpace + 1);
    commandName.toLowerCase();
    remaining.trim();

    String parameters[MAX_COMMAND_PARAMETERS];
    for (size_t index = 0; index < MAX_COMMAND_PARAMETERS && !remaining.isEmpty(); ++index) {
        const int separator = remaining.indexOf(' ');
        parameters[index] = separator < 0 ? remaining : remaining.substring(0, separator);
        remaining = separator < 0 ? String() : remaining.substring(separator + 1);
        remaining.trim();
    }

    Command* command = nullptr;
    for (size_t index = 0; index < pCommandCount; ++index) {
        if (pCommands[index].name == commandName) {
            command = &pCommands[index];
            break;
        }
    }

    if (command == nullptr) {
        const String output = commandName + " - Invalid command.\r\n\r\n";
        session.client.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());
    } else if (command->admin && !session.admin) {
        session.client.write("Permission denied.\r\n\r\n");
    } else if (parameters[0] == "-h" || parameters[0] == "-?" || parameters[0] == "--help") {
        const String output = command->help + "\r\n\r\n";
        session.client.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());
    } else {
        command->callback(session.client, parameters);
        if (command->name != "clear" && session.client.connected()) session.client.write("\r\n");
    }

    if (session.client.connected()) WritePrompt(session);
}

void telnetserver::CloseSession(Session& session) {
    if (!session.active) return;

    session_callback_t callback;
    {
        Lock lock(pMutex);
        if (lock.IsLocked()) callback = pOnSessionEnd;
    }

    if (callback) callback(session.client, GetSessionInfo(session));
    session.client.stop();
    session.input.clear();
    session.lastInput.clear();
    session.user.clear();
    session.active = false;
    session.admin = false;
}

void telnetserver::WritePrompt(Session& session) {
    const String prompt = session.user + " " + Prompt;
    session.client.write(reinterpret_cast<const uint8_t*>(prompt.c_str()), prompt.length());
}

telnetserver::Session* telnetserver::FindSession(WiFiClient& client) {
    for (Session& session : pSessions) {
        if (session.active && session.client.remoteIP() == client.remoteIP() && session.client.remotePort() == client.remotePort()) return &session;
    }
    return nullptr;
}

telnetserver::SessionInfo telnetserver::GetSessionInfo(const Session& session) const {
    SessionInfo info;
    info.remoteIP = session.client.remoteIP();
    info.remotePort = session.client.remotePort();
    info.user = session.user;
    info.admin = session.admin;
    return info;
}
