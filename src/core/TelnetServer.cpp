#include "TelnetServer.h"

#include "Defaults.h"
#include "Globals.h"

namespace {
    constexpr const char* GuestUser = "guest";
    constexpr const char* Prompt = "> ";
    constexpr size_t FormatLabelWidth = 15;
}

String telnetserver::FormatPrefix(String label) {
    label.trim();
    if (label.length() > FormatLabelWidth) label.remove(FormatLabelWidth);
    while (label.length() < FormatLabelWidth) label += ' ';
    return label + "| ";
}

String telnetserver::FormatContinuationPrefix() {
    String prefix;
    prefix.reserve(FormatLabelWidth + 2);
    for (size_t index = 0; index < FormatLabelWidth; ++index) prefix += ' ';
    prefix += "| ";
    return prefix;
}

String telnetserver::FormatLine(const String& label, const String& text) {
    return FormatPrefix(label) + text + "\r\n";
}

String telnetserver::FormatBlock(const String& label, const String& text) {
    String output;
    output.reserve(text.length() + 32);
    const String firstPrefix = FormatPrefix(label);
    const String continuation = FormatContinuationPrefix();
    size_t offset = 0;
    bool first = true;

    while (offset < text.length()) {
        const int newline = text.indexOf('\n', offset);
        const size_t end = newline < 0 ? text.length() : static_cast<size_t>(newline);
        size_t contentEnd = end;
        if (contentEnd > offset && text[contentEnd - 1] == '\r') --contentEnd;
        output += first ? firstPrefix : continuation;
        output += text.substring(offset, contentEnd);
        output += "\r\n";
        first = false;
        if (newline < 0) break;
        offset = static_cast<size_t>(newline) + 1;
    }

    if (first) output = firstPrefix + "\r\n";
    return output;
}

telnetserver::telnetserver() noexcept : pMutex(xSemaphoreCreateMutexStatic(&pMutexStorage)) {
    configASSERT(pMutex != nullptr);
    pPort = Defaults.TelnetServer.Port;
    pEnabled = Defaults.TelnetServer.Enabled;
    pIdleTimeoutMs = Defaults.TelnetServer.IdleTimeoutMs;
    pMaxSessions = Defaults.TelnetServer.MaxSessions;
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

void telnetserver::IdleTimeout(uint32_t valueMs) noexcept {
    Lock lock(pMutex);
    if (lock.IsLocked()) pIdleTimeoutMs = valueMs;
}

uint32_t telnetserver::IdleTimeout() const noexcept {
    Lock lock(pMutex);
    return lock.IsLocked() ? pIdleTimeoutMs : 0;
}

void telnetserver::MaxSessions(size_t value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    if (value < MIN_SESSIONS) value = MIN_SESSIONS;
    if (value > MAX_SESSIONS) value = MAX_SESSIONS;
    pMaxSessions = value;
}

size_t telnetserver::MaxSessions() const noexcept {
    Lock lock(pMutex);
    return lock.IsLocked() ? pMaxSessions : 0;
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
        client.write(FormatLine("Session", "Closed.").c_str());
        Session* session = FindSession(client);
        if (session != nullptr) CloseSession(*session);
        else client.stop();
    });

    (void)OnCommand("sessions", "Show current Telnet sessions\r\n\r\nsessions", [this](WiFiClient& client, String*) {
        bool first = true;
        for (const Session& session : pSessions) {
            if (!session.active) continue;
            const String line = (first ? FormatPrefix("Sessions") : FormatContinuationPrefix()) +
                session.user + "@" + session.client.remoteIP().toString() + ":" + String(session.client.remotePort()) +
                (session.admin ? " - Admin" : "") + "\r\n";
            client.write(reinterpret_cast<const uint8_t*>(line.c_str()), line.length());
            first = false;
        }
        if (first) client.write(FormatLine("Sessions", "No active sessions.").c_str());
    });

    (void)OnCommand("whoami", "Show information about current user\r\n\r\nwhoami", [this](WiFiClient& client, String*) {
        Session* session = FindSession(client);
        if (session == nullptr) return;
        const String line = FormatLine(
            "Identity",
            session->user + "@" + session->client.remoteIP().toString() + ":" + String(session->client.remotePort()) +
                (session->admin ? " - Admin" : "")
        );
        client.write(reinterpret_cast<const uint8_t*>(line.c_str()), line.length());
    });

    (void)OnCommand("echo", "Display a text message\r\n\r\necho message", [](WiFiClient& client, String* parameters) {
        String output;
        for (size_t index = 0; index < MAX_COMMAND_PARAMETERS; ++index) {
            if (parameters[index].isEmpty()) continue;
            if (!output.isEmpty()) output += ' ';
            output += parameters[index];
        }
        output = FormatLine("Echo", output);
        client.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());
    });

    (void)OnCommand("help", "Show available commands\r\n\r\nhelp [command]", [this](WiFiClient& client, String* parameters) {
        if (!parameters[0].isEmpty()) {
            for (size_t index = 0; index < pCommandCount; ++index) {
                if (pCommands[index].name == parameters[0]) {
                    const String output = FormatBlock("Help", pCommands[index].help);
                    client.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());
                    return;
                }
            }
            const String output = FormatLine("Help", "Invalid command: " + parameters[0]);
            client.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());
            return;
        }

        String commandNames[MAX_COMMANDS];
        size_t maximumNameLength = 0;
        for (size_t index = 0; index < pCommandCount; ++index) {
            commandNames[index] = pCommands[index].name;
            maximumNameLength = max(maximumNameLength, commandNames[index].length());
        }

        for (size_t index = 1; index < pCommandCount; ++index) {
            const String current = commandNames[index];
            size_t destination = index;
            while (destination > 0 && commandNames[destination - 1].compareTo(current) > 0) {
                commandNames[destination] = commandNames[destination - 1];
                --destination;
            }
            commandNames[destination] = current;
        }

        constexpr size_t CommandsPerLine = 10;
        const size_t columnWidth = maximumNameLength + 1;
        String commands;
        commands.reserve((pCommandCount * columnWidth) + ((pCommandCount / CommandsPerLine) * 2));
        for (size_t index = 0; index < pCommandCount; ++index) {
            commands += commandNames[index];
            for (size_t padding = commandNames[index].length(); padding < columnWidth; ++padding) commands += ' ';

            if ((index + 1) % CommandsPerLine == 0 && index + 1 < pCommandCount) commands += "\r\n";
        }

        const String output = FormatBlock("Help", commands) + "\r\n" +
            FormatLine("", "Use . to repeat the last command.");
        client.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());
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

    const size_t maxSessions = MaxSessions();
    Session* availableSession = nullptr;
    for (size_t index = 0; index < maxSessions; ++index) {
        if (!pSessions[index].active) {
            availableSession = &pSessions[index];
            break;
        }
    }

    if (availableSession == nullptr) {
        client.write(FormatLine("Telnet", "Server busy.").c_str());
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
    session.lastActivityAt = xTaskGetTickCount();
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
    const TickType_t now = xTaskGetTickCount();
    // A value of 0 disables the idle timeout entirely.
    const uint32_t idleTimeoutMs = IdleTimeout();

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
            session.lastActivityAt = now;
            ProcessByte(session, static_cast<uint8_t>(value));
            ++processed;
            if (!session.client.connected()) break;
        }

        if (!session.client.connected()) {
            CloseSession(session);
            continue;
        }

        if (idleTimeoutMs != 0 && static_cast<TickType_t>(now - session.lastActivityAt) >= pdMS_TO_TICKS(idleTimeoutMs)) {
            const String message = FormatLine("Session", "Closed due to inactivity.");
            session.client.write(reinterpret_cast<const uint8_t*>(message.c_str()), message.length());
            CloseSession(session);
        }
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
        const String output = FormatLine("CLI", "Invalid command: " + commandName) + "\r\n";
        session.client.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());
    } else if (command->admin && !session.admin) {
        const String output = FormatLine("CLI", "Permission denied.") + "\r\n";
        session.client.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());
    } else if (parameters[0] == "-h" || parameters[0] == "-?" || parameters[0] == "--help") {
        const String output = FormatBlock("Help", command->help) + "\r\n";
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
