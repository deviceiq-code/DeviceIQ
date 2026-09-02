#include "FileSystem.h"

namespace {
    void CountEntries(File directory, filesystem::Statistics& output) {
        File entry = directory.openNextFile();
        while (entry) {
            if (entry.isDirectory()) {
                ++output.directories;
                CountEntries(entry, output);
            } else {
                const size_t size = entry.size();
                ++output.files;
                output.fileBytes += size;
                if (size > output.largestFileBytes) output.largestFileBytes = size;
            }
            entry.close();
            entry = directory.openNextFile();
        }
    }
}

bool filesystem::Start(bool formatOnFail) {
    if (pMounted) return true;

    if (pMutex == nullptr) {
        pMutex = xSemaphoreCreateMutex();
        if (pMutex == nullptr) return false;
    }

    {
        Lock lock(pMutex, pdMS_TO_TICKS(1000));

        if (lock.IsLocked() == false) return false;
        if (LittleFS.begin(formatOnFail) == false) return false;

        pMounted = true;
    }

    PurgeOrphanedTempFiles();

    return true;
}

void filesystem::Stop() {
    Lock lock(pMutex, pdMS_TO_TICKS(1000));
    if (!lock.IsLocked() || !pMounted) return;

    LittleFS.end();
    pMounted = false;
}

void filesystem::PurgeOrphanedTempFiles() {
    Lock lock(pMutex, pdMS_TO_TICKS(1000));
    if (lock.IsLocked() == false) return;

    File root = LittleFS.open("/", FILE_READ);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }

    File entry = root.openNextFile();
    while (entry) {
        const bool isTempFile = !entry.isDirectory() && String(entry.name()).endsWith(".tmp");
        const String path = entry.path();
        entry.close();

        if (isTempFile) LittleFS.remove(path.c_str());

        entry = root.openNextFile();
    }

    root.close();
}

bool filesystem::GetStatistics(Statistics& output, TickType_t timeout) {
    output = Statistics();
    if (!pMounted) return false;

    Lock lock(pMutex, timeout);
    if (!lock.IsLocked()) return false;

    output.totalBytes = LittleFS.totalBytes();
    output.usedBytes = LittleFS.usedBytes();

    File root = LittleFS.open("/", FILE_READ);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return false;
    }

    CountEntries(root, output);
    root.close();
    return true;
}

bool filesystem::Exists(const char* path, TickType_t timeout) {
    if (pMounted == false || path == nullptr) return false;
    Lock lock(pMutex, timeout);
    if (lock.IsLocked() == false) return false;
    return LittleFS.exists(path);
}

size_t filesystem::Size(const char* path, TickType_t timeout) {
    if (pMounted == false || path == nullptr) return 0;

    Lock lock(pMutex, timeout);

    if (lock.IsLocked() == false) return 0;

    File file = LittleFS.open(path, FILE_READ);
    if (file == false) return 0;

    const size_t size = file.size();
    file.close();

    return size;
}

filesystem::Result filesystem::Read(const char* path, String& output, TickType_t timeout) {
    output.clear();

    if (pMounted == false) return Result::NotInitialized;
    if (path == nullptr) return Result::InvalidArgument;

    Lock lock(pMutex, timeout);

    if (lock.IsLocked() == false) return Result::LockTimeout;
    if (LittleFS.exists(path) == false) return Result::NotFound;

    File file = LittleFS.open(path, FILE_READ);

    if (file == false) return Result::OpenFailed;

    output.reserve(file.size());

    while (file.available()) {
        output += static_cast<char>(file.read());
    }


    file.close();

    return Result::Ok;
}

filesystem::Result filesystem::Read(const char* path, uint8_t* buffer, size_t bufferSize, size_t& bytesRead, TickType_t timeout) {
    if (pMounted == false) return Result::NotInitialized;
    if (path == nullptr || buffer == nullptr || bufferSize == 0) return Result::InvalidArgument;

    bytesRead = 0;

    Lock lock(pMutex, timeout);

    if (lock.IsLocked() == false) return Result::LockTimeout;

    if (LittleFS.exists(path) == false) return Result::NotFound;

    File file = LittleFS.open(path, FILE_READ);
    if (file == false) return Result::OpenFailed;

    bytesRead = file.read(buffer, bufferSize);

    file.close();

    return Result::Ok;
}

filesystem::Result filesystem::Write(const char* path, const uint8_t* data, size_t length, TickType_t timeout) {
    if (pMounted == false) return Result::NotInitialized;
    if (path == nullptr || data == nullptr) return Result::InvalidArgument;

    Lock lock(pMutex, timeout);

    if (lock.IsLocked() == false) return Result::LockTimeout;

    File file = LittleFS.open(path, FILE_WRITE);

    if (file == false) return Result::OpenFailed;
    const size_t written = file.write(data, length);

    file.close();

    if (written != length) return Result::WriteFailed;

    return Result::Ok;
}


filesystem::Result filesystem::Write(const char* path, const String& data, TickType_t timeout) {
    return Write(path, reinterpret_cast<const uint8_t*>(data.c_str()), data.length(), timeout);
}

filesystem::Result filesystem::WriteAtomic(const char* path, const uint8_t* data, size_t length, TickType_t timeout) {
    if (pMounted == false) return Result::NotInitialized;
    if (path == nullptr || data == nullptr) return Result::InvalidArgument;

    const String tempPath = String(path) + ".tmp";

    Lock lock(pMutex, timeout);

    if (lock.IsLocked() == false) return Result::LockTimeout;

    File file = LittleFS.open(tempPath.c_str(), FILE_WRITE);
    if (file == false) return Result::OpenFailed;
    const size_t written = file.write(data, length);
    file.close();

    if (written != length) {
        LittleFS.remove(tempPath.c_str());
        return Result::WriteFailed;
    }

    // Replaces path if it already exists; either the old or the new content
    // survives a crash, never a partially-written file.
    if (LittleFS.rename(tempPath.c_str(), path) == false) {
        LittleFS.remove(tempPath.c_str());
        return Result::RenameFailed;
    }

    return Result::Ok;
}

filesystem::Result filesystem::WriteAtomic(const char* path, const String& data, TickType_t timeout) {
    return WriteAtomic(path, reinterpret_cast<const uint8_t*>(data.c_str()), data.length(), timeout);
}

filesystem::Result filesystem::Append(const char* path, const uint8_t* data, size_t length, TickType_t timeout) {
    if (pMounted == false) return Result::NotInitialized;
    if (path == nullptr || data == nullptr) { return Result::InvalidArgument; }

    Lock lock(pMutex, timeout);

    if (lock.IsLocked() == false) return Result::LockTimeout;

    File file = LittleFS.open(path, FILE_APPEND);

    if (file == false) return Result::OpenFailed;
    const size_t written = file.write(data, length);

    file.close();

    if (written != length) return Result::WriteFailed;

    return Result::Ok;
}

filesystem::Result filesystem::Append(const char* path, const String& data, TickType_t timeout) {
    return Append(path, reinterpret_cast<const uint8_t*>(data.c_str()), data.length(), timeout);
}

filesystem::Result filesystem::AppendRotating(const char* path, const uint8_t* data, size_t length, size_t maxFileSize, TickType_t timeout) {
    if (pMounted == false) return Result::NotInitialized;
    if (path == nullptr || data == nullptr || maxFileSize == 0 || length > maxFileSize) return Result::InvalidArgument;

    Lock lock(pMutex, timeout);

    if (lock.IsLocked() == false) return Result::LockTimeout;

    File file = LittleFS.open(path, FILE_APPEND);
    if (file == false) return Result::OpenFailed;

    const size_t currentSize = file.size();
    if (currentSize > maxFileSize - length) {
        file.close();

        // Remove instead of renaming: a full volume may not have enough free
        // blocks to create or update a backup directory entry.
        if (LittleFS.remove(path) == false) return Result::RemoveFailed;

        file = LittleFS.open(path, FILE_WRITE);
        if (file == false) return Result::OpenFailed;
    }

    const size_t written = file.write(data, length);
    file.close();

    if (written != length) return Result::WriteFailed;

    return Result::Ok;
}

filesystem::Result filesystem::AppendRotating(const char* path, const String& data, size_t maxFileSize, TickType_t timeout) {
    return AppendRotating(path, reinterpret_cast<const uint8_t*>(data.c_str()), data.length(), maxFileSize, timeout);
}

filesystem::Result filesystem::Remove(const char* path, TickType_t timeout) {
    if (pMounted == false) return Result::NotInitialized;
    if (path == nullptr) return Result::InvalidArgument;

    Lock lock(pMutex, timeout);

    if (lock.IsLocked() == false) return Result::LockTimeout;
    if (LittleFS.exists(path) == false) return Result::NotFound;
    if (LittleFS.remove(path) == false) return Result::RemoveFailed;

    return Result::Ok;
}

filesystem::Result filesystem::Rename(const char* source, const char* destination, TickType_t timeout) {
    if (pMounted == false) return Result::NotInitialized;
    if (source == nullptr || destination == nullptr) return Result::InvalidArgument;

    Lock lock(pMutex, timeout);

    if (lock.IsLocked() == false) return Result::LockTimeout;
    if (LittleFS.exists(source) == false) return Result::NotFound;
    if (LittleFS.rename(source, destination) == false) return Result::RenameFailed;

    return Result::Ok;
}

filesystem::Result filesystem::CreateDirectory(const char* path, TickType_t timeout) {
    if (pMounted == false) return Result::NotInitialized;
    if (path == nullptr) return Result::InvalidArgument;

    Lock lock(pMutex, timeout);

    if (lock.IsLocked() == false) return Result::LockTimeout;
    if (LittleFS.mkdir(path) == false) return Result::CreateDirectoryFailed;

    return Result::Ok;
}

filesystem::Result filesystem::RemoveDirectory(const char* path, TickType_t timeout) {
    if (pMounted == false) return Result::NotInitialized;
    if (path == nullptr) return Result::InvalidArgument;

    Lock lock(pMutex, timeout);

    if (lock.IsLocked() == false) return Result::LockTimeout;
    if (LittleFS.rmdir(path) == false) return Result::RemoveDirectoryFailed;

    return Result::Ok;
}
