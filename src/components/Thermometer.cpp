#include "Thermometer.h"

#include <cmath>
#include <new>

#include <DHT.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <freertos/task.h>

thermometer::thermometer(
    String name,
    int16_t id,
    Buses bus,
    uint8_t address,
    ThermometerTypes type,
    uint32_t pollingIntervalMs,
    bool enabled
) :
    component(std::move(name), id, bus, address, enabled),
    pType(type),
    pPollingIntervalMs(pollingIntervalMs),
    pPollingIntervalTicks(pdMS_TO_TICKS(pollingIntervalMs)),
    pDs18b20ConversionTicks(pdMS_TO_TICKS(DS18B20_CONVERSION_TIME_MS)) {}

thermometer::~thermometer() = default;

float thermometer::Temperature() const noexcept {
    return Decode(pTemperature.load(std::memory_order_relaxed));
}

float thermometer::Humidity() const noexcept {
    return Decode(pHumidity.load(std::memory_order_relaxed));
}

void thermometer::GetInfo(String& output) const {
    component::GetInfo(output);
    output += "Type           | " + String(TypeName(Type())) + "\r\n";
    output += "PollingMs      | " + String(PollingInterval()) + "\r\n";
    output += "Available      | " + String(Available() ? "true" : "false") + "\r\n";
    output += "Temperature    | " + String(Available() ? String(Temperature(), 2) : String("unavailable")) + "\r\n";
    if (HasHumidity()) {
        output += "Humidity       | " + String(Available() ? String(Humidity(), 2) : String("unavailable")) + "\r\n";
    }
}

const char* thermometer::TypeName(ThermometerTypes value) noexcept {
    switch (value) {
        case ThermometerTypes::Dht11: return "DHT11";
        case ThermometerTypes::Dht12: return "DHT12";
        case ThermometerTypes::Dht21: return "DHT21";
        case ThermometerTypes::Dht22: return "DHT22";
        case ThermometerTypes::Ds18b20: return "DS18B20";
        default: return "Unknown";
    }
}

bool thermometer::ParseType(const String& value, ThermometerTypes& result) noexcept {
    if (value.equalsIgnoreCase("DHT11")) result = ThermometerTypes::Dht11;
    else if (value.equalsIgnoreCase("DHT12")) result = ThermometerTypes::Dht12;
    else if (value.equalsIgnoreCase("DHT21")) result = ThermometerTypes::Dht21;
    else if (value.equalsIgnoreCase("DHT22")) result = ThermometerTypes::Dht22;
    else if (value.equalsIgnoreCase("DS18B20")) result = ThermometerTypes::Ds18b20;
    else return false;
    return true;
}

bool thermometer::Configure() noexcept {
    if (pPollingIntervalMs < MINIMUM_POLLING_INTERVAL_MS) return false;

    if (pType == ThermometerTypes::Dht12 && Bus() == Buses::I2C) {
        return Bus() == Buses::I2C && Address() > 0 && Address() <= 0x7f;
    }

    return Bus() == Buses::Onboard && GPIO_IS_VALID_GPIO(static_cast<gpio_num_t>(Address()));
}

bool thermometer::Initialize() noexcept {
    if (pType == ThermometerTypes::Dht12 && Bus() == Buses::I2C) {
        Wire.begin();
    } else if (pType == ThermometerTypes::Ds18b20) {
        pOneWire.reset(new (std::nothrow) OneWire(Address()));
        if (!pOneWire) return false;
        pDallas.reset(new (std::nothrow) DallasTemperature(pOneWire.get()));
        if (!pDallas) return false;
        pDallas->begin();
        pDallas->setResolution(12);
        pDallas->setWaitForConversion(false);
    } else {
        uint8_t dhtType = DHT22;
        if (pType == ThermometerTypes::Dht11) dhtType = DHT11;
        else if (pType == ThermometerTypes::Dht12) dhtType = DHT12;
        else if (pType == ThermometerTypes::Dht21) dhtType = DHT21;
        pDht.reset(new (std::nothrow) DHT(Address(), dhtType));
        if (!pDht) return false;
        pDht->begin();
    }

    const TickType_t now = xTaskGetTickCount();
    pLastPollAt = static_cast<TickType_t>(now - pPollingIntervalTicks);
    if (pType == ThermometerTypes::Ds18b20 && Enabled()) BeginDs18b20Conversion(now);
    return true;
}

void thermometer::EnabledChanged(bool enabled) noexcept {
    if (!enabled) {
        pAvailable.store(false, std::memory_order_relaxed);
        pConversionPending = false;
        return;
    }

    const TickType_t now = xTaskGetTickCount();
    pLastPollAt = static_cast<TickType_t>(now - pPollingIntervalTicks);
    pFailureReported = false;
    if (pType == ThermometerTypes::Ds18b20) BeginDs18b20Conversion(now);
}

void thermometer::Control(TickType_t now) {
    if (pType == ThermometerTypes::Ds18b20) {
        if (pConversionPending) {
            if (!Elapsed(now, pConversionStartedAt, pDs18b20ConversionTicks)) return;

            pConversionPending = false;
            float temperature = NAN;
            if (ReadDs18b20(temperature)) ApplyReading(temperature, NAN);
            else ReportReadFailure();
            return;
        }

        if (Elapsed(now, pLastPollAt, pPollingIntervalTicks)) BeginDs18b20Conversion(now);
        return;
    }

    if (!Elapsed(now, pLastPollAt, pPollingIntervalTicks)) return;
    pLastPollAt = now;

    float temperature = NAN;
    float humidity = NAN;
    const bool success = pType == ThermometerTypes::Dht12 && Bus() == Buses::I2C
        ? ReadDht12(temperature, humidity)
        : ReadDht(temperature, humidity);
    if (success) ApplyReading(temperature, humidity);
    else ReportReadFailure();
}

const ComponentDescriptor* thermometer::EventDescriptors(size_t& count) const noexcept {
    static const ComponentDescriptor descriptors[] = {
        {"TemperatureChanged", EventCodes::TemperatureChanged},
        {"HumidityChanged", EventCodes::HumidityChanged},
        {"Changed", EventCodes::Changed},
        {"ReadFailed", EventCodes::ReadFailed},
        {"ReadRecovered", EventCodes::ReadRecovered}
    };
    count = sizeof(descriptors) / sizeof(descriptors[0]);
    return descriptors;
}

bool thermometer::ReadDht(float& temperature, float& humidity) noexcept {
    if (!pDht) return false;
    humidity = pDht->readHumidity();
    temperature = pDht->readTemperature();
    return std::isfinite(temperature) && std::isfinite(humidity);
}

bool thermometer::ReadDht12(float& temperature, float& humidity) noexcept {
    Wire.beginTransmission(Address());
    Wire.write(static_cast<uint8_t>(0));
    if (Wire.endTransmission() != 0) return false;

    if (Wire.requestFrom(Address(), static_cast<uint8_t>(5)) != 5) return false;
    uint8_t data[5];
    for (uint8_t& value : data) value = Wire.read();
    const uint8_t checksum = static_cast<uint8_t>(data[0] + data[1] + data[2] + data[3]);
    if (checksum != data[4]) return false;

    humidity = static_cast<float>(data[0]) + static_cast<float>(data[1]) * 0.1f;
    temperature = static_cast<float>(data[2]) + static_cast<float>(data[3] & 0x7f) * 0.1f;
    if ((data[3] & 0x80) != 0) temperature = -temperature;
    return std::isfinite(temperature) && std::isfinite(humidity);
}

bool thermometer::ReadDs18b20(float& temperature) noexcept {
    if (!pDallas || pDallas->getDeviceCount() == 0) return false;
    temperature = pDallas->getTempCByIndex(0);
    return std::isfinite(temperature) && temperature != DEVICE_DISCONNECTED_C &&
        temperature >= -55.0f && temperature <= 125.0f;
}

void thermometer::BeginDs18b20Conversion(TickType_t now) noexcept {
    if (!pDallas) return;
    pDallas->requestTemperatures();
    pLastPollAt = now;
    pConversionStartedAt = now;
    pConversionPending = true;
}

void thermometer::ApplyReading(float temperature, float humidity) noexcept {
    const int32_t encodedTemperature = Encode(temperature);
    const int32_t encodedHumidity = HasHumidity() ? Encode(humidity) : INVALID_READING;
    if (encodedTemperature == INVALID_READING || (HasHumidity() && encodedHumidity == INVALID_READING)) {
        ReportReadFailure();
        return;
    }

    const bool recovered = pFailureReported;
    const int32_t previousTemperature = pTemperature.exchange(encodedTemperature, std::memory_order_relaxed);
    const int32_t previousHumidity = pHumidity.exchange(encodedHumidity, std::memory_order_relaxed);
    const bool temperatureChanged = previousTemperature != encodedTemperature;
    const bool humidityChanged = HasHumidity() && previousHumidity != encodedHumidity;
    pAvailable.store(true, std::memory_order_relaxed);
    pFailureReported = false;

    if (recovered) (void)PublishEvent(EventCodes::ReadRecovered, encodedTemperature);

    if (temperatureChanged) (void)PublishEvent(EventCodes::TemperatureChanged, encodedTemperature);
    if (humidityChanged) (void)PublishEvent(EventCodes::HumidityChanged, encodedHumidity);
    if (temperatureChanged || humidityChanged) {
        MarkStateChanged();
        (void)PublishEvent(EventCodes::Changed, encodedTemperature);
    }
}

void thermometer::ReportReadFailure() noexcept {
    pAvailable.store(false, std::memory_order_relaxed);
    if (pFailureReported) return;
    pFailureReported = true;
    (void)PublishEvent(EventCodes::ReadFailed, 0);
}

int32_t thermometer::Encode(float value) noexcept {
    if (!std::isfinite(value)) return INVALID_READING;
    return static_cast<int32_t>(std::lround(value * 100.0f));
}

float thermometer::Decode(int32_t value) noexcept {
    return value == INVALID_READING ? NAN : static_cast<float>(value) / 100.0f;
}

bool thermometer::Elapsed(TickType_t now, TickType_t since, TickType_t interval) noexcept {
    return static_cast<TickType_t>(now - since) >= interval;
}
