#include "Users.h"

#include <mbedtls/pkcs5.h>
#include <mbedtls/md.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/platform_util.h>
#include <cstring>
#include <utility>

bool user::NormalizeUsername(String& username) noexcept {
    username.trim();
    username.toLowerCase();

    if (username.length() < USERNAME_MIN_LENGTH || username.length() > USERNAME_MAX_LENGTH) return false;

    for (size_t i = 0; i < username.length(); ++i) {
        const char c = username.charAt(i);

        const bool valid =
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-';

        if (!valid) return false;
    }

    return true;
}

bool user::Username(String username) noexcept {
    if (!NormalizeUsername(username)) return false;

    pUsername = std::move(username);
    return true;
}

bool user::SetPassword(const String& password) {
    if (password.length() < PASSWORD_MIN_LENGTH || password.length() > PASSWORD_MAX_LENGTH) return false;

    uint8_t newSalt[PASS_SALTLEN] = {0};
    uint8_t newHash[PASS_HASHLEN] = {0};

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char* pers = "user_salt_gen";

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char*)pers, strlen(pers)) != 0) {
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_platform_zeroize(newSalt, sizeof(newSalt));
        mbedtls_platform_zeroize(newHash, sizeof(newHash));
        return false;
    }

    if (mbedtls_ctr_drbg_random(&ctr_drbg, newSalt, sizeof(newSalt)) != 0) {
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_platform_zeroize(newSalt, sizeof(newSalt));
        mbedtls_platform_zeroize(newHash, sizeof(newHash));
        return false;
    }

    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_init(&ctx);

    if (mbedtls_md_setup(&ctx, info, 1) != 0) {
        mbedtls_md_free(&ctx);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_platform_zeroize(newSalt, sizeof(newSalt));
        mbedtls_platform_zeroize(newHash, sizeof(newHash));
        return false;
    }

    const int ret = mbedtls_pkcs5_pbkdf2_hmac(&ctx, reinterpret_cast<const unsigned char*>(password.c_str()), password.length(), newSalt, sizeof(newSalt), PASS_PBKDF2_ITERATIONS, sizeof(newHash), newHash);

    mbedtls_md_free(&ctx);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    if (ret == 0) {
        memcpy(pSalt, newSalt, sizeof(pSalt));
        memcpy(pHash, newHash, sizeof(pHash));
    }

    mbedtls_platform_zeroize(newSalt, sizeof(newSalt));
    mbedtls_platform_zeroize(newHash, sizeof(newHash));

    return ret == 0;
}

void user::Clear() noexcept {
    mbedtls_platform_zeroize(pSalt, sizeof(pSalt));
    mbedtls_platform_zeroize(pHash, sizeof(pHash));

    pUsername.clear();
    pAdmin = false;
}

bool user::Authenticate(const String& password) const {
    return VerifyPassword(password, pSalt, pHash);
}

bool user::VerifyPassword(const String& password, const uint8_t* salt, const uint8_t* hash) noexcept {
    uint8_t computed[PASS_HASHLEN];

    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    mbedtls_md_init(&ctx);

    if (mbedtls_md_setup(&ctx, info, 1) != 0) {
        mbedtls_md_free(&ctx);
        return false;
    }

    const int ret = mbedtls_pkcs5_pbkdf2_hmac(&ctx, reinterpret_cast<const unsigned char*>(password.c_str()), password.length(), salt, PASS_SALTLEN, PASS_PBKDF2_ITERATIONS, PASS_HASHLEN, computed);

    mbedtls_md_free(&ctx);

    if (ret != 0) {
        mbedtls_platform_zeroize(computed, sizeof(computed));
        return false;
    }

    volatile uint8_t difference = 0;

    for (size_t i = 0; i < PASS_HASHLEN; ++i) {
        difference |= hash[i] ^ computed[i];
    }

    const bool authenticated = difference == 0;

    mbedtls_platform_zeroize(computed, sizeof(computed));

    return authenticated;
}

UserReturn users::SetPassword(const String& username, const String& newPassword) {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return UserReturn::SynchronizationError;

    String normalizedUsername = username;

    if (!user::NormalizeUsername(normalizedUsername)) return UserReturn::InvalidUsername;

    for (size_t i = 0; i < pUserCount; ++i) {
        if (pUsers[i].Username() != normalizedUsername) continue;
        if (!pUsers[i].SetPassword(newPassword)) return UserReturn::PasswordError;

        return UserReturn::NoError;
    }

    return UserReturn::UserNotFound;
}

UserReturn users::Rename(const String& currentUsername, const String& newUsername) {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return UserReturn::SynchronizationError;

    String current = currentUsername;
    String replacement = newUsername;

    if (!user::NormalizeUsername(current) ||
        !user::NormalizeUsername(replacement)) {
        return UserReturn::InvalidUsername;
    }

    size_t currentIndex = MAX_USERS;

    for (size_t i = 0; i < pUserCount; ++i) {
        if (pUsers[i].Username() == replacement) {
            if (pUsers[i].Username() == current) return UserReturn::NoError;

            return UserReturn::UserExists;
        }

        if (pUsers[i].Username() == current) currentIndex = i;
    }

    if (currentIndex == MAX_USERS) return UserReturn::UserNotFound;
    if (!pUsers[currentIndex].Username(std::move(replacement))) return UserReturn::InvalidUsername;

    return UserReturn::NoError;
}

UserReturn users::Add(const String& username, const String& password, bool admin) {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return UserReturn::SynchronizationError;

    String normalizedUsername = username;
    if (!user::NormalizeUsername(normalizedUsername)) return UserReturn::InvalidUsername;

    if (pUserCount == 0) admin = true;

    if (pUserCount >= MAX_USERS) return UserReturn::MaxUsersReached;

    for (size_t i = 0; i < pUserCount; ++i) {
        if (pUsers[i].Username() == normalizedUsername) return UserReturn::UserExists;
    }

    user& u = pUsers[pUserCount];

    if (!u.Username(std::move(normalizedUsername))) return UserReturn::InvalidUsername;

    u.Admin(admin);

    if (!u.SetPassword(password)) {
        u.Clear();
        return UserReturn::PasswordError;
    }

    ++pUserCount;
    return UserReturn::NoError;
}

UserReturn users::AddStored(const String& username, bool admin, const uint8_t (&salt)[PASS_SALTLEN], const uint8_t (&hash)[PASS_HASHLEN]) {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return UserReturn::SynchronizationError;

    String normalizedUsername = username;
    if (!user::NormalizeUsername(normalizedUsername)) return UserReturn::InvalidUsername;

    if (pUserCount >= MAX_USERS) return UserReturn::MaxUsersReached;

    for (size_t i = 0; i < pUserCount; ++i) {
        if (pUsers[i].Username() == normalizedUsername) return UserReturn::UserExists;
    }

    user& u = pUsers[pUserCount];

    if (!u.Username(std::move(normalizedUsername))) return UserReturn::InvalidUsername;

    u.Admin(admin);
    u.SetCredentials(salt, hash);

    ++pUserCount;
    return UserReturn::NoError;
}

UserReturn users::Remove(const String& username) {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return UserReturn::SynchronizationError;

    String normalizedUsername = username;

    if (!user::NormalizeUsername(normalizedUsername)) return UserReturn::InvalidUsername;

    for (size_t i = 0; i < pUserCount; ++i) {
        if (pUsers[i].Username() != normalizedUsername) continue;
        if (pUsers[i].Admin() && CountAdminsUnlocked() == 1) return UserReturn::NoAdminRemaining;

        const size_t lastIndex = pUserCount - 1;

        if (i != lastIndex) pUsers[i] = std::move(pUsers[lastIndex]);

        pUsers[lastIndex].Clear();
        --pUserCount;

        return UserReturn::NoError;
    }

    return UserReturn::UserNotFound;
}

UserReturn users::SetAdmin(const String& username, bool admin) {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return UserReturn::SynchronizationError;

    String normalizedUsername = username;

    if (!user::NormalizeUsername(normalizedUsername)) return UserReturn::InvalidUsername;

    for (size_t i = 0; i < pUserCount; ++i) {
        user& current = pUsers[i];

        if (current.Username() != normalizedUsername) continue;
        if (current.Admin() == admin) return UserReturn::NoError;
        if (!admin && current.Admin() && CountAdminsUnlocked() == 1) return UserReturn::NoAdminRemaining;

        current.Admin(admin);
        return UserReturn::NoError;
    }

    return UserReturn::UserNotFound;
}

UserReturn users::Authenticate(const String& username, const String& password, IPAddress clientIP) {
    const uint32_t ip = static_cast<uint32_t>(clientIP);

    // The PBKDF2 hash below costs on the order of hundreds of milliseconds.
    // Only the credential lookup and the rate-limit bookkeeping need the
    // lock; the hash itself runs unlocked so it does not stall other user
    // operations (or other concurrent logon attempts) for its duration.
    uint8_t salt[PASS_SALTLEN];
    uint8_t hash[PASS_HASHLEN];

    {
        Lock lock(pMutex);
        if (!lock.IsLocked()) return UserReturn::SynchronizationError;

        const uint32_t now = millis();
        if (AuthenticationRateLimitedUnlocked(ip, now)) return UserReturn::AuthenticationRateLimited;

        String normalizedUsername = username;
        if (!user::NormalizeUsername(normalizedUsername)) {
            RegisterAuthenticationFailureUnlocked(ip, millis());
            return UserReturn::InvalidCredentials;
        }

        bool found = false;
        for (size_t i = 0; i < pUserCount; ++i) {
            if (pUsers[i].Username() == normalizedUsername) {
                pUsers[i].CopyCredentials(salt, hash);
                found = true;
                break;
            }
        }

        if (!found) {
            RegisterAuthenticationFailureUnlocked(ip, millis());
            return UserReturn::InvalidCredentials;
        }
    }

    const bool authenticated = user::VerifyPassword(password, salt, hash);
    mbedtls_platform_zeroize(salt, sizeof(salt));
    mbedtls_platform_zeroize(hash, sizeof(hash));

    Lock lock(pMutex);
    if (!lock.IsLocked()) return UserReturn::SynchronizationError;

    if (authenticated) {
        ResetAuthenticationRateLimitUnlocked(ip);
        return UserReturn::AuthenticationSuccess;
    }

    RegisterAuthenticationFailureUnlocked(ip, millis());
    return UserReturn::InvalidCredentials;
}

bool users::AuthenticationRateLimitedUnlocked(uint32_t ip, uint32_t now) const noexcept {
    for (const AuthRateLimitEntry& entry : pAuthRateLimits) {
        if (entry.inUse && entry.ip == ip) {
            return entry.delayMs != 0 && static_cast<int32_t>(now - entry.blockedUntilMs) < 0;
        }
    }
    return false;
}

void users::RegisterAuthenticationFailureUnlocked(uint32_t ip, uint32_t now) noexcept {
    AuthRateLimitEntry* entry = nullptr;
    AuthRateLimitEntry* oldest = nullptr;

    for (AuthRateLimitEntry& candidate : pAuthRateLimits) {
        if (candidate.inUse && candidate.ip == ip) {
            entry = &candidate;
            break;
        }
        if (!candidate.inUse) {
            if (entry == nullptr) entry = &candidate;
            continue;
        }
        if (oldest == nullptr || static_cast<int32_t>(candidate.lastAttemptMs - oldest->lastAttemptMs) < 0) {
            oldest = &candidate;
        }
    }

    // Table full and IP not tracked yet: evict the least-recently-attempted entry.
    if (entry == nullptr) entry = oldest;
    if (entry == nullptr) return;

    if (!entry->inUse || entry->ip != ip) {
        entry->inUse = true;
        entry->ip = ip;
        entry->delayMs = 0;
    }

    entry->delayMs = entry->delayMs == 0 ? AUTH_RATE_LIMIT_INITIAL_DELAY_MS :
        entry->delayMs >= AUTH_RATE_LIMIT_MAX_DELAY_MS / 2 ? AUTH_RATE_LIMIT_MAX_DELAY_MS : entry->delayMs * 2;

    entry->blockedUntilMs = now + entry->delayMs;
    entry->lastAttemptMs = now;
}

void users::ResetAuthenticationRateLimitUnlocked(uint32_t ip) noexcept {
    for (AuthRateLimitEntry& entry : pAuthRateLimits) {
        if (entry.inUse && entry.ip == ip) {
            entry.inUse = false;
            entry.delayMs = 0;
            entry.blockedUntilMs = 0;
            return;
        }
    }
}

UserReturn users::Find(const String& username, UserInfo* outUser) {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return UserReturn::SynchronizationError;

    if (outUser != nullptr) *outUser = UserInfo{};

    String normalizedUsername = username;
    if (!user::NormalizeUsername(normalizedUsername)) return UserReturn::InvalidUsername;

    for (size_t i = 0; i < pUserCount; ++i) {
        if (pUsers[i].Username() == normalizedUsername) {
            if (outUser != nullptr) {
                outUser->username = pUsers[i].Username();
                outUser->admin = pUsers[i].Admin();
            }
            return UserReturn::NoError;
        }
    }
    return UserReturn::UserNotFound;
}