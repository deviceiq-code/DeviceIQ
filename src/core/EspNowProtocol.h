#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Wire format between a DeviceIQ controller and its ESP-NOW nodes. This file
// is deliberately free of Arduino/ESP-IDF includes and must stay byte-identical
// to DeviceIQ-Node's src/EspNowProtocol.h: the two firmwares are built and
// flashed separately, and Version below is what keeps a mismatched pair from
// talking at all rather than misreading each other's frames.
//
// Frame: Header | body | 8-byte truncated HMAC-SHA256 tag. The tag covers both
// session nonces, then the header and body. The controller and node each pick
// a fresh nonce per session (see Hello/Welcome), so a frame captured in an
// earlier session never verifies; inside one session Header::sequence must
// strictly increase, so one can't be replayed either. ESP-NOW's own encryption
// isn't used: it caps out at about 6 encrypted peers and has no replay
// protection, and integrity is what matters for these frames.
namespace espnowprotocol {
    constexpr uint8_t Version = 1;
    constexpr size_t KeyLength = 16;
    constexpr size_t TagLength = 8;
    constexpr size_t MaxPorts = 8;
    constexpr size_t MaxFrameLength = 250;
    constexpr uint8_t MaxPin = 15;

    enum class Message : uint8_t {
        // Unauthenticated: an unadopted node has no key yet.
        Announce = 1,   // node -> broadcast: AnnounceBody
        Discovered,     // controller -> node: hold this channel, adoption may follow
        Adopt,          // controller -> node: AdoptBody
        // Authenticated.
        Adopted,        // node -> controller: AdoptedBody, tagged with the new key
        Hello,          // node -> controller: HelloBody, opens a session
        Welcome,        // controller -> node: WelcomeBody
        Config,         // controller -> node: ConfigBody
        ConfigAck,      // node -> controller: ConfigAckBody
        Set,            // controller -> node: SetBody
        State,          // node -> controller: StateBody, on change and as heartbeat
        Pong,           // controller -> node: acknowledges State
        Input,          // node -> controller: InputBody
        InputAck,       // controller -> node: InputAckBody
        Forget,         // controller -> node: wipe adoption and restart
        // Authenticated for an adopted node, plain for an unadopted one.
        Identify        // controller -> node: blink the onboard LED
    };

    enum class PortMode : uint8_t { Output = 0, Input, InputPullUp };

    #pragma pack(push, 1)
    struct Header {
        uint8_t magic[2];
        uint8_t version;
        uint8_t type;
        uint32_t sequence;
    };

    struct AnnounceBody {
        uint16_t firmware;
        uint16_t pinMask;
    };

    struct AdoptBody {
        uint8_t key[KeyLength];
        uint32_t challenge;
    };

    struct AdoptedBody {
        uint32_t challenge;
    };

    struct HelloBody {
        uint32_t nodeNonce;
        uint32_t configCrc;
        uint16_t firmware;
        uint16_t pinMask;
    };

    struct WelcomeBody {
        uint32_t controllerNonce;
        uint32_t nodeNonce;
    };

    struct PortConfig {
        uint8_t pin;
        uint8_t mode;
        uint8_t safeLevel;
        uint8_t reserved;
        uint16_t autoOffMs; // 0 = latching. Outputs only.
    };

    struct ConfigBody {
        uint8_t count;
        PortConfig ports[MaxPorts];
    };

    struct ConfigAckBody {
        uint32_t configCrc;
        uint8_t accepted;
    };

    // Every output at once: resending it is idempotent, so a lost frame or a
    // duplicate can't leave a node in the wrong state.
    struct SetBody {
        uint16_t levels;
    };

    struct StateBody {
        uint16_t levels; // Outputs and inputs, one bit per GPIO.
    };

    struct InputBody {
        uint16_t changeId;
        uint8_t pin;
        uint8_t level;
    };

    struct InputAckBody {
        uint16_t changeId;
    };
    #pragma pack(pop)

    static_assert(sizeof(Header) == 8, "Header layout is part of the wire format");
    static_assert(sizeof(PortConfig) == 6, "PortConfig layout is part of the wire format");
    static_assert(sizeof(ConfigBody) + sizeof(Header) + TagLength <= MaxFrameLength, "ConfigBody must fit one ESP-NOW frame");

    // HMAC-SHA256 over nonces[8] + data, truncated to TagLength. Implemented
    // per platform (mbedTLS on the controller, BearSSL on the node).
    void Hmac(const uint8_t* key, const uint8_t* nonces, const uint8_t* data, size_t length, uint8_t* tag);

    inline uint32_t Crc32(const uint8_t* data, size_t length) {
        uint32_t crc = 0xFFFFFFFFUL;
        for (size_t index = 0; index < length; ++index) {
            crc ^= data[index];
            for (uint8_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
        }
        return ~crc;
    }

    inline void PackNonces(uint32_t controllerNonce, uint32_t nodeNonce, uint8_t* nonces) {
        memcpy(nonces, &controllerNonce, sizeof(controllerNonce));
        memcpy(nonces + sizeof(controllerNonce), &nodeNonce, sizeof(nodeNonce));
    }

    // Builds a frame into `frame` (MaxFrameLength bytes) and returns its
    // length, or 0 if the body doesn't fit. A null key leaves the tag zeroed,
    // for the unauthenticated messages.
    inline size_t Seal(uint8_t* frame, Message type, uint32_t sequence, const void* body, size_t bodyLength,
        const uint8_t* key, uint32_t controllerNonce, uint32_t nodeNonce) {
        const size_t length = sizeof(Header) + bodyLength + TagLength;
        if (length > MaxFrameLength) return 0;

        Header header{};
        header.magic[0] = 'D';
        header.magic[1] = 'Q';
        header.version = Version;
        header.type = static_cast<uint8_t>(type);
        header.sequence = sequence;
        memcpy(frame, &header, sizeof(header));
        if (bodyLength > 0) memcpy(frame + sizeof(header), body, bodyLength);

        uint8_t* tag = frame + sizeof(header) + bodyLength;
        memset(tag, 0, TagLength);
        if (key != nullptr) {
            uint8_t nonces[8];
            PackNonces(controllerNonce, nodeNonce, nonces);
            Hmac(key, nonces, frame, sizeof(header) + bodyLength, tag);
        }
        return length;
    }

    // Structural check only: says nothing about who sent the frame.
    inline bool Parse(const uint8_t* frame, size_t length, Header& header, const uint8_t*& body, size_t& bodyLength) {
        if (length < sizeof(Header) + TagLength || length > MaxFrameLength) return false;
        memcpy(&header, frame, sizeof(header));
        if (header.magic[0] != 'D' || header.magic[1] != 'Q' || header.version != Version) return false;
        body = frame + sizeof(Header);
        bodyLength = length - sizeof(Header) - TagLength;
        return true;
    }

    inline bool Verify(const uint8_t* frame, size_t length, const uint8_t* key, uint32_t controllerNonce, uint32_t nodeNonce) {
        if (length < sizeof(Header) + TagLength) return false;

        uint8_t nonces[8];
        uint8_t expected[TagLength];
        PackNonces(controllerNonce, nodeNonce, nonces);
        Hmac(key, nonces, frame, length - TagLength, expected);

        const uint8_t* received = frame + length - TagLength;
        uint8_t difference = 0;
        for (size_t index = 0; index < TagLength; ++index) difference |= static_cast<uint8_t>(expected[index] ^ received[index]);
        return difference == 0;
    }
}
