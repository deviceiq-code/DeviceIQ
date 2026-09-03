#pragma once

#include <Arduino.h>

#include "components/Component.h"

class automation final {
    public:
        automation() = default;
        automation(const automation&) = delete;
        automation& operator=(const automation&) = delete;

        void Clear() noexcept;
        [[nodiscard]] bool Register(component& source, const String& eventName, const String& script, String& error) noexcept;
        [[nodiscard]] bool Execute(const ComponentEvent& event) noexcept;
        [[nodiscard]] size_t Count() const noexcept { return pBindingCount; }
        // Checks a script's syntax (e.g. "log(...)" / "compset(selector
        // property=value)") without resolving an event name or binding it
        // to a component - used to validate an action before it's saved to
        // a component's configuration, when no live component instance may
        // exist yet (e.g. a just-added component, pending its first boot).
        [[nodiscard]] static bool ValidateScript(const String& script, String& error) noexcept;

    private:
        enum class Action : uint8_t { Log, ComponentSet };

        struct Binding {
            component* source = nullptr;
            uint16_t eventCode = 0;
            Action action = Action::Log;
            String eventName;
            String argument;
            String targetSelector;
            String property;
            String value;
        };

        static constexpr size_t MAX_BINDINGS = 64;
        static constexpr TickType_t COMMAND_TIMEOUT = pdMS_TO_TICKS(100);

        [[nodiscard]] static bool ParseScript(const String& script, Binding& binding, String& error) noexcept;
        [[nodiscard]] static component* ResolveComponent(const String& selector) noexcept;
        [[nodiscard]] static const char* PropertyResultName(ComponentPropertyResult result) noexcept;
        [[nodiscard]] bool Execute(const Binding& binding) noexcept;

        Binding pBindings[MAX_BINDINGS];
        size_t pBindingCount = 0;
};
