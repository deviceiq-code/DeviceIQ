#pragma once

#include <Arduino.h>
#include <base64.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/md.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/platform_util.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

constexpr uint32_t PASS_PBKDF2_ITERATIONS = 100000;
constexpr uint32_t PASS_SALTLEN = 16;
constexpr uint32_t PASS_HASHLEN = 32;
constexpr uint8_t MAX_USERS = 3;
constexpr size_t USERNAME_MIN_LENGTH = 3;
constexpr size_t USERNAME_MAX_LENGTH = 32;

enum UserError : uint8_t { NoError = 0, UserExists, UserNotFound, MaxUsersReached, NoAdminRemaining, InvalidUsername, PasswordError, InvalidCredentials, SynchronizationError };

class user {
    public:
        const String& Username() const noexcept { return _username; }
        bool Admin() const noexcept { return _admin; }
        
        static bool NormalizeUsername(String& username) noexcept;
    private:
        friend class users;

        bool Username(String username) noexcept;
        bool SetPassword(const String& password);
        bool Authenticate(const String& password) const;
        void Admin(bool value) noexcept { _admin = value; }
        void Clear() noexcept;

        uint8_t _salt[PASS_SALTLEN] = {0};
        uint8_t _hash[PASS_HASHLEN] = {0};

        String _username;
        bool _admin = false;
};

class users {
    public:
        users() : _mutex(xSemaphoreCreateMutex()) {}
        users(const users&) = delete;
        users& operator=(const users&) = delete;
        ~users() { if (_mutex != nullptr) { vSemaphoreDelete(_mutex); }}
        
        inline size_t Count() const noexcept { Lock lock(_mutex); if (!lock.IsLocked()) return 0; return _userCount; }
        inline size_t CountAdmins() const noexcept { Lock lock(_mutex); if (!lock.IsLocked()) return 0; size_t count = 0; for (size_t i = 0; i < _userCount; ++i) if (_users[i].Admin()) count++; return count; }
        UserError SetAdmin(const String& username, bool admin);
        UserError Add(const String& username, const String& password, bool admin = false);
        UserError Remove(const String& username);
        UserError Authenticate(const String& username, const String& password, user** outUser = nullptr);
        UserError Find(const String& username, user** outUser = nullptr);
        UserError Rename(const String& currentUsername, const String& newUsername);
        UserError SetPassword(const String& username, const String& newPassword);
    private:
        class Lock {
            public:
                explicit Lock(SemaphoreHandle_t mutex) : _mutex(mutex), _locked(mutex != nullptr && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {}
                ~Lock() { if (_locked) xSemaphoreGive(_mutex); }
                Lock(const Lock&) = delete;
                Lock& operator=(const Lock&) = delete;

                bool IsLocked() const noexcept { return _locked; }

            private:
                SemaphoreHandle_t _mutex;
                bool _locked;
        };

        size_t CountAdminsUnlocked() const noexcept { size_t count = 0; for (size_t i = 0; i < _userCount; ++i) { if (_users[i].Admin()) { ++count; }} return count;}

        SemaphoreHandle_t _mutex = nullptr;
        user _users[MAX_USERS];
        size_t _userCount = 0;

        bool hasAdmin() const { for (size_t i = 0; i < _userCount; ++i) if (_users[i].Admin()) return true; return false; }
};