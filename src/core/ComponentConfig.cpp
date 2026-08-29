#include "Settings.h"

#include <ArduinoJson.h>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <new>

#include "Globals.h"
#include "components/Blinds.h"
#include "components/Button.h"
#include "components/Relay.h"
#include "components/Thermometer.h"

namespace {
    constexpr size_t MaxConfiguredComponents = 32;
    constexpr uint8_t ComponentSchemaVersion = 1;

    bool ParseComponentID(const char* key, int16_t& id) {
        if (key == nullptr || *key == '\0') return false;
        char* end = nullptr;
        const long parsed = std::strtol(key, &end, 10);
        if (*end != '\0' || parsed < 1 || parsed > INT16_MAX || String(parsed) != key) return false;
        id = static_cast<int16_t>(parsed);
        return true;
    }

    JsonObjectConst ComponentSetup(JsonObjectConst object) {
        return object["Setup"].as<JsonObjectConst>();
    }

    JsonObject ComponentSetup(JsonObject object) {
        return object["Setup"].as<JsonObject>();
    }

    bool HasComponentSections(JsonObjectConst object) {
        if (!object["Setup"].is<JsonObjectConst>() ||
            !object["Properties"].is<JsonObjectConst>() ||
            !object["Events"].is<JsonObjectConst>()) return false;
        if (!object["Setup"]["ID"].isNull()) return false;

        size_t sectionCount = 0;
        for (JsonPairConst section : object) {
            const String name = section.key().c_str();
            if (name != "Setup" && name != "Properties" && name != "Events") return false;
            ++sectionCount;
        }
        return sectionCount == 3;
    }

    struct RelayConfiguration {
        String name;
        int16_t id = 0;
        uint8_t address = 0;
        relay::RelayTypes type = relay::RelayTypes::NormallyOpen;
        relay::DriveModes driveMode = relay::DriveModes::ActiveHigh;
        bool state = false;
        bool enabled = true;
    };

    bool ParseRelayConfiguration(JsonObjectConst object, int16_t id, RelayConfiguration& result) {
        if (!HasComponentSections(object)) return false;
        const JsonObjectConst setup = ComponentSetup(object);
        if (!setup["Name"].is<const char*>() ||
            !setup["Class"].is<const char*>() ||
            !setup["Bus"].is<const char*>() ||
            !setup["Address"].is<int>()) {
            return false;
        }

        result.name = setup["Name"].as<const char*>();
        result.name.trim();
        if (result.name.isEmpty()) return false;

        const int address = setup["Address"].as<int>();
        if (address < 0 || address > UINT8_MAX) return false;

        const String componentClass = setup["Class"].as<const char*>();
        const String bus = setup["Bus"].as<const char*>();
        if (!componentClass.equalsIgnoreCase("Relay") || !bus.equalsIgnoreCase("Onboard")) return false;

        result.id = id;
        result.address = static_cast<uint8_t>(address);

        if (!setup["Type"].isNull()) {
            if (!setup["Type"].is<const char*>()) return false;
            const String type = setup["Type"].as<const char*>();
            if (type.equalsIgnoreCase("NormallyOpen")) {
                result.type = relay::RelayTypes::NormallyOpen;
            } else if (type.equalsIgnoreCase("NormallyClosed")) {
                result.type = relay::RelayTypes::NormallyClosed;
            } else {
                return false;
            }
        }

        if (!setup["DriveMode"].isNull()) {
            if (!setup["DriveMode"].is<const char*>()) return false;
            const String driveMode = setup["DriveMode"].as<const char*>();
            if (driveMode.equalsIgnoreCase("ActiveHigh")) {
                result.driveMode = relay::DriveModes::ActiveHigh;
            } else if (driveMode.equalsIgnoreCase("ActiveLow")) {
                result.driveMode = relay::DriveModes::ActiveLow;
            } else {
                return false;
            }
        }

        if (!object["Properties"].isNull()) {
            if (!object["Properties"].is<JsonObjectConst>()) return false;
            const JsonObjectConst properties = object["Properties"].as<JsonObjectConst>();

            if (!properties["Enabled"].isNull()) {
                if (!properties["Enabled"].is<bool>()) return false;
                result.enabled = properties["Enabled"].as<bool>();
            }

            if (!properties["State"].isNull()) {
                if (!properties["State"].is<bool>()) return false;
                result.state = properties["State"].as<bool>();
            }
        }

        if (!object["Events"].isNull() && !object["Events"].is<JsonObjectConst>()) return false;
        return true;
    }

    struct ButtonConfiguration {
        String name;
        int16_t id = 0;
        uint8_t address = 0;
        button::ActiveLevels activeLevel = button::ActiveLevels::Low;
        button::InputModes inputMode = button::InputModes::PullUp;
        uint32_t debounceTimeMs = button::DEFAULT_DEBOUNCE_TIME_MS;
        uint32_t longClickTimeMs = button::DEFAULT_LONG_CLICK_TIME_MS;
        uint32_t multiClickTimeMs = button::DEFAULT_MULTI_CLICK_TIME_MS;
        bool enabled = true;
    };

    bool ReadOptionalTime(JsonObjectConst object, const char* name, uint32_t& result) {
        if (object[name].isNull()) return true;
        if (!object[name].is<uint32_t>()) return false;
        result = object[name].as<uint32_t>();
        return true;
    }

    bool ParseButtonConfiguration(JsonObjectConst object, int16_t id, ButtonConfiguration& result) {
        if (!HasComponentSections(object)) return false;
        const JsonObjectConst setup = ComponentSetup(object);
        if (!setup["Name"].is<const char*>() ||
            !setup["Class"].is<const char*>() ||
            !setup["Bus"].is<const char*>() ||
            !setup["Address"].is<int>()) {
            return false;
        }

        result.name = setup["Name"].as<const char*>();
        result.name.trim();
        if (result.name.isEmpty()) return false;

        const int address = setup["Address"].as<int>();
        if (address < 0 || address > UINT8_MAX) return false;

        const String componentClass = setup["Class"].as<const char*>();
        const String bus = setup["Bus"].as<const char*>();
        if (!componentClass.equalsIgnoreCase("Button") || !bus.equalsIgnoreCase("Onboard")) return false;

        result.id = id;
        result.address = static_cast<uint8_t>(address);

        if (!setup["ActiveLevel"].isNull()) {
            if (!setup["ActiveLevel"].is<const char*>()) return false;
            const String activeLevel = setup["ActiveLevel"].as<const char*>();
            if (activeLevel.equalsIgnoreCase("High")) {
                result.activeLevel = button::ActiveLevels::High;
            } else if (activeLevel.equalsIgnoreCase("Low")) {
                result.activeLevel = button::ActiveLevels::Low;
            } else {
                return false;
            }
        }

        if (!setup["InputMode"].isNull()) {
            if (!setup["InputMode"].is<const char*>()) return false;
            const String inputMode = setup["InputMode"].as<const char*>();
            if (inputMode.equalsIgnoreCase("Floating")) {
                result.inputMode = button::InputModes::Floating;
            } else if (inputMode.equalsIgnoreCase("PullUp")) {
                result.inputMode = button::InputModes::PullUp;
            } else if (inputMode.equalsIgnoreCase("PullDown")) {
                result.inputMode = button::InputModes::PullDown;
            } else {
                return false;
            }
        }

        if (!ReadOptionalTime(setup, "DebounceTimeMs", result.debounceTimeMs) ||
            !ReadOptionalTime(setup, "LongClickTimeMs", result.longClickTimeMs) ||
            !ReadOptionalTime(setup, "MultiClickTimeMs", result.multiClickTimeMs)) {
            return false;
        }

        if (!object["Properties"].isNull()) {
            if (!object["Properties"].is<JsonObjectConst>()) return false;
            const JsonObjectConst properties = object["Properties"].as<JsonObjectConst>();

            if (!properties["Enabled"].isNull()) {
                if (!properties["Enabled"].is<bool>()) return false;
                result.enabled = properties["Enabled"].as<bool>();
            }
        }

        if (!object["Events"].isNull() && !object["Events"].is<JsonObjectConst>()) return false;
        return true;
    }

    struct ThermometerConfiguration {
        String name;
        int16_t id = 0;
        component::Buses bus = component::Buses::Onboard;
        uint8_t address = 0;
        thermometer::ThermometerTypes type = thermometer::ThermometerTypes::Ds18b20;
        uint32_t pollingIntervalMs = thermometer::DEFAULT_POLLING_INTERVAL_MS;
        bool enabled = true;
    };

    bool ParseThermometerConfiguration(JsonObjectConst object, int16_t id, ThermometerConfiguration& result) {
        if (!HasComponentSections(object)) return false;
        const JsonObjectConst setup = ComponentSetup(object);
        if (!setup["Name"].is<const char*>() || !setup["Class"].is<const char*>() ||
            !setup["Bus"].is<const char*>() ||
            !setup["Address"].is<int>()) return false;

        result.name = setup["Name"].as<const char*>();
        result.name.trim();
        if (result.name.isEmpty()) return false;

        const int address = setup["Address"].as<int>();
        if (address < 0 || address > UINT8_MAX) return false;
        result.id = id;
        result.address = static_cast<uint8_t>(address);

        const String componentClass = setup["Class"].as<const char*>();
        const String bus = setup["Bus"].as<const char*>();
        if (!componentClass.equalsIgnoreCase("Thermometer")) return false;
        if (bus.equalsIgnoreCase("Onboard")) result.bus = component::Buses::Onboard;
        else if (bus.equalsIgnoreCase("I2C")) result.bus = component::Buses::I2C;
        else return false;

        if (!setup["Type"].isNull()) {
            if (!setup["Type"].is<const char*>() ||
                !thermometer::ParseType(setup["Type"].as<const char*>(), result.type)) return false;
        }
        if (result.bus == component::Buses::I2C && result.type != thermometer::ThermometerTypes::Dht12) return false;
        if (result.bus == component::Buses::I2C && (result.address == 0 || result.address > 0x7f)) return false;

        if (!ReadOptionalTime(setup, "PollingIntervalMs", result.pollingIntervalMs) ||
            result.pollingIntervalMs < thermometer::MINIMUM_POLLING_INTERVAL_MS) return false;

        if (!object["Properties"].isNull()) {
            if (!object["Properties"].is<JsonObjectConst>()) return false;
            const JsonObjectConst properties = object["Properties"].as<JsonObjectConst>();
            if (!properties["Enabled"].isNull()) {
                if (!properties["Enabled"].is<bool>()) return false;
                result.enabled = properties["Enabled"].as<bool>();
            }
        }

        if (!object["Events"].isNull() && !object["Events"].is<JsonObjectConst>()) return false;
        return true;
    }

    struct BlindsConfiguration {
        String name;
        int16_t id = 0;
        int16_t relayUp = 0;
        int16_t relayDown = 0;
        int16_t buttonUp = 0;
        int16_t buttonDown = 0;
        uint8_t position = 0;
        uint32_t openStepTimeMs = 0;
        uint32_t closeStepTimeMs = 0;
        float openCorrectionFactor = blinds::DEFAULT_OPEN_CORRECTION_FACTOR;
        float closeCorrectionFactor = blinds::DEFAULT_CLOSE_CORRECTION_FACTOR;
        uint32_t endstopMarginMs = blinds::DEFAULT_ENDSTOP_MARGIN_MS;
        uint32_t reversalDelayMs = blinds::DEFAULT_REVERSAL_DELAY_MS;
        bool enabled = true;
    };

    bool ParseBlindsConfiguration(JsonObjectConst object, int16_t id, BlindsConfiguration& result, bool requireDirectionalTimes = true) {
        if (!HasComponentSections(object)) return false;
        const JsonObjectConst setup = ComponentSetup(object);
        if (!setup["Name"].is<const char*>() || !setup["Class"].is<const char*>() ||
            !setup["Bus"].is<const char*>() || !setup["RelayUp"].is<int>() ||
            !setup["RelayDown"].is<int>()) return false;

        result.name = setup["Name"].as<const char*>();
        result.name.trim();
        const int relayUp = setup["RelayUp"].as<int>();
        const int relayDown = setup["RelayDown"].as<int>();
        if (result.name.isEmpty() || relayUp < 1 || relayUp > INT16_MAX ||
            relayDown < 1 || relayDown > INT16_MAX) return false;
        result.id = id;
        result.relayUp = static_cast<int16_t>(relayUp);
        result.relayDown = static_cast<int16_t>(relayDown);

        const String componentClass = setup["Class"].as<const char*>();
        const String bus = setup["Bus"].as<const char*>();
        if (!componentClass.equalsIgnoreCase("Blinds") || !bus.equalsIgnoreCase("Group")) return false;

        if (!setup["ButtonUp"].isNull()) {
            if (!setup["ButtonUp"].is<int>()) return false;
            const int buttonUp = setup["ButtonUp"].as<int>();
            if (buttonUp < 1 || buttonUp > INT16_MAX) return false;
            result.buttonUp = static_cast<int16_t>(buttonUp);
        }
        if (!setup["ButtonDown"].isNull()) {
            if (!setup["ButtonDown"].is<int>()) return false;
            const int buttonDown = setup["ButtonDown"].as<int>();
            if (buttonDown < 1 || buttonDown > INT16_MAX) return false;
            result.buttonDown = static_cast<int16_t>(buttonDown);
        }

        const bool directionalTimesValid = setup["OpenStepTimeMs"].is<uint32_t>() &&
            setup["CloseStepTimeMs"].is<uint32_t>() &&
            setup["OpenStepTimeMs"].as<uint32_t>() > 0 &&
            setup["CloseStepTimeMs"].as<uint32_t>() > 0 &&
            setup["OpenStepTimeMs"].as<uint32_t>() <= UINT32_MAX / 100U &&
            setup["CloseStepTimeMs"].as<uint32_t>() <= UINT32_MAX / 100U;
        if (!directionalTimesValid && requireDirectionalTimes) return false;
        if (directionalTimesValid) {
            result.openStepTimeMs = setup["OpenStepTimeMs"].as<uint32_t>();
            result.closeStepTimeMs = setup["CloseStepTimeMs"].as<uint32_t>();
        }

        if (!setup["OpenCorrectionFactor"].isNull()) {
            if (!setup["OpenCorrectionFactor"].is<float>()) return false;
            result.openCorrectionFactor = setup["OpenCorrectionFactor"].as<float>();
        }
        if (!setup["CloseCorrectionFactor"].isNull()) {
            if (!setup["CloseCorrectionFactor"].is<float>()) return false;
            result.closeCorrectionFactor = setup["CloseCorrectionFactor"].as<float>();
        }
        if (!std::isfinite(result.openCorrectionFactor) || !std::isfinite(result.closeCorrectionFactor) ||
            result.openCorrectionFactor < 0.0f || result.openCorrectionFactor > blinds::MAX_CORRECTION_FACTOR ||
            result.closeCorrectionFactor < 0.0f || result.closeCorrectionFactor > blinds::MAX_CORRECTION_FACTOR) return false;

        if (!ReadOptionalTime(setup, "EndstopMarginMs", result.endstopMarginMs) ||
            !ReadOptionalTime(setup, "ReversalDelayMs", result.reversalDelayMs)) return false;

        if (!object["Properties"].isNull()) {
            if (!object["Properties"].is<JsonObjectConst>()) return false;
            const JsonObjectConst properties = object["Properties"].as<JsonObjectConst>();
            if (!properties["Enabled"].isNull()) {
                if (!properties["Enabled"].is<bool>()) return false;
                result.enabled = properties["Enabled"].as<bool>();
            }
            if (!properties["Position"].isNull()) {
                if (!properties["Position"].is<int>()) return false;
                const int position = properties["Position"].as<int>();
                if (position < 0 || position > 100) return false;
                result.position = static_cast<uint8_t>(position);
            }
        }

        if (!object["Events"].isNull() && !object["Events"].is<JsonObjectConst>()) return false;
        return true;
    }
}

bool settings::InstallComponents(const String& configfilename) noexcept {
    if (ComponentController.IsStarted() || ComponentController.Count() != 0) return false;

    const String path = configfilename.length() ? configfilename : String(Defaults.ConfigFileName);
    String content;
    if (FileSystem.Read(path.c_str(), content) != filesystem::Result::Ok || content.isEmpty()) return false;

    JsonDocument document;
    if (deserializeJson(document, content)) return false;
    if ((document["ComponentSchemaVersion"] | 0) != ComponentSchemaVersion) return false;

    const JsonObjectConst components = document["Components"].as<JsonObjectConst>();
    if (components.isNull() || components.size() > MaxConfiguredComponents) return false;

    struct ComponentIdentity {
        String name;
        int16_t id;
        component::Buses bus;
        uint8_t address;
        component::Classes type;
        bool hasAddress;
    };

    ComponentIdentity identities[MaxConfiguredComponents];
    int16_t owners[MaxConfiguredComponents];
    bool installable[MaxConfiguredComponents];
    size_t count = 0;

    for (JsonPairConst entry : components) {
        int16_t configuredID = 0;
        if (!ParseComponentID(entry.key().c_str(), configuredID) || !entry.value().is<JsonObjectConst>()) return false;
        const JsonObjectConst object = entry.value().as<JsonObjectConst>();
        if (!HasComponentSections(object)) return false;
        const JsonObjectConst setup = ComponentSetup(object);
        if (!setup["Class"].is<const char*>()) return false;

        const String componentClass = setup["Class"].as<const char*>();
        if (componentClass.equalsIgnoreCase("Relay")) {
            RelayConfiguration configuration;
            if (!ParseRelayConfiguration(object, configuredID, configuration)) return false;
            identities[count] = {configuration.name, configuration.id, component::Buses::Onboard, configuration.address, component::Classes::Relay, true};
        } else if (componentClass.equalsIgnoreCase("Button")) {
            ButtonConfiguration configuration;
            if (!ParseButtonConfiguration(object, configuredID, configuration)) return false;
            identities[count] = {configuration.name, configuration.id, component::Buses::Onboard, configuration.address, component::Classes::Button, true};
        } else if (componentClass.equalsIgnoreCase("Thermometer")) {
            ThermometerConfiguration configuration;
            if (!ParseThermometerConfiguration(object, configuredID, configuration)) return false;
            identities[count] = {configuration.name, configuration.id, configuration.bus, configuration.address, component::Classes::Thermometer, true};
        } else if (componentClass.equalsIgnoreCase("Blinds")) {
            BlindsConfiguration configuration;
            if (!ParseBlindsConfiguration(object, configuredID, configuration, false)) return false;
            identities[count] = {configuration.name, configuration.id, component::Buses::Group, 0, component::Classes::Blinds, false};
            installable[count] = configuration.openStepTimeMs > 0 && configuration.closeStepTimeMs > 0;
            if (!installable[count]) {
                Logger.Log(
                    "Blinds '" + configuration.name + "' not installed: OpenStepTimeMs and CloseStepTimeMs are required",
                    logger::LogLevels::Warning
                );
            }
        } else {
            return false;
        }

        for (size_t previous = 0; previous < count; ++previous) {
            if (identities[previous].name.equalsIgnoreCase(identities[count].name) ||
                (identities[previous].hasAddress && identities[count].hasAddress &&
                    identities[previous].bus == identities[count].bus &&
                    identities[previous].address == identities[count].address)) {
                return false;
            }
        }

        owners[count] = -1;
        if (!componentClass.equalsIgnoreCase("Blinds")) installable[count] = true;
        ++count;
    }

    auto resolveMember = [&](int16_t selector, component::Classes expected) -> int16_t {
        for (size_t candidate = 0; candidate < count; ++candidate) {
            if (identities[candidate].type != expected) continue;
            if (identities[candidate].id == selector) return static_cast<int16_t>(candidate);
        }
        return -1;
    };

    size_t blindsIndex = 0;
    for (JsonPairConst entry : components) {
        int16_t configuredID = 0;
        if (!ParseComponentID(entry.key().c_str(), configuredID)) return false;
        const JsonObjectConst object = entry.value().as<JsonObjectConst>();
        const String componentClass = ComponentSetup(object)["Class"].as<const char*>();
        if (!componentClass.equalsIgnoreCase("Blinds")) {
            ++blindsIndex;
            continue;
        }

        BlindsConfiguration configuration;
        if (!ParseBlindsConfiguration(object, configuredID, configuration, false)) return false;
        const int16_t relayUp = resolveMember(configuration.relayUp, component::Classes::Relay);
        const int16_t relayDown = resolveMember(configuration.relayDown, component::Classes::Relay);
        const int16_t buttonUp = configuration.buttonUp == 0 ? -1 : resolveMember(configuration.buttonUp, component::Classes::Button);
        const int16_t buttonDown = configuration.buttonDown == 0 ? -1 : resolveMember(configuration.buttonDown, component::Classes::Button);
        if (relayUp < 0 || relayDown < 0 || relayUp == relayDown ||
            (configuration.buttonUp != 0 && buttonUp < 0) ||
            (configuration.buttonDown != 0 && buttonDown < 0) ||
            (buttonUp >= 0 && buttonUp == buttonDown)) return false;

        const int16_t members[] = {relayUp, relayDown, buttonUp, buttonDown};
        for (int16_t member : members) {
            if (member < 0) continue;
            if (owners[member] >= 0) return false;
            owners[member] = static_cast<int16_t>(blindsIndex);
            if (!installable[blindsIndex]) installable[member] = false;
        }
        ++blindsIndex;
    }

    std::unique_ptr<component> instances[MaxConfiguredComponents];
    size_t index = 0;

    // Create physical components first so groups may reference them regardless
    // of their ordering in the configuration object.
    for (JsonPairConst entry : components) {
        int16_t configuredID = 0;
        if (!ParseComponentID(entry.key().c_str(), configuredID)) return false;
        const JsonObjectConst object = entry.value().as<JsonObjectConst>();
        const String componentClass = ComponentSetup(object)["Class"].as<const char*>();

        if (!installable[index]) {
            ++index;
            continue;
        }

        if (componentClass.equalsIgnoreCase("Relay")) {
            RelayConfiguration configuration;
            if (!ParseRelayConfiguration(object, configuredID, configuration)) return false;
            instances[index].reset(new (std::nothrow) relay(
                configuration.name,
                configuration.id,
                component::Buses::Onboard,
                configuration.address,
                configuration.type,
                configuration.driveMode,
                owners[index] < 0 ? configuration.state : false,
                owners[index] < 0 ? configuration.enabled : true
            ));
        } else if (componentClass.equalsIgnoreCase("Button")) {
            ButtonConfiguration configuration;
            if (!ParseButtonConfiguration(object, configuredID, configuration)) return false;
            instances[index].reset(new (std::nothrow) button(
                configuration.name,
                configuration.id,
                component::Buses::Onboard,
                configuration.address,
                configuration.activeLevel,
                configuration.inputMode,
                configuration.debounceTimeMs,
                configuration.longClickTimeMs,
                configuration.multiClickTimeMs,
                owners[index] < 0 ? configuration.enabled : true
            ));
        } else if (componentClass.equalsIgnoreCase("Thermometer")) {
            ThermometerConfiguration configuration;
            if (!ParseThermometerConfiguration(object, configuredID, configuration)) return false;
            instances[index].reset(new (std::nothrow) thermometer(
                configuration.name,
                configuration.id,
                configuration.bus,
                configuration.address,
                configuration.type,
                configuration.pollingIntervalMs,
                configuration.enabled
            ));
        }

        if (!componentClass.equalsIgnoreCase("Blinds") && !instances[index]) return false;
        ++index;
    }

    index = 0;
    for (JsonPairConst entry : components) {
        int16_t configuredID = 0;
        if (!ParseComponentID(entry.key().c_str(), configuredID)) return false;
        const JsonObjectConst object = entry.value().as<JsonObjectConst>();
        const String componentClass = ComponentSetup(object)["Class"].as<const char*>();
        if (!installable[index]) {
            ++index;
            continue;
        }
        if (!componentClass.equalsIgnoreCase("Blinds")) {
            ++index;
            continue;
        }

        BlindsConfiguration configuration;
        if (!ParseBlindsConfiguration(object, configuredID, configuration)) return false;
        const int16_t relayUpIndex = resolveMember(configuration.relayUp, component::Classes::Relay);
        const int16_t relayDownIndex = resolveMember(configuration.relayDown, component::Classes::Relay);
        const int16_t buttonUpIndex = configuration.buttonUp == 0 ? -1 : resolveMember(configuration.buttonUp, component::Classes::Button);
        const int16_t buttonDownIndex = configuration.buttonDown == 0 ? -1 : resolveMember(configuration.buttonDown, component::Classes::Button);
        instances[index].reset(new (std::nothrow) blinds(
            configuration.name,
            configuration.id,
            static_cast<relay&>(*instances[relayUpIndex]),
            static_cast<relay&>(*instances[relayDownIndex]),
            buttonUpIndex < 0 ? nullptr : static_cast<button*>(instances[buttonUpIndex].get()),
            buttonDownIndex < 0 ? nullptr : static_cast<button*>(instances[buttonDownIndex].get()),
            configuration.position,
            configuration.openStepTimeMs,
            configuration.closeStepTimeMs,
            configuration.openCorrectionFactor,
            configuration.closeCorrectionFactor,
            configuration.endstopMarginMs,
            configuration.reversalDelayMs,
            configuration.enabled
        ));
        if (!instances[index]) return false;
        ++index;
    }

    component* runtimeByConfiguration[MaxConfiguredComponents]{};
    for (index = 0; index < count; ++index) {
        if (!installable[index]) continue;
        if (!ComponentController.Register(std::move(instances[index]))) return false;
        runtimeByConfiguration[index] = ComponentController.FindByID(identities[index].id);
        if (runtimeByConfiguration[index] == nullptr) return false;
    }

    for (index = 0; index < count; ++index) {
        if (owners[index] < 0) continue;
        if (!installable[index]) continue;
        component* member = runtimeByConfiguration[index];
        component* owner = runtimeByConfiguration[static_cast<size_t>(owners[index])];
        if (member == nullptr || owner == nullptr || !ComponentController.AssignOwner(*member, *owner)) return false;
        Logger.Log(
            "Component " + member->Name() + ": private member of Blinds '" + owner->Name() +
                "'; standalone automation and MQTT disabled",
            logger::LogLevels::Information
        );
    }

    Automation.Clear();
    index = 0;
    for (JsonPairConst entry : components) {
        component* instance = runtimeByConfiguration[index++];
        if (instance == nullptr) continue;
        const JsonObjectConst events = entry.value()["Events"].as<JsonObjectConst>();
        if (events.isNull()) continue;

        if (!instance->IsPublic()) {
            for (JsonPairConst configuredEvent : events) {
                Logger.Log(
                    "Component " + instance->Name() + ": event '" + String(configuredEvent.key().c_str()) +
                        "' ignored because it is owned by Blinds '" + instance->Owner()->Name() + "'",
                    logger::LogLevels::Warning
                );
            }
            continue;
        }

        for (JsonPairConst configuredEvent : events) {
            const String eventName = configuredEvent.key().c_str();
            if (!configuredEvent.value().is<const char*>()) {
                Logger.Log(
                    "Component " + instance->Name() + ": event '" + eventName + "' action must be a string",
                    logger::LogLevels::Warning
                );
                continue;
            }

            String error;
            if (!Automation.Register(*instance, eventName, configuredEvent.value().as<const char*>(), error)) {
                Logger.Log(
                    "Component " + instance->Name() + ": invalid event '" + eventName + "' (" + error + ")",
                    logger::LogLevels::Warning
                );
            }
        }
    }

    return true;
}

bool settings::SaveComponentsState(const String& configfilename) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return false;

    // Clear before taking the snapshot. Changes occurring while Save() runs
    // set the flags again and will be persisted by the next cycle.
    ComponentController.ClearPropertyChanged();
    ComponentController.ClearStateChanged();

    if (!Save(configfilename)) {
        pSaveComponentsStateFlag = true;
        return false;
    }

    pSaveComponentsStateFlag = false;
    return true;
}
namespace {
    bool ParseInteger(const String& text, long minimum, long maximum, long& result) {
        if (text.isEmpty()) return false;
        char* end = nullptr;
        const long parsed = std::strtol(text.c_str(), &end, 10);
        if (end == text.c_str() || *end != '\0' || parsed < minimum || parsed > maximum) return false;
        result = parsed;
        return true;
    }

    bool ParseConfigBoolean(const String& text, bool& result) {
        if (text.equalsIgnoreCase("true") || text.equalsIgnoreCase("on") || text == "1") {
            result = true;
            return true;
        }
        if (text.equalsIgnoreCase("false") || text.equalsIgnoreCase("off") || text == "0") {
            result = false;
            return true;
        }
        return false;
    }

    bool SplitAssignment(const String& assignment, String& name, String& value) {
        const int separator = assignment.indexOf('=');
        if (separator <= 0 || separator == static_cast<int>(assignment.length()) - 1 ||
            assignment.indexOf('=', separator + 1) >= 0) return false;
        name = assignment.substring(0, separator);
        value = assignment.substring(separator + 1);
        name.trim();
        value.trim();
        return !name.isEmpty() && !value.isEmpty();
    }

    bool SelectorID(const String& selector, int16_t& id) {
        if (!selector.startsWith("#")) return false;
        long parsed = 0;
        if (!ParseInteger(selector.substring(1), 1, INT16_MAX, parsed)) return false;
        id = static_cast<int16_t>(parsed);
        return true;
    }

    JsonObject FindConfiguredComponent(JsonObject components, const String& selector, int16_t& configuredID) {
        int16_t id = 0;
        const bool byID = SelectorID(selector, id);
        if (byID) {
            JsonObject item = components[String(id)].as<JsonObject>();
            if (!item.isNull()) configuredID = id;
            return item;
        }
        for (JsonPair entry : components) {
            JsonObject item = entry.value().as<JsonObject>();
            const JsonObject setup = ComponentSetup(item);
            const String name = setup["Name"] | "";
            if (name.equalsIgnoreCase(selector)) {
                if (!ParseComponentID(entry.key().c_str(), configuredID)) return JsonObject();
                return item;
            }
        }
        return JsonObject();
    }

    component* FindRuntimeComponent(const String& selector) {
        int16_t id = 0;
        if (SelectorID(selector, id)) return ComponentController.FindByID(id);

        for (size_t index = 0; index < ComponentController.Count(); ++index) {
            component* item = ComponentController.At(index);
            if (item != nullptr && item->Name().equalsIgnoreCase(selector)) return item;
        }
        return nullptr;
    }

    JsonObject ResolveConfiguredComponent(JsonObject components, const String& selector, int16_t& configuredID) {
        JsonObject configured = FindConfiguredComponent(components, selector, configuredID);
        if (!configured.isNull()) return configured;

        component* runtime = FindRuntimeComponent(selector);
        if (runtime == nullptr) return JsonObject();
        return FindConfiguredComponent(components, "#" + String(runtime->ID()), configuredID);
    }

    bool ValidateCatalog(JsonObjectConst components) {
        if (components.isNull() || components.size() > MaxConfiguredComponents) return false;

        struct Identity {
            String name;
            int16_t id;
            component::Buses bus;
            uint8_t address;
            component::Classes type;
            bool hasAddress;
        };

        Identity identities[MaxConfiguredComponents];
        int16_t owners[MaxConfiguredComponents];
        size_t count = 0;

        for (JsonPairConst entry : components) {
            int16_t configuredID = 0;
            if (!ParseComponentID(entry.key().c_str(), configuredID) || !entry.value().is<JsonObjectConst>()) return false;
            const JsonObjectConst object = entry.value().as<JsonObjectConst>();
            if (!HasComponentSections(object)) return false;
            const JsonObjectConst setup = ComponentSetup(object);
            if (!setup["Class"].is<const char*>()) return false;
            const String componentClass = setup["Class"].as<const char*>();

            if (componentClass.equalsIgnoreCase("Relay")) {
                RelayConfiguration configuration;
                if (!ParseRelayConfiguration(object, configuredID, configuration)) return false;
                identities[count] = {configuration.name, configuration.id, component::Buses::Onboard, configuration.address, component::Classes::Relay, true};
            } else if (componentClass.equalsIgnoreCase("Button")) {
                ButtonConfiguration configuration;
                if (!ParseButtonConfiguration(object, configuredID, configuration)) return false;
                identities[count] = {configuration.name, configuration.id, component::Buses::Onboard, configuration.address, component::Classes::Button, true};
            } else if (componentClass.equalsIgnoreCase("Thermometer")) {
                ThermometerConfiguration configuration;
                if (!ParseThermometerConfiguration(object, configuredID, configuration)) return false;
                identities[count] = {configuration.name, configuration.id, configuration.bus, configuration.address, component::Classes::Thermometer, true};
            } else if (componentClass.equalsIgnoreCase("Blinds")) {
                BlindsConfiguration configuration;
                if (!ParseBlindsConfiguration(object, configuredID, configuration)) return false;
                identities[count] = {configuration.name, configuration.id, component::Buses::Group, 0, component::Classes::Blinds, false};
            } else {
                return false;
            }

            for (size_t previous = 0; previous < count; ++previous) {
                if (identities[previous].name.equalsIgnoreCase(identities[count].name) ||
                    (identities[previous].hasAddress && identities[count].hasAddress &&
                        identities[previous].bus == identities[count].bus &&
                        identities[previous].address == identities[count].address)) return false;
            }
            owners[count] = -1;
            ++count;
        }

        auto resolveMember = [&](int16_t selector, component::Classes expected) -> int16_t {
            for (size_t candidate = 0; candidate < count; ++candidate) {
                if (identities[candidate].type != expected) continue;
                if (identities[candidate].id == selector) return static_cast<int16_t>(candidate);
            }
            return -1;
        };

        size_t ownerIndex = 0;
        for (JsonPairConst entry : components) {
            int16_t configuredID = 0;
            if (!ParseComponentID(entry.key().c_str(), configuredID)) return false;
            const JsonObjectConst object = entry.value().as<JsonObjectConst>();
            const String componentClass = ComponentSetup(object)["Class"].as<const char*>();
            if (!componentClass.equalsIgnoreCase("Blinds")) {
                ++ownerIndex;
                continue;
            }
            BlindsConfiguration configuration;
            if (!ParseBlindsConfiguration(object, configuredID, configuration)) return false;
            const int16_t relayUp = resolveMember(configuration.relayUp, component::Classes::Relay);
            const int16_t relayDown = resolveMember(configuration.relayDown, component::Classes::Relay);
            const int16_t buttonUp = configuration.buttonUp == 0 ? -1 : resolveMember(configuration.buttonUp, component::Classes::Button);
            const int16_t buttonDown = configuration.buttonDown == 0 ? -1 : resolveMember(configuration.buttonDown, component::Classes::Button);
            if (relayUp < 0 || relayDown < 0 || relayUp == relayDown ||
                (configuration.buttonUp != 0 && buttonUp < 0) ||
                (configuration.buttonDown != 0 && buttonDown < 0) ||
                (buttonUp >= 0 && buttonUp == buttonDown)) return false;
            const int16_t members[] = {relayUp, relayDown, buttonUp, buttonDown};
            for (int16_t member : members) {
                if (member < 0) continue;
                if (owners[member] >= 0) return false;
                owners[member] = static_cast<int16_t>(ownerIndex);
            }
            ++ownerIndex;
        }
        return true;
    }

    bool ConfigurationMatchesRuntime(JsonObjectConst configured, int16_t configuredID, const component& runtime) {
        const JsonObjectConst setup = ComponentSetup(configured);
        if (configuredID != runtime.ID()) return false;

        const String configuredName = setup["Name"] | "";
        const JsonObjectConst properties = configured["Properties"].as<JsonObjectConst>();
        if (configuredName != runtime.Name() || properties.isNull()) return false;

        if (runtime.Class() != component::Classes::Blinds &&
            (setup["Address"] | -1) != runtime.Address()) return false;
        if (runtime.IsPublic() && (properties["Enabled"] | true) != runtime.Enabled()) return false;

        if (runtime.Class() == component::Classes::Relay) {
            RelayConfiguration configuration;
            if (!ParseRelayConfiguration(configured, configuredID, configuration)) return false;
            const relay& value = static_cast<const relay&>(runtime);
            return configuration.type == value.Type() && configuration.driveMode == value.DriveMode();
        }

        if (runtime.Class() == component::Classes::Button) {
            ButtonConfiguration configuration;
            if (!ParseButtonConfiguration(configured, configuredID, configuration)) return false;
            const button& value = static_cast<const button&>(runtime);
            return configuration.activeLevel == value.ActiveLevel() &&
                   configuration.inputMode == value.InputMode() &&
                   configuration.debounceTimeMs == value.DebounceTime() &&
                   configuration.longClickTimeMs == value.LongClickTime() &&
                   configuration.multiClickTimeMs == value.MultiClickTime();
        }

        if (runtime.Class() == component::Classes::Thermometer) {
            ThermometerConfiguration configuration;
            if (!ParseThermometerConfiguration(configured, configuredID, configuration)) return false;
            const thermometer& value = static_cast<const thermometer&>(runtime);
            return configuration.bus == value.Bus() && configuration.type == value.Type() &&
                configuration.pollingIntervalMs == value.PollingInterval();
        }

        if (runtime.Class() == component::Classes::Blinds) {
            BlindsConfiguration configuration;
            if (!ParseBlindsConfiguration(configured, configuredID, configuration)) return false;
            const blinds& value = static_cast<const blinds&>(runtime);
            const bool buttonsMatch =
                (value.ButtonUp() == nullptr ? configuration.buttonUp == 0 : value.ButtonUp()->ID() == configuration.buttonUp) &&
                (value.ButtonDown() == nullptr ? configuration.buttonDown == 0 : value.ButtonDown()->ID() == configuration.buttonDown);
            return value.RelayUp().ID() == configuration.relayUp &&
                value.RelayDown().ID() == configuration.relayDown && buttonsMatch &&
                value.OpenStepTime() == configuration.openStepTimeMs &&
                value.CloseStepTime() == configuration.closeStepTimeMs &&
                value.OpenCorrectionFactor() == configuration.openCorrectionFactor &&
                value.CloseCorrectionFactor() == configuration.closeCorrectionFactor &&
                value.EndstopMargin() == configuration.endstopMarginMs &&
                value.ReversalDelay() == configuration.reversalDelayMs;
        }

        return false;
    }

    bool CatalogPending(JsonObjectConst components) {
        if (components.size() != ComponentController.Count()) return true;
        for (JsonPairConst entry : components) {
            int16_t id = 0;
            if (!ParseComponentID(entry.key().c_str(), id) || !entry.value().is<JsonObjectConst>()) return true;
            const JsonObjectConst configured = entry.value().as<JsonObjectConst>();
            component* runtime = ComponentController.FindByID(id);
            if (runtime == nullptr || !ConfigurationMatchesRuntime(configured, id, *runtime)) return true;
        }
        return false;
    }

    bool ApplyConfiguredProperty(JsonObject item, const String& property, const String& text, bool allowState) {
        JsonObject setup = ComponentSetup(item);
        if (setup.isNull()) return false;
        if (property.equalsIgnoreCase("enabled")) {
            bool value = false;
            if (!ParseConfigBoolean(text, value)) return false;
            item["Properties"].to<JsonObject>()["Enabled"] = value;
            return true;
        }

        if (property.equalsIgnoreCase("address")) {
            long value = 0;
            if (!ParseInteger(text, 0, UINT8_MAX, value)) return false;
            setup["Address"] = value;
            return true;
        }

        const String componentClass = setup["Class"] | "";
        if (componentClass.equalsIgnoreCase("Relay")) {
            if (allowState && property.equalsIgnoreCase("state")) {
                bool value = false;
                if (!ParseConfigBoolean(text, value)) return false;
                item["Properties"].to<JsonObject>()["State"] = value;
                return true;
            }
            if (property.equalsIgnoreCase("type")) {
                if (!text.equalsIgnoreCase("NormallyOpen") && !text.equalsIgnoreCase("NormallyClosed")) return false;
                setup["Type"] = text.equalsIgnoreCase("NormallyOpen") ? "NormallyOpen" : "NormallyClosed";
                return true;
            }
            if (property.equalsIgnoreCase("drivemode")) {
                if (!text.equalsIgnoreCase("ActiveHigh") && !text.equalsIgnoreCase("ActiveLow")) return false;
                setup["DriveMode"] = text.equalsIgnoreCase("ActiveHigh") ? "ActiveHigh" : "ActiveLow";
                return true;
            }
        }

        if (componentClass.equalsIgnoreCase("Button")) {
            if (property.equalsIgnoreCase("activelevel")) {
                if (!text.equalsIgnoreCase("High") && !text.equalsIgnoreCase("Low")) return false;
                setup["ActiveLevel"] = text.equalsIgnoreCase("High") ? "High" : "Low";
                return true;
            }
            if (property.equalsIgnoreCase("inputmode")) {
                if (!text.equalsIgnoreCase("Floating") && !text.equalsIgnoreCase("PullUp") && !text.equalsIgnoreCase("PullDown")) return false;
                setup["InputMode"] = text.equalsIgnoreCase("Floating") ? "Floating" :
                    text.equalsIgnoreCase("PullDown") ? "PullDown" : "PullUp";
                return true;
            }

            const bool isTime = property.equalsIgnoreCase("debouncetimems") ||
                property.equalsIgnoreCase("longclicktimems") ||
                property.equalsIgnoreCase("multiclicktimems");
            if (isTime) {
                long value = 0;
                if (!ParseInteger(text, 0, INT32_MAX, value)) return false;
                const char* key = property.equalsIgnoreCase("debouncetimems") ? "DebounceTimeMs" :
                    property.equalsIgnoreCase("longclicktimems") ? "LongClickTimeMs" : "MultiClickTimeMs";
                setup[key] = static_cast<uint32_t>(value);
                return true;
            }
        }

        if (componentClass.equalsIgnoreCase("Thermometer")) {
            if (property.equalsIgnoreCase("type")) {
                thermometer::ThermometerTypes type;
                if (!thermometer::ParseType(text, type)) return false;
                setup["Type"] = thermometer::TypeName(type);
                return true;
            }
            if (property.equalsIgnoreCase("pollingintervalms")) {
                long value = 0;
                if (!ParseInteger(text, thermometer::MINIMUM_POLLING_INTERVAL_MS, INT32_MAX, value)) return false;
                setup["PollingIntervalMs"] = static_cast<uint32_t>(value);
                return true;
            }
        }

        if (componentClass.equalsIgnoreCase("Blinds")) {
            if (property.equalsIgnoreCase("position")) {
                long value = 0;
                if (!ParseInteger(text, 0, 100, value)) return false;
                item["Properties"].to<JsonObject>()["Position"] = value;
                return true;
            }
            if (property.equalsIgnoreCase("opensteptimems") || property.equalsIgnoreCase("closesteptimems") ||
                property.equalsIgnoreCase("endstopmarginms") || property.equalsIgnoreCase("reversaldelayms")) {
                long value = 0;
                const bool stepTime = property.equalsIgnoreCase("opensteptimems") || property.equalsIgnoreCase("closesteptimems");
                if (!ParseInteger(text, stepTime ? 1 : 0, stepTime ? UINT32_MAX / 100U : INT32_MAX, value)) return false;
                const char* key = property.equalsIgnoreCase("opensteptimems") ? "OpenStepTimeMs" :
                    property.equalsIgnoreCase("closesteptimems") ? "CloseStepTimeMs" :
                    property.equalsIgnoreCase("endstopmarginms") ? "EndstopMarginMs" : "ReversalDelayMs";
                setup[key] = static_cast<uint32_t>(value);
                return true;
            }
            if (property.equalsIgnoreCase("opencorrectionfactor") || property.equalsIgnoreCase("closecorrectionfactor")) {
                char* end = nullptr;
                const float value = std::strtof(text.c_str(), &end);
                if (end == text.c_str() || *end != '\0' || !std::isfinite(value) ||
                    value < 0.0f || value > blinds::MAX_CORRECTION_FACTOR) return false;
                setup[property.equalsIgnoreCase("opencorrectionfactor") ? "OpenCorrectionFactor" : "CloseCorrectionFactor"] = value;
                return true;
            }
        }

        return false;
    }

    bool WriteConfigurationDocument(JsonDocument& document) {
        document["ComponentSchemaVersion"] = ComponentSchemaVersion;
        String serialized;
        if (serializeJsonPretty(document, serialized) == 0) return false;
        return FileSystem.Write(Defaults.ConfigFileName, serialized) == filesystem::Result::Ok;
    }

    const char* PropertyResultName(ComponentPropertyResult result) {
        switch (result) {
            case ComponentPropertyResult::Accepted: return "accepted";
            case ComponentPropertyResult::PropertyNotSupported: return "property not supported";
            case ComponentPropertyResult::InvalidValue: return "invalid value";
            case ComponentPropertyResult::ComponentDisabled: return "component disabled";
            case ComponentPropertyResult::CommandRejected: return "command rejected";
            default: return "unknown error";
        }
    }
}

bool settings::ExecuteComponentCommand(String* parameters, String& output) noexcept {
    if (parameters == nullptr) return false;

    String subcommand = parameters[0];
    subcommand.toLowerCase();
    if (subcommand.isEmpty()) {
        output = "Usage: comp list | tree | status [selector] | set selector property=value | trigger selector event [value=integer] | rename selector name=value | remove selector | add class key=value...\r\n";
        return false;
    }

    Lock lock(pMutex);
    if (!lock.IsLocked()) {
        output = "Component configuration is busy.\r\n";
        return false;
    }

    String content;
    if (FileSystem.Read(Defaults.ConfigFileName, content) != filesystem::Result::Ok || content.isEmpty()) {
        output = "Error reading configuration file.\r\n";
        return false;
    }

    JsonDocument document;
    if (deserializeJson(document, content)) {
        output = "Invalid configuration file.\r\n";
        return false;
    }
    if ((document["ComponentSchemaVersion"] | 0) != ComponentSchemaVersion) {
        output = "Unsupported component schema. ComponentSchemaVersion must be 1.\r\n";
        return false;
    }

    JsonObject components = document["Components"].as<JsonObject>();
    if (components.isNull()) {
        output = "Components configuration is missing.\r\n";
        return false;
    }

    if (subcommand == "tree") {
        if (!parameters[1].isEmpty()) {
            output = "Usage: comp tree\r\n";
            return false;
        }

        struct BusView {
            component::Buses value;
            const char* name;
        };
        const BusView buses[] = {
            {component::Buses::Onboard, "Onboard"},
            {component::Buses::I2C, "I2C"},
            {component::Buses::Group, "Group"}
        };

        const auto configuredStatus = [&](JsonObjectConst item, int16_t id) -> const char* {
            component* runtime = ComponentController.FindByID(id);
            if (runtime == nullptr) return "restart required";
            return ConfigurationMatchesRuntime(item, id, *runtime) ? "running" : "restart required";
        };

        const auto appendBlindsMember = [&](const char* role, JsonVariantConst reference) {
            if (!reference.is<int>()) {
                output += "|       |-- " + String(role) + " -> [not configured]\r\n";
                return;
            }
            const int memberID = reference.as<int>();
            JsonObjectConst member = components[String(memberID)].as<JsonObjectConst>();
            if (member.isNull()) {
                output += "|       |-- " + String(role) + " -> #" + String(memberID) + " [missing]\r\n";
                return;
            }
            const JsonObjectConst memberSetup = ComponentSetup(member);
            output += "|       |-- " + String(role) + " -> #" + String(memberID) + " " +
                String(memberSetup["Class"] | "") + " " + String(memberSetup["Name"] | "") + "\r\n";
        };

        output = "Components\r\n";
        bool anyBus = false;
        for (const BusView& bus : buses) {
            bool hasComponents = false;
            for (JsonPair entry : components) {
                const JsonObjectConst setup = entry.value()["Setup"].as<JsonObjectConst>();
                const String configuredBus = setup["Bus"] | "";
                if (configuredBus.equalsIgnoreCase(bus.name)) {
                    hasComponents = true;
                    break;
                }
            }
            if (!hasComponents) {
                for (size_t index = 0; index < ComponentController.Count(); ++index) {
                    component* runtime = ComponentController.At(index);
                    if (runtime != nullptr && runtime->Bus() == bus.value && components[String(runtime->ID())].isNull()) {
                        hasComponents = true;
                        break;
                    }
                }
            }
            if (!hasComponents) continue;

            anyBus = true;
            output += "|-- " + String(bus.name) + "\r\n";
            for (JsonPair entry : components) {
                int16_t id = 0;
                if (!ParseComponentID(entry.key().c_str(), id) || !entry.value().is<JsonObjectConst>()) {
                    output += "|   |-- [invalid component key or definition]\r\n";
                    continue;
                }
                const JsonObjectConst item = entry.value().as<JsonObjectConst>();
                const JsonObjectConst setup = ComponentSetup(item);
                const String configuredBus = setup["Bus"] | "";
                if (!configuredBus.equalsIgnoreCase(bus.name)) continue;

                output += "|   |-- #" + String(id) + " " + String(setup["Class"] | "") + " " +
                    String(setup["Name"] | "") + " [" + configuredStatus(item, id) + "]\r\n";
                const String componentClass = setup["Class"] | "";
                if (componentClass.equalsIgnoreCase("Blinds")) {
                    appendBlindsMember("RelayUp", setup["RelayUp"]);
                    appendBlindsMember("RelayDown", setup["RelayDown"]);
                    if (!setup["ButtonUp"].isNull()) appendBlindsMember("ButtonUp", setup["ButtonUp"]);
                    if (!setup["ButtonDown"].isNull()) appendBlindsMember("ButtonDown", setup["ButtonDown"]);
                }
            }

            for (size_t index = 0; index < ComponentController.Count(); ++index) {
                component* runtime = ComponentController.At(index);
                if (runtime == nullptr || runtime->Bus() != bus.value || !components[String(runtime->ID())].isNull()) continue;
                output += "|   |-- #" + String(runtime->ID()) + " " + String(component::ClassName(runtime->Class())) +
                    " " + runtime->Name() + " [pending removal]\r\n";
            }
        }
        if (!anyBus) output += "`-- [empty]\r\n";
        return true;
    }

    if (subcommand == "list") {
        if (!parameters[1].isEmpty()) {
            output = "Usage: comp list\r\n";
            return false;
        }

        if (components.size() == 0) {
            output = "No components configured.\r\n";
            return true;
        }

        for (JsonPair entry : components) {
            int16_t id = 0;
            if (!ParseComponentID(entry.key().c_str(), id)) {
                output = "Invalid component ID key.\r\n";
                return false;
            }
            JsonObject item = entry.value().as<JsonObject>();
            const JsonObject setup = ComponentSetup(item);
            const String name = setup["Name"] | "";
            const String componentClass = setup["Class"] | "";
            component* runtime = ComponentController.FindByID(id);
            output += "#" + String(id) + " " + componentClass + " " + name;
            output += runtime == nullptr ? " [restart required]\r\n" :
                ConfigurationMatchesRuntime(item, id, *runtime) ? " [running]\r\n" : " [restart required]\r\n";
        }

        for (size_t index = 0; index < ComponentController.Count(); ++index) {
            component* runtime = ComponentController.At(index);
            const bool found = !components[String(runtime->ID())].isNull();
            if (!found) output += "#" + String(runtime->ID()) + " " + String(component::ClassName(runtime->Class())) +
                " " + runtime->Name() + " [pending removal]\r\n";
        }
        return true;
    }

    if (subcommand == "status") {
        if (!parameters[2].isEmpty()) {
            output = "Usage: comp status [component_name|#component_id]\r\n";
            return false;
        }

        if (parameters[1].isEmpty()) {
            output += "Runtime components      | " + String(ComponentController.Count()) + "\r\n";
            output += "Configured components   | " + String(components.size()) + "\r\n";
            output += "Configuration pending   | " + String(CatalogPending(components) ? "yes" : "no") + "\r\n";
            output += "Restart required        | " + String(CatalogPending(components) ? "yes" : "no") + "\r\n";
            return true;
        }

        int16_t configuredID = 0;
        JsonObject configured = ResolveConfiguredComponent(components, parameters[1], configuredID);
        component* runtime = configured.isNull() ? FindRuntimeComponent(parameters[1]) :
            ComponentController.FindByID(configuredID);

        if (configured.isNull() && runtime == nullptr) {
            output = "Component '" + parameters[1] + "' not found.\r\n";
            return false;
        }

        if (runtime != nullptr) runtime->GetInfo(output);
        else output += "Runtime        | Not loaded\r\n";

        if (!configured.isNull()) {
            const JsonObject setup = ComponentSetup(configured);
            output += "ConfiguredName | " + String(setup["Name"] | "") + "\r\n";
            output += "ConfiguredID   | " + String(configuredID) + "\r\n";
        } else {
            output += "Configuration  | Pending removal\r\n";
        }

        const bool pending = configured.isNull() || runtime == nullptr ||
            !ConfigurationMatchesRuntime(configured, configuredID, *runtime);
        output += "RestartRequired| " + String(pending ? "yes" : "no") + "\r\n";
        return true;
    }

    if (subcommand == "set") {
        if (parameters[1].isEmpty() || parameters[2].isEmpty() || !parameters[3].isEmpty()) {
            output = "Usage: comp set selector property=value\r\n";
            return false;
        }

        String property;
        String value;
        if (!SplitAssignment(parameters[2], property, value)) {
            output = "Invalid assignment. Expected property=value.\r\n";
            return false;
        }

        int16_t configuredID = 0;
        JsonObject configured = ResolveConfiguredComponent(components, parameters[1], configuredID);
        component* runtime = FindRuntimeComponent(parameters[1]);
        if (runtime == nullptr && !configured.isNull()) {
            runtime = ComponentController.FindByID(configuredID);
        }

        if (property.equalsIgnoreCase("state")) {
            if (runtime == nullptr) {
                output = "Component is not running; state cannot be changed.\r\n";
                return false;
            }
            if (!runtime->IsPublic()) {
                output = "Component is a private Blinds member; control it through '" + runtime->Owner()->Name() + "'.\r\n";
                return false;
            }
            const ComponentPropertyResult result = runtime->SetProperty("state", value, pdMS_TO_TICKS(100));
            output = "Set " + runtime->Name() + ".state=" + value + ": " + PropertyResultName(result) + ".\r\n";
            return result == ComponentPropertyResult::Accepted;
        }

        if (configured.isNull()) {
            output = "Configured component '" + parameters[1] + "' not found.\r\n";
            return false;
        }

        if (!ApplyConfiguredProperty(configured, property, value, false) || !ValidateCatalog(components)) {
            output = "Unsupported property or invalid value.\r\n";
            return false;
        }

        if (!WriteConfigurationDocument(document)) {
            output = "Error saving configuration.\r\n";
            return false;
        }

        output = "Configuration saved. Restart required. Use 'reboot' to apply changes.\r\n";
        return true;
    }

    if (subcommand == "trigger") {
        if (parameters[1].isEmpty() || parameters[2].isEmpty() || !parameters[4].isEmpty()) {
            output = "Usage: comp trigger selector event [value=integer]\r\n";
            return false;
        }

        int32_t eventValue = 0;
        if (!parameters[3].isEmpty()) {
            String property;
            String value;
            long parsed = 0;
            if (!SplitAssignment(parameters[3], property, value) || !property.equalsIgnoreCase("value") ||
                !ParseInteger(value, INT32_MIN, INT32_MAX, parsed)) {
                output = "Invalid event value. Expected value=integer.\r\n";
                return false;
            }
            eventValue = static_cast<int32_t>(parsed);
        }

        int16_t configuredID = 0;
        JsonObject configured = ResolveConfiguredComponent(components, parameters[1], configuredID);
        component* runtime = FindRuntimeComponent(parameters[1]);
        if (runtime == nullptr && !configured.isNull()) {
            runtime = ComponentController.FindByID(configuredID);
        }
        if (runtime == nullptr) {
            output = "Component '" + parameters[1] + "' is not running.\r\n";
            return false;
        }

        uint16_t eventCode = 0;
        if (!runtime->ResolveEvent(parameters[2], eventCode)) {
            output = "Component '" + runtime->Name() + "' does not expose event '" + parameters[2] + "'.\r\n";
            return false;
        }
        if (!runtime->Enabled()) {
            output = "Component '" + runtime->Name() + "' is disabled.\r\n";
            return false;
        }
        if (!runtime->Initialized()) {
            output = "Component '" + runtime->Name() + "' is not initialized.\r\n";
            return false;
        }

        if (!runtime->TriggerEvent(parameters[2], eventValue, pdMS_TO_TICKS(100))) {
            output = "Event queue rejected " + runtime->Name() + "." + parameters[2] + ".\r\n";
            return false;
        }

        output = "Triggered " + runtime->Name() + "." + parameters[2] + " value=" + String(eventValue) + ".\r\n";
        return true;
    }

    if (subcommand == "rename") {
        if (parameters[1].isEmpty() || parameters[2].isEmpty() || !parameters[3].isEmpty()) {
            output = "Usage: comp rename selector name=newname\r\n";
            return false;
        }

        String property;
        String value;
        if (!SplitAssignment(parameters[2], property, value) || !property.equalsIgnoreCase("name")) {
            output = "Invalid assignment. Expected name=newname.\r\n";
            return false;
        }

        int16_t configuredID = 0;
        JsonObject configured = ResolveConfiguredComponent(components, parameters[1], configuredID);
        if (configured.isNull()) {
            output = "Configured component '" + parameters[1] + "' not found.\r\n";
            return false;
        }

        ComponentSetup(configured)["Name"] = value;
        if (!ValidateCatalog(components) || !WriteConfigurationDocument(document)) {
            output = "Invalid or duplicate name; configuration was not saved.\r\n";
            return false;
        }

        output = "Configuration saved. Restart required. Use 'reboot' to apply changes.\r\n";
        return true;
    }

    if (subcommand == "remove") {
        if (parameters[1].isEmpty() || !parameters[2].isEmpty()) {
            output = "Usage: comp remove selector\r\n";
            return false;
        }

        int16_t configuredID = 0;
        JsonObject configured = ResolveConfiguredComponent(components, parameters[1], configuredID);
        if (configured.isNull()) {
            output = "Configured component '" + parameters[1] + "' not found.\r\n";
            return false;
        }

        components.remove(String(configuredID));

        if (!ValidateCatalog(components)) {
            output = "Component is still required by a Blinds group and was not removed.\r\n";
            return false;
        }

        if (!WriteConfigurationDocument(document)) {
            output = "Error saving configuration.\r\n";
            return false;
        }

        output = "Component removed from configuration. Restart required. Use 'reboot' to apply changes.\r\n";
        return true;
    }

    if (subcommand == "add") {
        if (parameters[1].isEmpty()) {
            output = "Usage: comp add relay|button|thermometer|blinds name=value [id=value] ...\r\n";
            return false;
        }
        if (components.size() >= MaxConfiguredComponents) {
            output = "Maximum component count reached.\r\n";
            return false;
        }

        String componentClass = parameters[1];
        if (!componentClass.equalsIgnoreCase("Relay") && !componentClass.equalsIgnoreCase("Button") &&
            !componentClass.equalsIgnoreCase("Thermometer") && !componentClass.equalsIgnoreCase("Blinds")) {
            output = "Unsupported component class. Expected relay, button, thermometer, or blinds.\r\n";
            return false;
        }

        int16_t createdID = 0;
        for (size_t index = 2; index < ::telnetserver::MAX_COMMAND_PARAMETERS && !parameters[index].isEmpty(); ++index) {
            String property;
            String value;
            if (!SplitAssignment(parameters[index], property, value)) continue;
            if (!property.equalsIgnoreCase("id")) continue;
            long id = 0;
            if (!ParseInteger(value, 1, INT16_MAX, id)) {
                output = "Invalid component ID.\r\n";
                return false;
            }
            createdID = static_cast<int16_t>(id);
            break;
        }
        if (createdID == 0) {
            for (int32_t candidate = 1; candidate <= INT16_MAX; ++candidate) {
                if (components[String(candidate)].isNull()) {
                    createdID = static_cast<int16_t>(candidate);
                    break;
                }
            }
        }
        if (createdID == 0 || !components[String(createdID)].isNull()) {
            output = "Component ID already exists or no ID is available.\r\n";
            return false;
        }

        JsonObject item = components[String(createdID)].to<JsonObject>();
        JsonObject setup = item["Setup"].to<JsonObject>();
        setup["Class"] = componentClass.equalsIgnoreCase("Relay") ? "Relay" :
            componentClass.equalsIgnoreCase("Button") ? "Button" :
            componentClass.equalsIgnoreCase("Thermometer") ? "Thermometer" : "Blinds";
        setup["Bus"] = componentClass.equalsIgnoreCase("Blinds") ? "Group" : "Onboard";
        if (!componentClass.equalsIgnoreCase("Blinds")) setup["Address"] = -1;
        JsonObject properties = item["Properties"].to<JsonObject>();
        properties["Enabled"] = true;
        item["Events"].to<JsonObject>();

        if (componentClass.equalsIgnoreCase("Relay")) {
            setup["Type"] = "NormallyOpen";
            setup["DriveMode"] = "ActiveHigh";
            properties["State"] = false;
        } else if (componentClass.equalsIgnoreCase("Button")) {
            setup["ActiveLevel"] = "Low";
            setup["InputMode"] = "PullUp";
            setup["DebounceTimeMs"] = button::DEFAULT_DEBOUNCE_TIME_MS;
            setup["LongClickTimeMs"] = button::DEFAULT_LONG_CLICK_TIME_MS;
            setup["MultiClickTimeMs"] = button::DEFAULT_MULTI_CLICK_TIME_MS;
        } else if (componentClass.equalsIgnoreCase("Thermometer")) {
            setup["Type"] = "DS18B20";
            setup["PollingIntervalMs"] = thermometer::DEFAULT_POLLING_INTERVAL_MS;
        } else {
            setup["RelayUp"] = 0;
            setup["RelayDown"] = 0;
            setup["OpenStepTimeMs"] = Defaults.Components.Blinds.OpenStepTimeMs;
            setup["CloseStepTimeMs"] = Defaults.Components.Blinds.CloseStepTimeMs;
            setup["OpenCorrectionFactor"] = Defaults.Components.Blinds.OpenCorrectionFactor;
            setup["CloseCorrectionFactor"] = Defaults.Components.Blinds.CloseCorrectionFactor;
            setup["EndstopMarginMs"] = Defaults.Components.Blinds.EndstopMarginMs;
            setup["ReversalDelayMs"] = blinds::DEFAULT_REVERSAL_DELAY_MS;
            properties["Position"] = 0;
        }

        for (size_t index = 2; index < ::telnetserver::MAX_COMMAND_PARAMETERS && !parameters[index].isEmpty(); ++index) {
            String property;
            String value;
            if (!SplitAssignment(parameters[index], property, value)) {
                output = "Invalid assignment '" + parameters[index] + "'.\r\n";
                return false;
            }

            if (property.equalsIgnoreCase("name")) {
                setup["Name"] = value;
            } else if (property.equalsIgnoreCase("id")) {
                long id = 0;
                if (!ParseInteger(value, 1, INT16_MAX, id) || id != createdID) {
                    output = "Invalid component ID.\r\n";
                    return false;
                }
            } else if (property.equalsIgnoreCase("bus")) {
                const bool isBlinds = componentClass.equalsIgnoreCase("Blinds");
                const bool isThermometer = componentClass.equalsIgnoreCase("Thermometer");
                const bool validBus = isBlinds ? value.equalsIgnoreCase("Group") :
                    isThermometer ? (value.equalsIgnoreCase("Onboard") || value.equalsIgnoreCase("I2C")) :
                    value.equalsIgnoreCase("Onboard");
                if (!validBus) {
                    output = isBlinds ? "Blinds requires bus=Group.\r\n" :
                        isThermometer ? "Thermometer supports bus=Onboard or bus=I2C.\r\n" :
                        "Only bus=Onboard is currently supported.\r\n";
                    return false;
                }
                setup["Bus"] = value.equalsIgnoreCase("I2C") ? "I2C" : isBlinds ? "Group" : "Onboard";
            } else if (componentClass.equalsIgnoreCase("Blinds") && property.equalsIgnoreCase("relayup")) {
                long memberID = 0;
                if (!ParseInteger(value, 1, INT16_MAX, memberID)) return false;
                setup["RelayUp"] = memberID;
            } else if (componentClass.equalsIgnoreCase("Blinds") && property.equalsIgnoreCase("relaydown")) {
                long memberID = 0;
                if (!ParseInteger(value, 1, INT16_MAX, memberID)) return false;
                setup["RelayDown"] = memberID;
            } else if (componentClass.equalsIgnoreCase("Blinds") && property.equalsIgnoreCase("buttonup")) {
                long memberID = 0;
                if (!ParseInteger(value, 1, INT16_MAX, memberID)) return false;
                setup["ButtonUp"] = memberID;
            } else if (componentClass.equalsIgnoreCase("Blinds") && property.equalsIgnoreCase("buttondown")) {
                long memberID = 0;
                if (!ParseInteger(value, 1, INT16_MAX, memberID)) return false;
                setup["ButtonDown"] = memberID;
            } else if (!ApplyConfiguredProperty(item, property, value, true)) {
                output = "Unsupported property '" + property + "'.\r\n";
                return false;
            }
        }

        if (!ValidateCatalog(components)) {
            output = "Invalid component definition, duplicate ID/name, or duplicate GPIO.\r\n";
            return false;
        }

        if (!WriteConfigurationDocument(document)) {
            output = "Error saving configuration.\r\n";
            return false;
        }

        output = "Component created as #" + String(createdID) +
            ". Restart required. Use 'reboot' to apply changes.\r\n";
        return true;
    }

    output = "Unknown comp subcommand. Use: comp list|tree|status|set|trigger|rename|remove|add\r\n";
    return false;
}
