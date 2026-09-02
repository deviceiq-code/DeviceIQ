#include "HTTPServer.h"

#include <esp_random.h>

#include "core/Globals.h"
#include "core/Users.h"

namespace {
    const char* LoginFailureMessage(UserReturn result) {
        switch (result) {
            case UserReturn::InvalidCredentials: return "invalid credentials";
            case UserReturn::AuthenticationRateLimited: return "too many failed attempts, try again later";
            case UserReturn::SynchronizationError: return "authentication temporarily unavailable";
            default: return "authentication failed";
        }
    }

    String JsonEscaped(const String& value) {
        String result;
        result.reserve(value.length());
        for (size_t index = 0; index < value.length(); ++index) {
            const char character = value[index];
            if (character == '"' || character == '\\') result += '\\';
            result += character;
        }
        return result;
    }
}

httpserver::httpserver() noexcept : pMutex(xSemaphoreCreateMutexStatic(&pMutexStorage)) {
    configASSERT(pMutex != nullptr);
    pPort = Defaults.WebServer.Port;
    pEnabled = Defaults.WebServer.Enabled;
    pIdleTimeoutMs = Defaults.WebServer.IdleTimeoutMs;
    pMaxSessions = Defaults.WebServer.MaxSessions;
}

httpserver::~httpserver() {
    Stop();
}

bool httpserver::Start() {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return false;
    if (!pEnabled || pTaskHandle != nullptr) return true;

    return xTaskCreate(TaskEntry, "HTTPServer", TASK_STACK_SIZE, this, TASK_PRIORITY, &pTaskHandle) == pdPASS;
}

void httpserver::Stop() {
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

void httpserver::Enabled(bool value) noexcept {
    Lock lock(pMutex);
    if (lock.IsLocked() && pTaskHandle == nullptr) pEnabled = value;
}

bool httpserver::Enabled() const noexcept {
    Lock lock(pMutex);
    return lock.IsLocked() && pEnabled;
}

void httpserver::Port(uint16_t value) noexcept {
    Lock lock(pMutex);
    if (lock.IsLocked() && pTaskHandle == nullptr) pPort = value == 0 ? Defaults.WebServer.Port : value;
}

uint16_t httpserver::Port() const noexcept {
    Lock lock(pMutex);
    return lock.IsLocked() ? pPort : 0;
}

void httpserver::IdleTimeout(uint32_t valueMs) noexcept {
    Lock lock(pMutex);
    if (lock.IsLocked()) pIdleTimeoutMs = valueMs;
}

uint32_t httpserver::IdleTimeout() const noexcept {
    Lock lock(pMutex);
    return lock.IsLocked() ? pIdleTimeoutMs : 0;
}

void httpserver::MaxSessions(size_t value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    if (value < MIN_SESSIONS) value = MIN_SESSIONS;
    if (value > MAX_SESSIONS) value = MAX_SESSIONS;
    pMaxSessions = value;
}

size_t httpserver::MaxSessions() const noexcept {
    Lock lock(pMutex);
    return lock.IsLocked() ? pMaxSessions : 0;
}

void httpserver::TaskEntry(void* parameter) {
    static_cast<httpserver*>(parameter)->Task();
}

void httpserver::Task() {
    uint16_t port = 80;
    {
        Lock lock(pMutex);
        if (!lock.IsLocked()) {
            vTaskDelete(nullptr);
            return;
        }
        port = pPort;
    }

    WebServer server(port);
    RegisterRoutes(server);

    bool serverStarted = false;

    while (true) {
        uint32_t notifications = 0;
        xTaskNotifyWait(0, UINT32_MAX, &notifications, 0);
        if ((notifications & STOP_NOTIFICATION) != 0) break;

        const network::APMode networkMode = Network.ConnectionMode();
        if (!serverStarted && networkMode != network::APMode::Offline) {
            server.begin();
            serverStarted = true;
        }

        if (serverStarted) server.handleClient();

        vTaskDelay(TASK_DELAY);
    }

    if (serverStarted) server.stop();

    {
        Lock lock(pMutex);
        if (lock.IsLocked()) pTaskHandle = nullptr;
    }

    vTaskDelete(nullptr);
}

void httpserver::RegisterRoutes(WebServer& server) {
    // Only "Authorization" is collected by default; the session cookie needs
    // to be requested explicitly, before begin().
    static const char* HeaderKeys[] = {"Cookie"};
    server.collectHeaders(HeaderKeys, 1);

    server.on("/", HTTP_GET, [this, &server]() { HandleIndex(server); });
    server.on("/dashboard.html", HTTP_GET, [this, &server]() { HandleDashboard(server); });
    server.on("/config.html", HTTP_GET, [this, &server]() { HandleConfig(server); });
    server.on("/style.css", HTTP_GET, [this, &server]() { HandleStyle(server); });
    server.on("/api/login", HTTP_POST, [this, &server]() { HandleLoginPost(server); });
    server.on("/api/logout", HTTP_POST, [this, &server]() { HandleLogoutPost(server); });
    server.on("/api/session", HTTP_GET, [this, &server]() { HandleSessionGet(server); });
    server.onNotFound([this, &server]() { HandleNotFound(server); });
}

void httpserver::HandleIndex(WebServer& server) {
    ServeFile(server, "/index.html", "text/html");
}

void httpserver::HandleDashboard(WebServer& server) {
    ServeProtectedFile(server, "/dashboard.html", "text/html", false);
}

void httpserver::HandleConfig(WebServer& server) {
    ServeProtectedFile(server, "/config.html", "text/html", true);
}

void httpserver::HandleStyle(WebServer& server) {
    ServeFile(server, "/style.css", "text/css");
}

void httpserver::HandleLoginPost(WebServer& server) {
    const String username = server.arg("username");
    const String password = server.arg("password");
    const IPAddress remoteIP = server.client().remoteIP();

    if (username.isEmpty() || password.isEmpty()) {
        server.send(400, "application/json", "{\"error\":\"missing username or password\"}");
        return;
    }

    const UserReturn authentication = Settings.Users.Authenticate(username, password, remoteIP);
    if (authentication != UserReturn::AuthenticationSuccess) {
        Logger.Log(
            "Web Server: Logon failed for " + username + "@" + remoteIP.toString() + " - " + String(LoginFailureMessage(authentication)),
            logger::LogLevels::Warning
        );
        server.send(401, "application/json", "{\"error\":\"" + JsonEscaped(LoginFailureMessage(authentication)) + "\"}");
        return;
    }

    UserInfo info;
    if (Settings.Users.Find(username, &info) != UserReturn::NoError) {
        Logger.Log("Web Server: Unable to load user record after successful authentication", logger::LogLevels::Error);
        server.send(500, "application/json", "{\"error\":\"internal error\"}");
        return;
    }

    Session* session = CreateSession(info.username, info.admin);
    if (session == nullptr) {
        server.send(503, "application/json", "{\"error\":\"too many active sessions\"}");
        return;
    }

    Logger.Log("Web Server: Logon successful for " + info.username + "@" + remoteIP.toString(), logger::LogLevels::Information);
    server.sendHeader("Set-Cookie", "session=" + String(session->token) + "; Path=/; HttpOnly; SameSite=Strict");
    server.send(200, "application/json", "{\"success\":true,\"username\":\"" + JsonEscaped(info.username) + "\",\"admin\":" + (info.admin ? "true" : "false") + "}");
}

void httpserver::HandleLogoutPost(WebServer& server) {
    const String token = SessionTokenFromCookie(server);
    if (!token.isEmpty()) DestroySession(token);

    server.sendHeader("Set-Cookie", "session=; Path=/; HttpOnly; Max-Age=0");
    server.send(200, "application/json", "{\"success\":true}");
}

void httpserver::HandleSessionGet(WebServer& server) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        server.send(401, "application/json", "{\"authenticated\":false}");
        return;
    }

    server.send(
        200, "application/json",
        "{\"authenticated\":true,\"username\":\"" + JsonEscaped(session->username) + "\",\"admin\":" + (session->admin ? "true" : "false") + "}"
    );
}

void httpserver::HandleNotFound(WebServer& server) {
    server.send(404, "text/plain", "Not found");
}

void httpserver::ServeFile(WebServer& server, const char* path, const char* contentType) {
    String content;
    if (FileSystem.Read(path, content) != filesystem::Result::Ok) {
        server.send(404, "text/plain", "Not found");
        return;
    }
    server.send(200, contentType, content);
}

void httpserver::ServeProtectedFile(WebServer& server, const char* path, const char* contentType, bool requireAdmin) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        server.sendHeader("Location", "/");
        server.send(302, "text/plain", "Redirecting to /");
        return;
    }

    if (requireAdmin && !session->admin) {
        server.sendHeader("Location", "/dashboard.html");
        server.send(302, "text/plain", "Redirecting to /dashboard.html");
        return;
    }

    ServeFile(server, path, contentType);
}

httpserver::Session* httpserver::FindSession(const String& token) noexcept {
    if (token.isEmpty()) return nullptr;

    // A value of 0 disables the idle timeout entirely.
    const uint32_t idleTimeoutMs = IdleTimeout();
    const TickType_t now = xTaskGetTickCount();
    for (Session& session : pSessions) {
        if (!session.active || token != session.token) continue;

        if (idleTimeoutMs != 0 && static_cast<TickType_t>(now - session.lastActivityAt) >= pdMS_TO_TICKS(idleTimeoutMs)) {
            session.active = false;
            session.username.clear();
            return nullptr;
        }

        return &session;
    }

    return nullptr;
}

httpserver::Session* httpserver::CreateSession(const String& username, bool admin) noexcept {
    const TickType_t now = xTaskGetTickCount();
    const size_t maxSessions = MaxSessions();

    Session* target = nullptr;
    Session* oldest = nullptr;
    for (size_t index = 0; index < maxSessions; ++index) {
        Session& session = pSessions[index];
        if (!session.active) {
            target = &session;
            break;
        }
        if (oldest == nullptr || static_cast<TickType_t>(session.lastActivityAt - oldest->lastActivityAt) < 0) oldest = &session;
    }
    if (target == nullptr) target = oldest;
    if (target == nullptr) return nullptr;

    const String token = GenerateToken();
    token.toCharArray(target->token, sizeof(target->token));
    target->username = username;
    target->admin = admin;
    target->active = true;
    target->lastActivityAt = now;
    return target;
}

void httpserver::DestroySession(const String& token) noexcept {
    Session* session = FindSession(token);
    if (session == nullptr) return;
    session->active = false;
    session->username.clear();
    session->token[0] = '\0';
}

String httpserver::SessionTokenFromCookie(WebServer& server) {
    if (!server.hasHeader("Cookie")) return String();

    const String cookies = server.header("Cookie");
    const String key = "session=";
    const int start = cookies.indexOf(key);
    if (start < 0) return String();

    const int valueStart = start + key.length();
    const int end = cookies.indexOf(';', valueStart);
    return end < 0 ? cookies.substring(valueStart) : cookies.substring(valueStart, end);
}

httpserver::Session* httpserver::AuthenticatedSession(WebServer& server) noexcept {
    Session* session = FindSession(SessionTokenFromCookie(server));
    if (session != nullptr) session->lastActivityAt = xTaskGetTickCount();
    return session;
}

String httpserver::GenerateToken() {
    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof(bytes));

    char hex[sizeof(bytes) * 2 + 1];
    for (size_t index = 0; index < sizeof(bytes); ++index) {
        snprintf(hex + index * 2, 3, "%02x", bytes[index]);
    }
    return String(hex);
}
