#include "Settings.h"

#include <ArduinoJson.h>
#include <cstdlib>
#include <memory>
#include <new>

#include "Globals.h"
#include "components/Button.h"
#include "components/Relay.h"

namespace {
    constexpr size_t MaxConfiguredComponents = 32;

    struct RelayConfiguration {
        String name;
        int16_t id = 0;
        uint8_t address = 0;
        relay::RelayTypes type = relay::RelayTypes::NormallyOpen;
        relay::DriveModes driveMode = relay::DriveModes::ActiveHigh;
        bool state = false;
        bool enabled = true;
    };

    bool ParseRelayConfiguration(JsonObjectConst object, RelayConfiguration& result) {
        if (!object["Name"].is<const char*>() ||
            !object["ID"].is<int>() ||
            !object["Class"].is<const char*>() ||
            !object["Bus"].is<const char*>() ||
            !object["Address"].is<int>()) {
            return false;
        }

        result.name = object["Name"].as<const char*>();
        result.name.trim();
        if (result.name.isEmpty()) return false;

        const int id = object["ID"].as<int>();
        const int address = object["Address"].as<int>();
        if (id < INT16_MIN || id > INT16_MAX || address < 0 || address > UINT8_MAX) return false;

        const String componentClass = object["Class"].as<const char*>();
        const String bus = object["Bus"].as<const char*>();
        if (!componentClass.equalsIgnoreCase("Relay") || !bus.equalsIgnoreCase("Onboard")) return false;

        result.id = static_cast<int16_t>(id);
        result.address = static_cast<uint8_t>(address);

        if (!object["Type"].isNull()) {
            if (!object["Type"].is<const char*>()) return false;
            const String type = object["Type"].as<const char*>();
            if (type.equalsIgnoreCase("NormallyOpen")) {
                result.type = relay::RelayTypes::NormallyOpen;
            } else if (type.equalsIgnoreCase("NormallyClosed")) {
                result.type = relay::RelayTypes::NormallyClosed;
            } else {
                return false;
            }
        }

        if (!object["DriveMode"].isNull()) {
            if (!object["DriveMode"].is<const char*>()) return false;
            const String driveMode = object["DriveMode"].as<const char*>();
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

    bool ParseButtonConfiguration(JsonObjectConst object, ButtonConfiguration& result) {
        if (!object["Name"].is<const char*>() ||
            !object["ID"].is<int>() ||
            !object["Class"].is<const char*>() ||
            !object["Bus"].is<const char*>() ||
            !object["Address"].is<int>()) {
            return false;
        }

        result.name = object["Name"].as<const char*>();
        result.name.trim();
        if (result.name.isEmpty()) return false;

        const int id = object["ID"].as<int>();
        const int address = object["Address"].as<int>();
        if (id < INT16_MIN || id > INT16_MAX || address < 0 || address > UINT8_MAX) return false;

        const String componentClass = object["Class"].as<const char*>();
        const String bus = object["Bus"].as<const char*>();
        if (!componentClass.equalsIgnoreCase("Button") || !bus.equalsIgnoreCase("Onboard")) return false;

        result.id = static_cast<int16_t>(id);
        result.address = static_cast<uint8_t>(address);

        if (!object["ActiveLevel"].isNull()) {
            if (!object["ActiveLevel"].is<const char*>()) return false;
            const String activeLevel = object["ActiveLevel"].as<const char*>();
            if (activeLevel.equalsIgnoreCase("High")) {
                result.activeLevel = button::ActiveLevels::High;
            } else if (activeLevel.equalsIgnoreCase("Low")) {
                result.activeLevel = button::ActiveLevels::Low;
            } else {
                return false;
            }
        }

        if (!object["InputMode"].isNull()) {
            if (!object["InputMode"].is<const char*>()) return false;
            const String inputMode = object["InputMode"].as<const char*>();
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

        if (!ReadOptionalTime(object, "DebounceTimeMs", result.debounceTimeMs) ||
            !ReadOptionalTime(object, "LongClickTimeMs", result.longClickTimeMs) ||
            !ReadOptionalTime(object, "MultiClickTimeMs", result.multiClickTimeMs)) {
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
}

bool settings::InstallComponents(const String& configfilename) noexcept {
    if (ComponentController.IsStarted() || ComponentController.Count() != 0) return false;

    const String path = configfilename.length() ? configfilename : String(Defaults.ConfigFileName);
    String content;
    if (FileSystem.Read(path.c_str(), content) != filesystem::Result::Ok || content.isEmpty()) return false;

    JsonDocument document;
    if (deserializeJson(document, content)) return false;

    const JsonArrayConst components = document["Components"].as<JsonArrayConst>();
    if (components.isNull() || components.size() > MaxConfiguredComponents) return false;

    struct ComponentIdentity {
        String name;
        int16_t id;
        uint8_t address;
    };

    ComponentIdentity identities[MaxConfiguredComponents];
    size_t count = 0;

    for (JsonVariantConst value : components) {
        if (!value.is<JsonObjectConst>()) return false;
        const JsonObjectConst object = value.as<JsonObjectConst>();
        if (!object["Class"].is<const char*>()) return false;

        const String componentClass = object["Class"].as<const char*>();
        if (componentClass.equalsIgnoreCase("Relay")) {
            RelayConfiguration configuration;
            if (!ParseRelayConfiguration(object, configuration)) return false;
            identities[count] = {configuration.name, configuration.id, configuration.address};
        } else if (componentClass.equalsIgnoreCase("Button")) {
            ButtonConfiguration configuration;
            if (!ParseButtonConfiguration(object, configuration)) return false;
            identities[count] = {configuration.name, configuration.id, configuration.address};
        } else {
            return false;
        }

        for (size_t previous = 0; previous < count; ++previous) {
            if (identities[previous].id == identities[count].id ||
                identities[previous].name.equalsIgnoreCase(identities[count].name) ||
                identities[previous].address == identities[count].address) {
                return false;
            }
        }

        ++count;
    }

    std::unique_ptr<component> instances[MaxConfiguredComponents];
    size_t index = 0;

    for (JsonVariantConst value : components) {
        const JsonObjectConst object = value.as<JsonObjectConst>();
        const String componentClass = object["Class"].as<const char*>();

        if (componentClass.equalsIgnoreCase("Relay")) {
            RelayConfiguration configuration;
            if (!ParseRelayConfiguration(object, configuration)) return false;
            instances[index].reset(new (std::nothrow) relay(
                configuration.name,
                configuration.id,
                component::Buses::Onboard,
                configuration.address,
                configuration.type,
                configuration.driveMode,
                configuration.state,
                configuration.enabled
            ));
        } else {
            ButtonConfiguration configuration;
            if (!ParseButtonConfiguration(object, configuration)) return false;
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
                configuration.enabled
            ));
        }

        if (!instances[index]) return false;
        ++index;
    }

    for (index = 0; index < count; ++index) {
        if (!ComponentController.Register(std::move(instances[index]))) return false;
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
        if (!ParseInteger(selector.substring(1), INT16_MIN, INT16_MAX, parsed)) return false;
        id = static_cast<int16_t>(parsed);
        return true;
    }

    JsonObject FindConfiguredComponent(JsonArray components, const String& selector) {
        int16_t id = 0;
        const bool byID = SelectorID(selector, id);
        for (JsonObject item : components) {
            if (byID) {
                if ((item["ID"] | INT32_MIN) == id) return item;
            } else {
                const String name = item["Name"] | "";
                if (name.equalsIgnoreCase(selector)) return item;
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

    JsonObject ResolveConfiguredComponent(JsonArray components, const String& selector) {
        JsonObject configured = FindConfiguredComponent(components, selector);
        if (!configured.isNull()) return configured;

        component* runtime = FindRuntimeComponent(selector);
        if (runtime == nullptr) return JsonObject();
        return FindConfiguredComponent(components, "#" + String(runtime->ID()));
    }

    bool ValidateCatalog(JsonArrayConst components) {
        if (components.isNull() || components.size() > MaxConfiguredComponents) return false;

        struct Identity {
            String name;
            int16_t id;
            uint8_t address;
        };

        Identity identities[MaxConfiguredComponents];
        size_t count = 0;

        for (JsonObjectConst object : components) {
            if (!object["Class"].is<const char*>()) return false;
            const String componentClass = object["Class"].as<const char*>();

            if (componentClass.equalsIgnoreCase("Relay")) {
                RelayConfiguration configuration;
                if (!ParseRelayConfiguration(object, configuration)) return false;
                identities[count] = {configuration.name, configuration.id, configuration.address};
            } else if (componentClass.equalsIgnoreCase("Button")) {
                ButtonConfiguration configuration;
                if (!ParseButtonConfiguration(object, configuration)) return false;
                identities[count] = {configuration.name, configuration.id, configuration.address};
            } else {
                return false;
            }

            for (size_t previous = 0; previous < count; ++previous) {
                if (identities[previous].id == identities[count].id ||
                    identities[previous].name.equalsIgnoreCase(identities[count].name) ||
                    identities[previous].address == identities[count].address) return false;
            }
            ++count;
        }
        return true;
    }

    bool ConfigurationMatchesRuntime(JsonObjectConst configured, const component& runtime) {
        if ((configured["ID"] | INT32_MIN) != runtime.ID() ||
            (configured["Address"] | -1) != runtime.Address()) return false;

        const String configuredName = configured["Name"] | "";
        const JsonObjectConst properties = configured["Properties"].as<JsonObjectConst>();
        if (configuredName != runtime.Name() || properties.isNull() ||
            (properties["Enabled"] | true) != runtime.Enabled()) return false;

        if (runtime.Class() == component::Classes::Relay) {
            RelayConfiguration configuration;
            if (!ParseRelayConfiguration(configured, configuration)) return false;
            const relay& value = static_cast<const relay&>(runtime);
            return configuration.type == value.Type() && configuration.driveMode == value.DriveMode();
        }

        if (runtime.Class() == component::Classes::Button) {
            ButtonConfiguration configuration;
            if (!ParseButtonConfiguration(configured, configuration)) return false;
            const button& value = static_cast<const button&>(runtime);
            return configuration.activeLevel == value.ActiveLevel() &&
                   configuration.inputMode == value.InputMode() &&
                   configuration.debounceTimeMs == value.DebounceTime() &&
                   configuration.longClickTimeMs == value.LongClickTime() &&
                   configuration.multiClickTimeMs == value.MultiClickTime();
        }

        return false;
    }

    bool CatalogPending(JsonArrayConst components) {
        if (components.size() != ComponentController.Count()) return true;
        for (JsonObjectConst configured : components) {
            const int id = configured["ID"] | INT32_MIN;
            component* runtime = id < INT16_MIN || id > INT16_MAX
                ? nullptr
                : ComponentController.FindByID(static_cast<int16_t>(id));
            if (runtime == nullptr || !ConfigurationMatchesRuntime(configured, *runtime)) return true;
        }
        return false;
    }

    bool ApplyConfiguredProperty(JsonObject item, const String& property, const String& text, bool allowState) {
        if (property.equalsIgnoreCase("enabled")) {
            bool value = false;
            if (!ParseConfigBoolean(text, value)) return false;
            item["Properties"].to<JsonObject>()["Enabled"] = value;
            return true;
        }

        if (property.equalsIgnoreCase("address")) {
            long value = 0;
            if (!ParseInteger(text, 0, UINT8_MAX, value)) return false;
            item["Address"] = value;
            return true;
        }

        const String componentClass = item["Class"] | "";
        if (componentClass.equalsIgnoreCase("Relay")) {
            if (allowState && property.equalsIgnoreCase("state")) {
                bool value = false;
                if (!ParseConfigBoolean(text, value)) return false;
                item["Properties"].to<JsonObject>()["State"] = value;
                return true;
            }
            if (property.equalsIgnoreCase("type")) {
                if (!text.equalsIgnoreCase("NormallyOpen") && !text.equalsIgnoreCase("NormallyClosed")) return false;
                item["Type"] = text.equalsIgnoreCase("NormallyOpen") ? "NormallyOpen" : "NormallyClosed";
                return true;
            }
            if (property.equalsIgnoreCase("drivemode")) {
                if (!text.equalsIgnoreCase("ActiveHigh") && !text.equalsIgnoreCase("ActiveLow")) return false;
                item["DriveMode"] = text.equalsIgnoreCase("ActiveHigh") ? "ActiveHigh" : "ActiveLow";
                return true;
            }
        }

        if (componentClass.equalsIgnoreCase("Button")) {
            if (property.equalsIgnoreCase("activelevel")) {
                if (!text.equalsIgnoreCase("High") && !text.equalsIgnoreCase("Low")) return false;
                item["ActiveLevel"] = text.equalsIgnoreCase("High") ? "High" : "Low";
                return true;
            }
            if (property.equalsIgnoreCase("inputmode")) {
                if (!text.equalsIgnoreCase("Floating") && !text.equalsIgnoreCase("PullUp") && !text.equalsIgnoreCase("PullDown")) return false;
                item["InputMode"] = text.equalsIgnoreCase("Floating") ? "Floating" :
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
                item[key] = static_cast<uint32_t>(value);
                return true;
            }
        }

        return false;
    }

    bool WriteConfigurationDocument(JsonDocument& document) {
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
        output = "Usage: comp list | status [selector] | set selector property=value | rename selector name=value | remove selector | add class key=value...\r\n";
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

    JsonArray components = document["Components"].as<JsonArray>();
    if (components.isNull()) {
        output = "Components configuration is missing.\r\n";
        return false;
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

        for (JsonObject item : components) {
            const int16_t id = item["ID"] | 0;
            const String name = item["Name"] | "";
            const String componentClass = item["Class"] | "";
            component* runtime = ComponentController.FindByID(id);
            output += "#" + String(id) + " " + componentClass + " " + name;
            output += runtime == nullptr ? " [restart required]\r\n" :
                ConfigurationMatchesRuntime(item, *runtime) ? " [running]\r\n" : " [restart required]\r\n";
        }

        for (size_t index = 0; index < ComponentController.Count(); ++index) {
            component* runtime = ComponentController.At(index);
            bool found = false;
            for (JsonObject item : components) {
                if ((item["ID"] | INT32_MIN) == runtime->ID()) {
                    found = true;
                    break;
                }
            }
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

        JsonObject configured = ResolveConfiguredComponent(components, parameters[1]);
        component* runtime = configured.isNull() ? FindRuntimeComponent(parameters[1]) :
            ComponentController.FindByID(static_cast<int16_t>(configured["ID"] | 0));

        if (configured.isNull() && runtime == nullptr) {
            output = "Component '" + parameters[1] + "' not found.\r\n";
            return false;
        }

        if (runtime != nullptr) runtime->GetInfo(output);
        else output += "Runtime        | Not loaded\r\n";

        if (!configured.isNull()) {
            output += "ConfiguredName | " + String(configured["Name"] | "") + "\r\n";
            output += "ConfiguredID   | " + String(configured["ID"] | 0) + "\r\n";
        } else {
            output += "Configuration  | Pending removal\r\n";
        }

        const bool pending = configured.isNull() || runtime == nullptr ||
            !ConfigurationMatchesRuntime(configured, *runtime);
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

        JsonObject configured = ResolveConfiguredComponent(components, parameters[1]);
        component* runtime = FindRuntimeComponent(parameters[1]);
        if (runtime == nullptr && !configured.isNull()) {
            runtime = ComponentController.FindByID(static_cast<int16_t>(configured["ID"] | 0));
        }

        if (property.equalsIgnoreCase("state")) {
            if (runtime == nullptr) {
                output = "Component is not running; state cannot be changed.\r\n";
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

        JsonObject configured = ResolveConfiguredComponent(components, parameters[1]);
        if (configured.isNull()) {
            output = "Configured component '" + parameters[1] + "' not found.\r\n";
            return false;
        }

        configured["Name"] = value;
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

        JsonObject configured = ResolveConfiguredComponent(components, parameters[1]);
        if (configured.isNull()) {
            output = "Configured component '" + parameters[1] + "' not found.\r\n";
            return false;
        }

        const int16_t removedID = configured["ID"] | 0;
        size_t removeIndex = 0;
        for (JsonObject item : components) {
            if ((item["ID"] | INT32_MIN) == removedID) break;
            ++removeIndex;
        }
        components.remove(removeIndex);

        if (!WriteConfigurationDocument(document)) {
            output = "Error saving configuration.\r\n";
            return false;
        }

        output = "Component removed from configuration. Restart required. Use 'reboot' to apply changes.\r\n";
        return true;
    }

    if (subcommand == "add") {
        if (parameters[1].isEmpty()) {
            output = "Usage: comp add relay|button name=value [id=value] address=value ...\r\n";
            return false;
        }
        if (components.size() >= MaxConfiguredComponents) {
            output = "Maximum component count reached.\r\n";
            return false;
        }

        String componentClass = parameters[1];
        if (!componentClass.equalsIgnoreCase("Relay") && !componentClass.equalsIgnoreCase("Button")) {
            output = "Unsupported component class. Expected relay or button.\r\n";
            return false;
        }

        JsonObject item = components.add<JsonObject>();
        item["Class"] = componentClass.equalsIgnoreCase("Relay") ? "Relay" : "Button";
        item["Bus"] = "Onboard";
        item["Address"] = -1;
        JsonObject properties = item["Properties"].to<JsonObject>();
        properties["Enabled"] = true;
        item["Events"].to<JsonObject>();

        if (componentClass.equalsIgnoreCase("Relay")) {
            item["Type"] = "NormallyOpen";
            item["DriveMode"] = "ActiveHigh";
            properties["State"] = false;
        } else {
            item["ActiveLevel"] = "Low";
            item["InputMode"] = "PullUp";
            item["DebounceTimeMs"] = button::DEFAULT_DEBOUNCE_TIME_MS;
            item["LongClickTimeMs"] = button::DEFAULT_LONG_CLICK_TIME_MS;
            item["MultiClickTimeMs"] = button::DEFAULT_MULTI_CLICK_TIME_MS;
        }

        bool idProvided = false;
        for (size_t index = 2; index < ::telnetserver::MAX_COMMAND_PARAMETERS && !parameters[index].isEmpty(); ++index) {
            String property;
            String value;
            if (!SplitAssignment(parameters[index], property, value)) {
                output = "Invalid assignment '" + parameters[index] + "'.\r\n";
                return false;
            }

            if (property.equalsIgnoreCase("name")) {
                item["Name"] = value;
            } else if (property.equalsIgnoreCase("id")) {
                long id = 0;
                if (!ParseInteger(value, 1, INT16_MAX, id)) {
                    output = "Invalid component ID.\r\n";
                    return false;
                }
                item["ID"] = id;
                idProvided = true;
            } else if (property.equalsIgnoreCase("bus")) {
                if (!value.equalsIgnoreCase("Onboard")) {
                    output = "Only bus=Onboard is currently supported.\r\n";
                    return false;
                }
            } else if (!ApplyConfiguredProperty(item, property, value, true)) {
                output = "Unsupported property '" + property + "'.\r\n";
                return false;
            }
        }

        if (!idProvided) {
            int16_t candidate = 1;
            while (candidate < INT16_MAX) {
                bool used = false;
                for (JsonObject configured : components) {
                    if ((configured["ID"] | INT32_MIN) == candidate) {
                        used = true;
                        break;
                    }
                }
                if (!used) break;
                ++candidate;
            }
            item["ID"] = candidate;
        }

        if (!ValidateCatalog(components)) {
            output = "Invalid component definition, duplicate ID/name, or duplicate GPIO.\r\n";
            return false;
        }

        const int16_t createdID = item["ID"] | 0;
        if (!WriteConfigurationDocument(document)) {
            output = "Error saving configuration.\r\n";
            return false;
        }

        output = "Component created as #" + String(createdID) +
            ". Restart required. Use 'reboot' to apply changes.\r\n";
        return true;
    }

    output = "Unknown comp subcommand. Use: comp list|status|set|rename|remove|add\r\n";
    return false;
}
