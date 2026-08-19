#include "Settings.h"

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

    _username = std::move(username);
    return true;
}

bool user::SetPassword(const String& password) {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char* pers = "user_salt_gen";

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char*)pers, strlen(pers)) != 0) {
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        return false;
    }

    if (mbedtls_ctr_drbg_random(&ctr_drbg, _salt, PASS_SALTLEN) != 0) {
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        return false;
    }

    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_init(&ctx);

    if (mbedtls_md_setup(&ctx, info, 1) != 0) {
        mbedtls_md_free(&ctx);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        return false;
    }

    int ret = mbedtls_pkcs5_pbkdf2_hmac(&ctx, (const unsigned char*)password.c_str(), password.length(), _salt, PASS_SALTLEN, PASS_PBKDF2_ITERATIONS, PASS_HASHLEN, _hash);

    mbedtls_md_free(&ctx);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    return (ret == 0);
}

void user::Clear() noexcept {
    mbedtls_platform_zeroize(_salt, sizeof(_salt));
    mbedtls_platform_zeroize(_hash, sizeof(_hash));

    _username.clear();
    _admin = false;
}

bool user::Authenticate(const String& password) const {
    uint8_t computed[PASS_HASHLEN];

    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    mbedtls_md_init(&ctx);

    if (mbedtls_md_setup(&ctx, info, 1) != 0) {
        mbedtls_md_free(&ctx);
        return false;
    }

    const int ret = mbedtls_pkcs5_pbkdf2_hmac(&ctx, reinterpret_cast<const unsigned char*>(password.c_str()), password.length(), _salt, PASS_SALTLEN, PASS_PBKDF2_ITERATIONS, PASS_HASHLEN, computed);

    mbedtls_md_free(&ctx);

    if (ret != 0) {
        mbedtls_platform_zeroize(computed, sizeof(computed));
        return false;
    }

    volatile uint8_t difference = 0;

    for (size_t i = 0; i < PASS_HASHLEN; ++i) {
        difference |= _hash[i] ^ computed[i];
    }

    const bool authenticated = difference == 0;

    mbedtls_platform_zeroize(computed, sizeof(computed));

    return authenticated;
}

UserError users::SetPassword(const String& username, const String& newPassword) {
    Lock lock(_mutex);
    if (!lock.IsLocked()) return UserError::SynchronizationError;

    String normalizedUsername = username;

    if (!user::NormalizeUsername(normalizedUsername)) return UserError::InvalidUsername;

    for (size_t i = 0; i < _userCount; ++i) {
        if (_users[i].Username() != normalizedUsername) continue;
        if (!_users[i].SetPassword(newPassword)) return UserError::PasswordError;

        return UserError::NoError;
    }

    return UserError::UserNotFound;
}

UserError users::Rename(const String& currentUsername, const String& newUsername) {
    Lock lock(_mutex);
    if (!lock.IsLocked()) return UserError::SynchronizationError;

    String current = currentUsername;
    String replacement = newUsername;

    if (!user::NormalizeUsername(current) ||
        !user::NormalizeUsername(replacement)) {
        return UserError::InvalidUsername;
    }

    size_t currentIndex = MAX_USERS;

    for (size_t i = 0; i < _userCount; ++i) {
        if (_users[i].Username() == replacement) {
            if (_users[i].Username() == current) return UserError::NoError;

            return UserError::UserExists;
        }

        if (_users[i].Username() == current) currentIndex = i;
    }

    if (currentIndex == MAX_USERS) return UserError::UserNotFound;
    if (!_users[currentIndex].Username(std::move(replacement))) return UserError::InvalidUsername;

    return UserError::NoError;
}

UserError users::Add(const String& username, const String& password, bool admin) {
    Lock lock(_mutex);
    if (!lock.IsLocked()) return UserError::SynchronizationError;

    String normalizedUsername = username;
    if (!user::NormalizeUsername(normalizedUsername)) return UserError::InvalidUsername;

    if (_userCount == 0) admin = true;

    if (_userCount >= MAX_USERS) return UserError::MaxUsersReached;

    for (size_t i = 0; i < _userCount; ++i) {
        if (_users[i].Username() == normalizedUsername) return UserError::UserExists;
    }

    user& u = _users[_userCount];

    if (!u.Username(std::move(normalizedUsername))) return UserError::InvalidUsername;

    u.Admin(admin);

    if (!u.SetPassword(password)) return UserError::PasswordError;

    ++_userCount;
    return UserError::NoError;
}

UserError users::Remove(const String& username) {
    Lock lock(_mutex);
    if (!lock.IsLocked()) return UserError::SynchronizationError;

    String normalizedUsername = username;

    if (!user::NormalizeUsername(normalizedUsername)) return UserError::InvalidUsername;

    for (size_t i = 0; i < _userCount; ++i) {
        if (_users[i].Username() != normalizedUsername) continue;
        if (_users[i].Admin() && CountAdminsUnlocked() == 1) return UserError::NoAdminRemaining;

        const size_t lastIndex = _userCount - 1;

        if (i != lastIndex) _users[i] = std::move(_users[lastIndex]);

        _users[lastIndex].Clear();
        --_userCount;

        return UserError::NoError;
    }

    return UserError::UserNotFound;
}

UserError users::SetAdmin(const String& username, bool admin) {
    Lock lock(_mutex);
    if (!lock.IsLocked()) return UserError::SynchronizationError;

    String normalizedUsername = username;

    if (!user::NormalizeUsername(normalizedUsername)) return UserError::InvalidUsername;

    for (size_t i = 0; i < _userCount; ++i) {
        user& current = _users[i];

        if (current.Username() != normalizedUsername) continue;
        if (current.Admin() == admin) return UserError::NoError;
        if (!admin && current.Admin() && CountAdminsUnlocked() == 1) return UserError::NoAdminRemaining;

        current.Admin(admin);
        return UserError::NoError;
    }

    return UserError::UserNotFound;
}

UserError users::Authenticate(const String& username, const String& password, user** outUser) {
    Lock lock(_mutex);
    if (!lock.IsLocked()) return UserError::SynchronizationError;

    if (outUser != nullptr) *outUser = nullptr;

    String normalizedUsername = username;
    if (!user::NormalizeUsername(normalizedUsername)) return UserError::InvalidUsername;

    for (size_t i = 0; i < _userCount; ++i) {
        if (_users[i].Username() == normalizedUsername) {
            if (_users[i].Authenticate(password)) {
                if (outUser) *outUser = &_users[i];
                return UserError::NoError;
            }
            return UserError::InvalidCredentials;
        }
    }
    return UserError::UserNotFound;
}

UserError users::Find(const String& username, user** outUser) {
    Lock lock(_mutex);
    if (!lock.IsLocked()) return UserError::SynchronizationError;

    if (outUser != nullptr) *outUser = nullptr;

    String normalizedUsername = username;
    if (!user::NormalizeUsername(normalizedUsername)) return UserError::InvalidUsername;

    for (size_t i = 0; i < _userCount; ++i) {
        if (_users[i].Username() == normalizedUsername) {
            if (outUser) *outUser = &_users[i];
            return UserError::NoError;
        }
    }
    return UserError::UserNotFound;
}