#pragma once

#include <atomic>
#include <memory>
#include <utility>

#include <Arduino.h>

#include "Component.h"

class DHT;
class DallasTemperature;
class OneWire;

class thermometer final : public component {
    public:
        enum class ThermometerTypes : uint8_t { Dht11, Dht12, Dht21, Dht22, Ds18b20 };

        enum EventCodes : uint16_t {
            TemperatureChanged = 1,
            HumidityChanged,
            Changed,
            ReadFailed
        };

        static constexpr uint32_t DEFAULT_POLLING_INTERVAL_MS = 5000;
        static constexpr uint32_t MINIMUM_POLLING_INTERVAL_MS = 1000;

        thermometer(
            String name,
            int16_t id,
            Buses bus,
            uint8_t address,
            ThermometerTypes type = ThermometerTypes::Ds18b20,
            uint32_t pollingIntervalMs = DEFAULT_POLLING_INTERVAL_MS,
            bool enabled = true
        );
        ~thermometer() override;

        [[nodiscard]] Classes Class() const noexcept override { return Classes::Thermometer; }
        [[nodiscard]] ThermometerTypes Type() const noexcept { return pType; }
        [[nodiscard]] uint32_t PollingInterval() const noexcept { return pPollingIntervalMs; }
        [[nodiscard]] bool HasHumidity() const noexcept { return pType != ThermometerTypes::Ds18b20; }
        [[nodiscard]] bool Available() const noexcept { return pAvailable.load(std::memory_order_relaxed); }
        [[nodiscard]] float Temperature() const noexcept;
        [[nodiscard]] float Humidity() const noexcept;

        void GetInfo(String& output) const override;
        [[nodiscard]] static const char* TypeName(ThermometerTypes value) noexcept;
        [[nodiscard]] static bool ParseType(const String& value, ThermometerTypes& result) noexcept;

    protected:
        bool Configure() noexcept override;
        bool Initialize() noexcept override;
        void EnabledChanged(bool enabled) noexcept override;
        void Control(TickType_t now) override;
        const ComponentDescriptor* EventDescriptors(size_t& count) const noexcept override;

    private:
        static constexpr int32_t INVALID_READING = INT32_MIN;
        static constexpr uint32_t DS18B20_CONVERSION_TIME_MS = 750;

        [[nodiscard]] bool ReadDht(float& temperature, float& humidity) noexcept;
        [[nodiscard]] bool ReadDht12(float& temperature, float& humidity) noexcept;
        [[nodiscard]] bool ReadDs18b20(float& temperature) noexcept;
        void BeginDs18b20Conversion(TickType_t now) noexcept;
        void ApplyReading(float temperature, float humidity) noexcept;
        void ReportReadFailure() noexcept;
        [[nodiscard]] static int32_t Encode(float value) noexcept;
        [[nodiscard]] static float Decode(int32_t value) noexcept;
        [[nodiscard]] static bool Elapsed(TickType_t now, TickType_t since, TickType_t interval) noexcept;

        const ThermometerTypes pType;
        const uint32_t pPollingIntervalMs;
        const TickType_t pPollingIntervalTicks;
        const TickType_t pDs18b20ConversionTicks;
        std::atomic<int32_t> pTemperature{INVALID_READING};
        std::atomic<int32_t> pHumidity{INVALID_READING};
        std::atomic<bool> pAvailable{false};
        std::unique_ptr<DHT> pDht;
        std::unique_ptr<OneWire> pOneWire;
        std::unique_ptr<DallasTemperature> pDallas;
        TickType_t pLastPollAt = 0;
        TickType_t pConversionStartedAt = 0;
        bool pConversionPending = false;
        bool pFailureReported = false;
};
