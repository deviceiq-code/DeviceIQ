#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

constexpr uint32_t PASS_PBKDF2_ITERATIONS = 100000;
constexpr uint32_t PASS_SALTLEN = 16;
constexpr uint32_t PASS_HASHLEN = 32;
constexpr uint8_t MAX_USERS = 3;
constexpr size_t USERNAME_MIN_LENGTH = 3;
constexpr size_t USERNAME_MAX_LENGTH = 32;
constexpr size_t PASSWORD_MIN_LENGTH = 8;
constexpr size_t PASSWORD_MAX_LENGTH = 64;
constexpr uint32_t AUTH_RATE_LIMIT_INITIAL_DELAY_MS = 1000;
constexpr uint32_t AUTH_RATE_LIMIT_MAX_DELAY_MS = 30000;

enum UserError : uint8_t { NoError = 0, AuthenticationSuccess, UserExists, UserNotFound, MaxUsersReached, NoAdminRemaining, InvalidUsername, PasswordError, InvalidCredentials, SynchronizationError, AuthenticationRateLimited };

struct UserInfo {
    String username;
    bool admin = false;
};

class user {
    public:
        const String& Username() const noexcept { return pUsername; }
        bool Admin() const noexcept { return pAdmin; }
        
        static bool NormalizeUsername(String& username) noexcept;
    private:
        friend class users;

        bool Username(String username) noexcept;
        bool SetPassword(const String& password);
        bool Authenticate(const String& password) const;
        void Admin(bool value) noexcept { pAdmin = value; }
        void Clear() noexcept;

        uint8_t pSalt[PASS_SALTLEN] = {0};
        uint8_t pHash[PASS_HASHLEN] = {0};

        String pUsername;
        bool pAdmin = false;
};

class users {
    public:
        users() noexcept : pMutex(xSemaphoreCreateMutexStatic(&pMutexStorage)) { configASSERT(pMutex != nullptr); }
        users(const users&) = delete;
        users& operator=(const users&) = delete;
        
        inline size_t Count() const noexcept { Lock lock(pMutex); if (!lock.IsLocked()) return 0; return pUserCount; }
        inline size_t CountAdmins() const noexcept { Lock lock(pMutex); if (!lock.IsLocked()) return 0; size_t count = 0; for (size_t i = 0; i < pUserCount; ++i) if (pUsers[i].Admin()) count++; return count; }
        UserError SetAdmin(const String& username, bool admin);
        UserError Add(const String& username, const String& password, bool admin = false);
        UserError Remove(const String& username);
        UserError Authenticate(const String& username, const String& password);
        UserError Find(const String& username, UserInfo* outUser = nullptr);
        UserError Rename(const String& currentUsername, const String& newUsername);
        UserError SetPassword(const String& username, const String& newPassword);

        template<typename Visitor>
        UserError ForEachStored(Visitor&& visitor) const {
            Lock lock(pMutex);
            if (!lock.IsLocked()) return UserError::SynchronizationError;

            for (size_t i = 0; i < pUserCount; ++i) {
                const user& current = pUsers[i];
                visitor(current.Username(), current.Admin(), current.pSalt, current.pHash);
            }

            return UserError::NoError;
        }
    private:
        class Lock {
            public:
                explicit Lock(SemaphoreHandle_t mutex) : pMutex(mutex), pLocked(mutex != nullptr && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {}
                ~Lock() { if (pLocked) xSemaphoreGive(pMutex); }
                Lock(const Lock&) = delete;
                Lock& operator=(const Lock&) = delete;

                bool IsLocked() const noexcept { return pLocked; }

            private:
                SemaphoreHandle_t pMutex;
                bool pLocked;
        };

        size_t CountAdminsUnlocked() const noexcept { size_t count = 0; for (size_t i = 0; i < pUserCount; ++i) { if (pUsers[i].Admin()) { ++count; }} return count;}
        bool AuthenticationRateLimitedUnlocked(uint32_t now) const noexcept;
        void RegisterAuthenticationFailureUnlocked(uint32_t now) noexcept;
        void ResetAuthenticationRateLimitUnlocked() noexcept;

        StaticSemaphore_t pMutexStorage{};
        SemaphoreHandle_t pMutex = nullptr;

        user pUsers[MAX_USERS];
        size_t pUserCount = 0;
        uint32_t pAuthenticationDelayMs = 0;
        uint32_t pAuthenticationBlockedUntilMs = 0;

        bool hasAdmin() const { for (size_t i = 0; i < pUserCount; ++i) if (pUsers[i].Admin()) return true; return false; }
};