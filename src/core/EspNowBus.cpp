#include "EspNowBus.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <mbedtls/md.h>
#include <cstdio>

#include "Globals.h"

using namespace espnowprotocol;

namespace {
    espnowbus* Instance = nullptr;

    bool Elapsed(TickType_t now, TickType_t at) noexcept {
        return static_cast<int32_t>(now - at) >= 0;
    }

    template <typename Body>
    bool ReadBody(const uint8_t* source, size_t length, Body& body) noexcept {
        if (length != sizeof(Body)) return false;
        memcpy(&body, source, sizeof(Body));
        return true;
    }

    // 0 means "no session" throughout, so it's never handed out.
    uint32_t RandomNonce() noexcept {
        uint32_t value = 0;
        while (value == 0) value = esp_random();
        return value;
    }
}

void espnowprotocol::Hmac(const uint8_t* key, const uint8_t* nonces, const uint8_t* data, size_t length, uint8_t* tag) {
    uint8_t digest[32];
    mbedtls_md_context_t context;
    mbedtls_md_init(&context);
    mbedtls_md_setup(&context, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&context, key, KeyLength);
    mbedtls_md_hmac_update(&context, nonces, 8);
    mbedtls_md_hmac_update(&context, data, length);
    mbedtls_md_hmac_finish(&context, digest);
    mbedtls_md_free(&context);
    memcpy(tag, digest, TagLength);
}

espnowbus::espnowbus() noexcept : pMutex(xSemaphoreCreateMutexStatic(&pMutexStorage)) {
    configASSERT(pMutex != nullptr);
}

bool espnowbus::Load() {
    JsonDocument document;
    if (!Settings.ReadNodes(document)) return false;

    Lock lock(pMutex);
    if (!lock.IsLocked()) return false;

    size_t index = 0;
    for (JsonObjectConst entry : document["Nodes"].as<JsonArrayConst>()) {
        const String name = entry["Name"] | "";
        const String mac = entry["MAC"] | "";
        const String key = entry["Key"] | "";
        if (index >= MAX_NODES) {
            Logger.Log("ESP-NOW: node table is full, ignoring '" + name + "'", logger::LogLevels::Error);
            break;
        }

        Node& node = pNodes[index];
        bool valid = IsValidName(name) && ParseMac(mac, node.mac) && key.length() == KeyLength * 2;
        for (size_t byte = 0; valid && byte < KeyLength; ++byte) {
            unsigned value = 0;
            valid = sscanf(key.c_str() + byte * 2, "%2x", &value) == 1;
            node.key[byte] = static_cast<uint8_t>(value);
        }
        if (!valid) {
            Logger.Log("ESP-NOW: ignoring invalid entry in Nodes: '" + name + "'", logger::LogLevels::Error);
            ClearNode(node);
            continue;
        }

        node.used = true;
        node.name = name;
        node.controllerNonce = RandomNonce();
        ++index;
    }
    return true;
}

bool espnowbus::Start() {
    if (pTaskHandle != nullptr) return true;

    pReceiveQueue = xQueueCreateStatic(RECEIVE_QUEUE_LENGTH, sizeof(ReceivedFrame), pReceiveQueueStorage, &pReceiveQueueControl);
    if (pReceiveQueue == nullptr) return false;

    Instance = this;
    if (xTaskCreate(TaskEntry, "EspNow", TASK_STACK_SIZE, this, TASK_PRIORITY, &pTaskHandle) != pdPASS) {
        pTaskHandle = nullptr;
        return false;
    }
    return true;
}

uint8_t espnowbus::Channel() const noexcept {
    uint8_t primary = 0;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    if (!pRadioStarted || esp_wifi_get_channel(&primary, &secondary) != ESP_OK) return 0;
    return primary;
}

espnowbus::Port espnowbus::Attach(const String& name, const PortConfig& config) noexcept {
    Lock lock(pMutex);
    Port port;
    if (!lock.IsLocked()) return port;

    Node* node = FindByName(name);
    if (node == nullptr || node->portCount >= MaxPorts || config.pin > MaxPin) {
        Logger.Log("ESP-NOW: unable to attach GPIO " + String(config.pin) + " on node '" + name + "'", logger::LogLevels::Error);
        return port;
    }

    node->ports[node->portCount++] = config;
    if (config.mode == static_cast<uint8_t>(PortMode::Output) && config.safeLevel) node->desired |= static_cast<uint16_t>(1U << config.pin);
    port.node = static_cast<int8_t>(node - pNodes);
    port.pin = config.pin;
    return port;
}

espnowbus::Port espnowbus::AttachOutput(const String& node, uint8_t pin, bool safeLevel, uint16_t autoOffMs) noexcept {
    PortConfig config{};
    config.pin = pin;
    config.mode = static_cast<uint8_t>(PortMode::Output);
    config.safeLevel = safeLevel ? 1 : 0;
    config.autoOffMs = autoOffMs;
    return Attach(node, config);
}

espnowbus::Port espnowbus::AttachInput(const String& node, uint8_t pin, bool pullUp) noexcept {
    PortConfig config{};
    config.pin = pin;
    config.mode = static_cast<uint8_t>(pullUp ? PortMode::InputPullUp : PortMode::Input);
    return Attach(node, config);
}

bool espnowbus::SetOutput(const Port& port, bool level, bool requireOnline) noexcept {
    if (port.node < 0) return false;
    {
        Lock lock(pMutex);
        if (!lock.IsLocked()) return false;

        Node& node = pNodes[port.node];
        const uint16_t mask = static_cast<uint16_t>(1U << port.pin);
        if (!node.used || (OutputMask(node) & mask) == 0 || (requireOnline && !node.online)) return false;

        node.desired = level ? static_cast<uint16_t>(node.desired | mask) : static_cast<uint16_t>(node.desired & ~mask);
        node.nextSyncAt = xTaskGetTickCount();
    }
    Wake();
    return true;
}

bool espnowbus::InputLevel(const Port& port, bool& level) const noexcept {
    if (port.node < 0) return false;

    Lock lock(pMutex);
    if (!lock.IsLocked()) return false;

    const Node& node = pNodes[port.node];
    if (!node.used || !node.online || !node.reportedKnown) return false;
    level = (node.reported & (1U << port.pin)) != 0;
    return true;
}

bool espnowbus::Online(const Port& port) const noexcept {
    if (port.node < 0) return false;

    Lock lock(pMutex);
    return lock.IsLocked() && pNodes[port.node].used && pNodes[port.node].online;
}

bool espnowbus::HasNode(const String& name) const noexcept {
    Lock lock(pMutex);
    return lock.IsLocked() && FindByName(name) != nullptr;
}

size_t espnowbus::NodeCount() const noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return 0;

    size_t count = 0;
    for (const Node& node : pNodes) if (node.used) ++count;
    return count;
}

bool espnowbus::GetNode(size_t index, NodeInfo& info) const noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return false;

    const TickType_t now = xTaskGetTickCount();
    for (const Node& node : pNodes) {
        if (!node.used) continue;
        if (index-- != 0) continue;

        info.name = node.name;
        info.mac = FormatMac(node.mac);
        info.online = node.online;
        info.configSynced = node.configSynced;
        info.configRejected = node.configRejected;
        info.firmware = node.firmware;
        info.pinMask = node.pinMask;
        info.ports = node.portCount;
        info.seen = node.seen;
        info.lastSeenSeconds = node.seen ? pdTICKS_TO_MS(now - node.lastHeardAt) / 1000U : 0;
        return true;
    }
    return false;
}

size_t espnowbus::PendingCount() const noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return 0;

    size_t count = 0;
    for (const Pending& pending : pPending) if (pending.used) ++count;
    return count;
}

bool espnowbus::GetPending(size_t index, PendingInfo& info) const noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return false;

    const TickType_t now = xTaskGetTickCount();
    for (const Pending& pending : pPending) {
        if (!pending.used) continue;
        if (index-- != 0) continue;

        info.mac = FormatMac(pending.mac);
        info.firmware = pending.firmware;
        info.pinMask = pending.pinMask;
        info.lastSeenSeconds = pdTICKS_TO_MS(now - pending.lastHeardAt) / 1000U;
        return true;
    }
    return false;
}

void espnowbus::OpenDiscovery(uint16_t seconds) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    pDiscoveryOpen = true;
    pDiscoveryUntil = xTaskGetTickCount() + pdMS_TO_TICKS(static_cast<uint32_t>(seconds) * 1000U);
}

void espnowbus::CloseDiscovery() noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    pDiscoveryOpen = false;
    for (Pending& pending : pPending) if (pending.used) DropPending(pending);
}

uint32_t espnowbus::DiscoverySecondsLeft() const noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked() || !pDiscoveryOpen) return 0;

    const TickType_t now = xTaskGetTickCount();
    return Elapsed(now, pDiscoveryUntil) ? 0 : pdTICKS_TO_MS(pDiscoveryUntil - now) / 1000U + 1U;
}

bool espnowbus::BeginAdoption(const uint8_t* mac, const String& name, int8_t replaces, String& error) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) {
        error = "Node table is busy.";
        return false;
    }
    if (!pRadioStarted) {
        error = "ESP-NOW is not running.";
        return false;
    }
    if (pJob.active) {
        error = "Another adoption is in progress.";
        return false;
    }
    if (FindPending(mac) == nullptr) {
        error = "That device is not waiting for adoption.";
        return false;
    }
    if (FindByMac(mac) != nullptr) {
        error = "That device is already adopted.";
        return false;
    }

    pJob = AdoptionJob{};
    pJob.active = true;
    pJob.replaces = replaces;
    pJob.name = name;
    memcpy(pJob.mac, mac, sizeof(pJob.mac));
    esp_fill_random(pJob.key, sizeof(pJob.key));
    pJob.challenge = RandomNonce();
    pJob.nextAttemptAt = xTaskGetTickCount();
    pAdoptionState = AdoptionState::Working;
    pAdoptionMessage = "Adopting " + name + "...";
    return true;
}

bool espnowbus::Adopt(const String& mac, const String& name, String& error) noexcept {
    uint8_t address[6];
    if (!ParseMac(mac, address)) {
        error = "Invalid MAC address.";
        return false;
    }
    if (!IsValidName(name)) {
        error = "Name must be 1-" + String(MAX_NAME_LENGTH) + " letters, digits, '-' or '_'.";
        return false;
    }
    {
        Lock lock(pMutex);
        if (!lock.IsLocked()) {
            error = "Node table is busy.";
            return false;
        }
        if (FindByName(name) != nullptr) {
            error = "A node with that name already exists.";
            return false;
        }
        if (FreeSlot() < 0) {
            error = "Maximum number of nodes reached.";
            return false;
        }
    }

    if (!BeginAdoption(address, name, -1, error)) return false;
    Wake();
    return true;
}

bool espnowbus::Replace(const String& name, const String& mac, String& error) noexcept {
    uint8_t address[6];
    if (!ParseMac(mac, address)) {
        error = "Invalid MAC address.";
        return false;
    }

    int8_t index = -1;
    {
        Lock lock(pMutex);
        if (!lock.IsLocked()) {
            error = "Node table is busy.";
            return false;
        }
        Node* node = FindByName(name);
        if (node == nullptr) {
            error = "Node '" + name + "' not found.";
            return false;
        }
        index = static_cast<int8_t>(node - pNodes);
    }

    if (!BeginAdoption(address, name, index, error)) return false;
    Wake();
    return true;
}

espnowbus::AdoptionState espnowbus::Adoption(String& message) const noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return AdoptionState::Idle;
    message = pAdoptionMessage;
    return pAdoptionState;
}

bool espnowbus::Identify(const String& selector, String& error) noexcept {
    {
        Lock lock(pMutex);
        if (!lock.IsLocked()) {
            error = "Node table is busy.";
            return false;
        }

        uint8_t mac[6];
        Pending* pending = ParseMac(selector, mac) ? FindPending(mac) : nullptr;
        Node* node = FindByName(selector);
        if (pending != nullptr) {
            pending->identifyRequested = true;
        } else if (node != nullptr && node->online) {
            node->identifyRequested = true;
        } else {
            error = node != nullptr ? "Node is offline." : "Device not found.";
            return false;
        }
    }
    Wake();
    return true;
}

bool espnowbus::Rename(const String& name, const String& newName, String& error) noexcept {
    if (!IsValidName(newName)) {
        error = "Name must be 1-" + String(MAX_NAME_LENGTH) + " letters, digits, '-' or '_'.";
        return false;
    }
    if (!HasNode(name)) {
        error = "Node '" + name + "' not found.";
        return false;
    }
    if (!name.equalsIgnoreCase(newName) && HasNode(newName)) {
        error = "A node with that name already exists.";
        return false;
    }
    if (!Settings.RenameNode(name, newName, error)) return false;

    Lock lock(pMutex);
    if (!lock.IsLocked()) return false;
    Node* node = FindByName(name);
    if (node != nullptr) node->name = newName;
    return true;
}

bool espnowbus::Remove(const String& name, String& error) noexcept {
    if (!HasNode(name)) {
        error = "Node '" + name + "' not found.";
        return false;
    }
    if (!Settings.RemoveNode(name, error)) return false;

    {
        Lock lock(pMutex);
        if (!lock.IsLocked()) return false;

        Node* node = FindByName(name);
        if (node == nullptr) return true;
        if (node->online) node->forgetFrames = FORGET_FRAME_COUNT;
        else ClearNode(*node);
    }
    Wake();
    return true;
}

bool espnowbus::IsValidName(const String& name) noexcept {
    if (name.isEmpty() || name.length() > MAX_NAME_LENGTH) return false;
    for (size_t index = 0; index < name.length(); ++index) {
        const char character = name.charAt(index);
        const bool valid = isalnum(static_cast<unsigned char>(character)) || character == '-' || character == '_';
        if (!valid) return false;
    }
    return true;
}

void espnowbus::TaskEntry(void* parameter) {
    static_cast<espnowbus*>(parameter)->Task();
}

// Runs on the WiFi task: copy and get out. A full queue just drops the frame,
// which every exchange here already survives through its retries.
void espnowbus::OnReceive(const esp_now_recv_info_t* info, const uint8_t* data, int length) {
    if (Instance == nullptr || info == nullptr || length <= 0 || length > static_cast<int>(MaxFrameLength)) return;

    ReceivedFrame frame;
    memcpy(frame.mac, info->src_addr, sizeof(frame.mac));
    frame.length = static_cast<uint8_t>(length);
    memcpy(frame.data, data, static_cast<size_t>(length));
    if (xQueueSend(Instance->pReceiveQueue, &frame, 0) == pdTRUE) Instance->Wake();
}

void espnowbus::Task() {
    ReceivedFrame frame;
    while (true) {
        (void)ulTaskNotifyTake(pdTRUE, IDLE_WAIT);

        const TickType_t now = xTaskGetTickCount();
        if (Elapsed(now, pNextRadioCheckAt)) {
            pNextRadioCheckAt = now + RADIO_CHECK_INTERVAL;
            ServiceRadio();
        }
        if (!pRadioStarted) continue;

        while (xQueueReceive(pReceiveQueue, &frame, 0) == pdTRUE) HandleFrame(frame);
        Tick(xTaskGetTickCount());
    }
}

void espnowbus::Wake() noexcept {
    if (pTaskHandle != nullptr) xTaskNotifyGive(pTaskHandle);
}

// The network task switches WiFi modes on its own schedule (fallback AP,
// reconnects) and offers no hook before doing so, so the mode is polled. Peers
// are bound to an interface, hence the full restart when it changes.
void espnowbus::ServiceRadio() {
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK) mode = WIFI_MODE_NULL;

    if (pRadioStarted && static_cast<int>(mode) == pRadioMode) return;
    if (pRadioStarted) StopRadio();
    if (mode != WIFI_MODE_NULL) {
        pRadioMode = static_cast<int>(mode);
        pInterface = (mode & WIFI_MODE_STA) != 0 ? WIFI_IF_STA : WIFI_IF_AP;
        if (!StartRadio()) Logger.Log("ESP-NOW: unable to start", logger::LogLevels::Error);
    }
}

bool espnowbus::StartRadio() {
    // Modem sleep makes a station miss frames between DTIM beacons, which
    // here would turn into seconds of input lag.
    WiFi.setSleep(false);

    if (esp_now_init() != ESP_OK) return false;
    if (esp_now_register_recv_cb(OnReceive) != ESP_OK) {
        esp_now_deinit();
        return false;
    }

    {
        Lock lock(pMutex);
        if (lock.IsLocked()) {
            for (const Node& node : pNodes) if (node.used) EnsurePeer(node.mac);
            for (const Pending& pending : pPending) if (pending.used) EnsurePeer(pending.mac);
        }
    }

    pRadioStarted = true;
    Logger.Log("ESP-NOW: running on channel " + String(Channel()), logger::LogLevels::Information);
    return true;
}

void espnowbus::StopRadio() {
    esp_now_deinit();
    pRadioStarted = false;

    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    for (Node& node : pNodes) if (node.used) MarkOffline(node);
}

void espnowbus::EnsurePeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) return;

    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, mac, sizeof(peer.peer_addr));
    peer.channel = 0; // Whatever channel the interface is on.
    peer.ifidx = static_cast<wifi_interface_t>(pInterface);
    peer.encrypt = false;
    (void)esp_now_add_peer(&peer);
}

void espnowbus::Send(const OutgoingFrame& frame) {
    if (frame.length == 0 || !pRadioStarted) return;
    (void)esp_now_send(frame.mac, frame.data, frame.length);
}

void espnowbus::HandleFrame(const ReceivedFrame& frame) {
    Header header;
    const uint8_t* body = nullptr;
    size_t bodyLength = 0;
    if (!Parse(frame.data, frame.length, header, body, bodyLength)) return;

    const Message type = static_cast<Message>(header.type);
    if (type == Message::Announce) {
        HandleAnnounce(frame, body, bodyLength);
        return;
    }
    if (type == Message::Adopted) {
        HandleAdopted(frame, body, bodyLength);
        return;
    }

    OutgoingFrame reply;
    {
        Lock lock(pMutex);
        if (!lock.IsLocked()) return;

        Node* node = FindByMac(frame.mac);
        if (node == nullptr) return;

        if (type == Message::Hello) {
            HandleHello(*node, frame, body, bodyLength, reply);
        } else {
            const bool current = node->session && Verify(frame.data, frame.length, node->key, node->controllerNonce, node->nodeNonce);
            const bool candidate = !current && node->candidateNonce != 0 &&
                Verify(frame.data, frame.length, node->key, node->controllerNonce, node->candidateNonce);
            if (!current && !candidate) return;

            if (candidate) {
                // Tagged with the candidate nonce: only the node holding the
                // key can have produced this, so the session is real.
                node->nodeNonce = node->candidateNonce;
                node->candidateNonce = 0;
                node->rxSequence = 0;
                node->session = true;
                node->reportedCrc = node->candidateCrc;
                node->configSynced = node->reportedCrc == ConfigCrc(*node);
                node->configRejected = false;
                node->reportedKnown = false;
            }
            if (header.sequence <= node->rxSequence) return;
            node->rxSequence = header.sequence;

            const bool wasOnline = node->online;
            node->online = true;
            node->seen = true;
            node->lastHeardAt = xTaskGetTickCount();
            if (!wasOnline) Logger.Log("ESP-NOW: node '" + node->name + "' online", logger::LogLevels::Information);

            HandleSessionFrame(*node, header, body, bodyLength, reply);
        }
    }
    Send(reply);
}

void espnowbus::HandleAnnounce(const ReceivedFrame& frame, const uint8_t* body, size_t bodyLength) {
    AnnounceBody announce;
    if (!ReadBody(body, bodyLength, announce)) return;

    OutgoingFrame reply;
    {
        Lock lock(pMutex);
        if (!lock.IsLocked() || !pDiscoveryOpen || FindByMac(frame.mac) != nullptr) return;

        Pending* pending = FindPending(frame.mac);
        if (pending == nullptr) {
            for (Pending& candidate : pPending) {
                if (!candidate.used) {
                    pending = &candidate;
                    break;
                }
            }
            if (pending == nullptr) return;

            *pending = Pending{};
            pending->used = true;
            memcpy(pending->mac, frame.mac, sizeof(pending->mac));
            EnsurePeer(frame.mac);
            Logger.Log("ESP-NOW: device " + FormatMac(frame.mac) + " waiting for adoption", logger::LogLevels::Information);
        }
        pending->firmware = announce.firmware;
        pending->pinMask = announce.pinMask;
        pending->lastHeardAt = xTaskGetTickCount();

        memcpy(reply.mac, frame.mac, sizeof(reply.mac));
        reply.length = Seal(reply.data, Message::Discovered, 0, nullptr, 0, nullptr, 0, 0);
    }
    Send(reply);
}

void espnowbus::HandleAdopted(const ReceivedFrame& frame, const uint8_t* body, size_t bodyLength) {
    AdoptedBody adopted;
    if (!ReadBody(body, bodyLength, adopted)) return;

    {
        Lock lock(pMutex);
        if (!lock.IsLocked() || !pJob.active || memcmp(frame.mac, pJob.mac, sizeof(pJob.mac)) != 0) return;
        if (adopted.challenge != pJob.challenge || !Verify(frame.data, frame.length, pJob.key, pJob.challenge, 0)) return;
    }
    FinishAdoption();
}

// Commits the adoption. Takes pMutex only around the table update: the write
// to config.json must not stall components calling into this class.
void espnowbus::FinishAdoption() {
    AdoptionJob job;
    {
        Lock lock(pMutex);
        if (!lock.IsLocked()) return;
        job = pJob;
        pJob.active = false;
    }

    char key[KeyLength * 2 + 1];
    for (size_t index = 0; index < KeyLength; ++index) snprintf(key + index * 2, 3, "%02x", job.key[index]);

    String error;
    const bool saved = Settings.SaveNode(job.name, FormatMac(job.mac), String(key), error);

    Lock lock(pMutex);
    if (!lock.IsLocked()) return;

    if (!saved) {
        pAdoptionState = AdoptionState::Failed;
        pAdoptionMessage = "Unable to save node: " + error;
        Logger.Log("ESP-NOW: " + pAdoptionMessage, logger::LogLevels::Error);
        return;
    }

    const int slot = job.replaces >= 0 ? job.replaces : FreeSlot();
    if (slot < 0) {
        pAdoptionState = AdoptionState::Failed;
        pAdoptionMessage = "Node table is full.";
        return;
    }

    Node& node = pNodes[slot];
    if (job.replaces >= 0) {
        // The old board is left adopted but orphaned: nothing tells it apart
        // from an offline node until a reset or reflash clears it.
        esp_now_del_peer(node.mac);
    } else {
        ClearNode(node);
        node.used = true;
        node.name = job.name;
    }
    memcpy(node.mac, job.mac, sizeof(node.mac));
    memcpy(node.key, job.key, sizeof(node.key));
    node.controllerNonce = RandomNonce();
    node.txSequence = 0;
    node.firmware = 0;
    node.seen = false;
    ResetSession(node);
    EnsurePeer(node.mac);

    Pending* pending = FindPending(job.mac);
    if (pending != nullptr) DropPending(*pending);

    pAdoptionState = AdoptionState::Succeeded;
    pAdoptionMessage = job.name + " adopted.";
    Logger.Log("ESP-NOW: adopted " + FormatMac(job.mac) + " as '" + job.name + "'", logger::LogLevels::Information);
}

void espnowbus::HandleHello(Node& node, const ReceivedFrame& frame, const uint8_t* body, size_t bodyLength, OutgoingFrame& reply) {
    HelloBody hello;
    if (!ReadBody(body, bodyLength, hello) || hello.nodeNonce == 0) return;
    if (!Verify(frame.data, frame.length, node.key, 0, hello.nodeNonce)) return;

    node.candidateNonce = hello.nodeNonce;
    node.candidateCrc = hello.configCrc;
    node.firmware = hello.firmware;
    node.pinMask = hello.pinMask;

    WelcomeBody welcome{node.controllerNonce, hello.nodeNonce};
    memcpy(reply.mac, node.mac, sizeof(reply.mac));
    reply.length = Seal(reply.data, Message::Welcome, ++node.txSequence, &welcome, sizeof(welcome), node.key, node.controllerNonce, hello.nodeNonce);
}

void espnowbus::HandleSessionFrame(Node& node, const Header& header, const uint8_t* body, size_t bodyLength, OutgoingFrame& reply) {
    switch (static_cast<Message>(header.type)) {
        case Message::State: {
            StateBody state;
            if (!ReadBody(body, bodyLength, state)) return;
            node.reported = state.levels;
            node.reportedKnown = true;
            node.nextSyncAt = xTaskGetTickCount();
            SealFor(node, Message::Pong, nullptr, 0, reply);
            return;
        }
        case Message::Input: {
            InputBody input;
            if (!ReadBody(body, bodyLength, input) || input.pin > MaxPin) return;

            const uint16_t mask = static_cast<uint16_t>(1U << input.pin);
            node.reported = input.level ? static_cast<uint16_t>(node.reported | mask) : static_cast<uint16_t>(node.reported & ~mask);
            InputAckBody acknowledgement{input.changeId};
            SealFor(node, Message::InputAck, &acknowledgement, sizeof(acknowledgement), reply);
            return;
        }
        case Message::ConfigAck: {
            ConfigAckBody acknowledgement;
            if (!ReadBody(body, bodyLength, acknowledgement)) return;
            if (acknowledgement.configCrc != ConfigCrc(node)) return;

            node.configSynced = acknowledgement.accepted != 0;
            node.configRejected = acknowledgement.accepted == 0;
            if (node.configRejected) {
                Logger.Log("ESP-NOW: node '" + node.name + "' rejected its port configuration", logger::LogLevels::Error);
            }
            return;
        }
        default:
            return;
    }
}

void espnowbus::Tick(TickType_t now) {
    TickAdoption(now);
    TickPending(now);
    for (size_t index = 0; index < MAX_NODES; ++index) TickNode(index, now);
}

void espnowbus::TickAdoption(TickType_t now) {
    OutgoingFrame frame;
    {
        Lock lock(pMutex);
        if (!lock.IsLocked() || !pJob.active || !Elapsed(now, pJob.nextAttemptAt)) return;

        if (pJob.attempts >= ADOPT_MAX_ATTEMPTS) {
            pJob.active = false;
            pAdoptionState = AdoptionState::Failed;
            pAdoptionMessage = "No answer from " + FormatMac(pJob.mac) + ".";
            Logger.Log("ESP-NOW: adoption failed: " + pAdoptionMessage, logger::LogLevels::Warning);
            return;
        }

        ++pJob.attempts;
        pJob.nextAttemptAt = now + ADOPT_RETRY_INTERVAL;

        AdoptBody adopt{};
        memcpy(adopt.key, pJob.key, sizeof(adopt.key));
        adopt.challenge = pJob.challenge;
        memcpy(frame.mac, pJob.mac, sizeof(frame.mac));
        frame.length = Seal(frame.data, Message::Adopt, pJob.attempts, &adopt, sizeof(adopt), nullptr, 0, 0);
    }
    Send(frame);
}

void espnowbus::TickPending(TickType_t now) {
    for (size_t index = 0; index < MAX_PENDING; ++index) {
        OutgoingFrame frame;
        {
            Lock lock(pMutex);
            if (!lock.IsLocked()) return;

            if (pDiscoveryOpen && Elapsed(now, pDiscoveryUntil)) {
                pDiscoveryOpen = false;
                for (Pending& pending : pPending) if (pending.used) DropPending(pending);
                Logger.Log("ESP-NOW: discovery window closed", logger::LogLevels::Information);
            }

            Pending& pending = pPending[index];
            if (!pending.used) continue;
            if (Elapsed(now, pending.lastHeardAt + PENDING_TIMEOUT)) {
                DropPending(pending);
                continue;
            }
            if (!pending.identifyRequested) continue;

            pending.identifyRequested = false;
            memcpy(frame.mac, pending.mac, sizeof(frame.mac));
            frame.length = Seal(frame.data, Message::Identify, 0, nullptr, 0, nullptr, 0, 0);
        }
        Send(frame);
    }
}

void espnowbus::TickNode(size_t index, TickType_t now) {
    OutgoingFrame frame;
    bool forgotten = false;
    {
        Lock lock(pMutex);
        if (!lock.IsLocked()) return;

        Node& node = pNodes[index];
        if (!node.used) return;

        if (node.forgetFrames > 0) {
            --node.forgetFrames;
            SealFor(node, Message::Forget, nullptr, 0, frame);
            forgotten = node.forgetFrames == 0;
        } else if (node.online && Elapsed(now, node.lastHeardAt + OFFLINE_TIMEOUT)) {
            MarkOffline(node);
            return;
        } else if (!node.session) {
            return;
        } else if (node.identifyRequested) {
            node.identifyRequested = false;
            SealFor(node, Message::Identify, nullptr, 0, frame);
        } else if (!Elapsed(now, node.nextSyncAt)) {
            return;
        } else if (!node.configSynced) {
            if (node.configRejected) return;
            const ConfigBody config = BuildConfig(node);
            SealFor(node, Message::Config, &config, sizeof(config), frame);
            node.nextSyncAt = now + CONFIG_RETRY_INTERVAL;
        } else if (node.reportedKnown && ((node.reported ^ node.desired) & OutputMask(node)) != 0) {
            const SetBody set{node.desired};
            SealFor(node, Message::Set, &set, sizeof(set), frame);
            node.nextSyncAt = now + SET_RETRY_INTERVAL;
        } else {
            return;
        }
    }
    Send(frame);

    if (forgotten) {
        Lock lock(pMutex);
        if (lock.IsLocked()) ClearNode(pNodes[index]);
    }
}

size_t espnowbus::SealFor(Node& node, Message type, const void* body, size_t bodyLength, OutgoingFrame& frame) noexcept {
    memcpy(frame.mac, node.mac, sizeof(frame.mac));
    frame.length = Seal(frame.data, type, ++node.txSequence, body, bodyLength, node.key, node.controllerNonce, node.nodeNonce);
    return frame.length;
}

espnowbus::Node* espnowbus::FindByMac(const uint8_t* mac) noexcept {
    for (Node& node : pNodes) {
        if (node.used && memcmp(node.mac, mac, sizeof(node.mac)) == 0) return &node;
    }
    return nullptr;
}

espnowbus::Node* espnowbus::FindByName(const String& name) noexcept {
    for (Node& node : pNodes) {
        if (node.used && node.name.equalsIgnoreCase(name)) return &node;
    }
    return nullptr;
}

const espnowbus::Node* espnowbus::FindByName(const String& name) const noexcept {
    for (const Node& node : pNodes) {
        if (node.used && node.name.equalsIgnoreCase(name)) return &node;
    }
    return nullptr;
}

espnowbus::Pending* espnowbus::FindPending(const uint8_t* mac) noexcept {
    for (Pending& pending : pPending) {
        if (pending.used && memcmp(pending.mac, mac, sizeof(pending.mac)) == 0) return &pending;
    }
    return nullptr;
}

int espnowbus::FreeSlot() const noexcept {
    for (size_t index = 0; index < MAX_NODES; ++index) {
        if (!pNodes[index].used) return static_cast<int>(index);
    }
    return -1;
}

void espnowbus::ResetSession(Node& node) noexcept {
    node.session = false;
    node.online = false;
    node.nodeNonce = 0;
    node.candidateNonce = 0;
    node.rxSequence = 0;
    node.reportedKnown = false;
    node.configSynced = false;
    node.configRejected = false;
}

void espnowbus::MarkOffline(Node& node) noexcept {
    const bool wasOnline = node.online;
    ResetSession(node);
    if (wasOnline) Logger.Log("ESP-NOW: node '" + node.name + "' offline", logger::LogLevels::Warning);
}

void espnowbus::DropPending(Pending& pending) noexcept {
    pending.used = false;
    if (pRadioStarted && FindByMac(pending.mac) == nullptr) (void)esp_now_del_peer(pending.mac);
}

void espnowbus::ClearNode(Node& node) noexcept {
    if (pRadioStarted && node.used) (void)esp_now_del_peer(node.mac);
    node = Node{};
}

ConfigBody espnowbus::BuildConfig(const Node& node) noexcept {
    ConfigBody config{};
    config.count = node.portCount;
    memcpy(config.ports, node.ports, sizeof(PortConfig) * node.portCount);
    return config;
}

uint32_t espnowbus::ConfigCrc(const Node& node) noexcept {
    const ConfigBody config = BuildConfig(node);
    return Crc32(reinterpret_cast<const uint8_t*>(&config), sizeof(config));
}

uint16_t espnowbus::OutputMask(const Node& node) noexcept {
    uint16_t mask = 0;
    for (uint8_t index = 0; index < node.portCount; ++index) {
        if (node.ports[index].mode == static_cast<uint8_t>(PortMode::Output)) mask |= static_cast<uint16_t>(1U << node.ports[index].pin);
    }
    return mask;
}

bool espnowbus::ParseMac(const String& text, uint8_t* mac) noexcept {
    unsigned values[6];
    char trailing = 0;
    if (sscanf(text.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x%c", &values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &trailing) != 6) return false;
    for (size_t index = 0; index < 6; ++index) mac[index] = static_cast<uint8_t>(values[index]);
    return true;
}

String espnowbus::FormatMac(const uint8_t* mac) {
    char text[18];
    snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(text);
}
