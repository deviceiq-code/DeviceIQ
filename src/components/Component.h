#pragma once

#include <atomic>
#include <utility>

#include <Arduino.h>
#include <freertos/FreeRTOS.h>

class component;

struct ComponentCommand {
    static constexpr uint16_t Enable = 0;
    static constexpr uint16_t Disable = 1;
    static constexpr uint16_t CustomCommandBase = 0x0100;

    component* target = nullptr;
    uint16_t code = 0;
    int32_t value = 0;
};

struct ComponentEvent {
    component* source = nullptr;
    uint16_t code = 0;
    int32_t value = 0;
    TickType_t timestamp = 0;
};

struct ComponentDescriptor {
    const char* name;
    uint16_t code;
};

class ComponentRuntime {
    public:
        virtual ~ComponentRuntime() = default;
        virtual bool SendCommand(const ComponentCommand& command, TickType_t timeout = 0) noexcept = 0;
        virtual bool SendCommandFromISR(const ComponentCommand& command, BaseType_t* higherPriorityTaskWoken) noexcept = 0;
        virtual bool Publish(const ComponentEvent& event) noexcept = 0;
        virtual bool PublishFromISR(const ComponentEvent& event, BaseType_t* higherPriorityTaskWoken) noexcept = 0;
};

class component {
    public:
        enum class Buses : uint8_t {
            Group,
            Onboard,
            I2C
        };

        enum class Classes : uint8_t {
            Base,
            Relay,
            PIR,
            Button,
            Blinds,
            Thermometer,
            CurrentMeter,
            Doorbell,
            ContactSensor
        };

        component(String name, int16_t id, Buses bus = Buses::Onboard, uint8_t address = 0, bool enabled = true);
        virtual ~component() = default;

        component(const component&) = delete;
        component& operator=(const component&) = delete;
        component(component&&) = delete;
        component& operator=(component&&) = delete;

        [[nodiscard]] const String& Name() const noexcept { return pName; }
        [[nodiscard]] int16_t ID() const noexcept { return pID; }
        [[nodiscard]] bool Enabled() const noexcept { return pEnabled.load(std::memory_order_relaxed); }
        [[nodiscard]] bool Configured() const noexcept { return pConfigured.load(std::memory_order_acquire); }
        [[nodiscard]] bool Initialized() const noexcept { return pInitialized.load(std::memory_order_acquire); }
        [[nodiscard]] Buses Bus() const noexcept { return pBus; }
        [[nodiscard]] uint8_t Address() const noexcept { return pAddress; }
        [[nodiscard]] virtual Classes Class() const noexcept { return Classes::Base; }

        [[nodiscard]] bool ResolveEvent(const String& name, uint16_t& code) const noexcept;
        [[nodiscard]] bool ResolveCommand(const String& name, uint16_t& code) const noexcept;

    protected:
        // Configure declares pins/resources. Initialize activates hardware only
        // after every component has completed Configure().
        virtual bool Configure() noexcept { return true; }
        virtual bool Initialize() noexcept { return true; }
        virtual void EnabledChanged(bool enabled) noexcept { (void)enabled; }

        // Called only by ComponentManager's task. Implementations must not block.
        virtual void Control(TickType_t now) { (void)now; }
        virtual void HandleCommand(const ComponentCommand& command) { (void)command; }

        virtual const ComponentDescriptor* EventDescriptors(size_t& count) const noexcept;
        virtual const ComponentDescriptor* CommandDescriptors(size_t& count) const noexcept;

        [[nodiscard]] bool RequestCommand(uint16_t code, int32_t value = 0, TickType_t timeout = 0) noexcept;
        [[nodiscard]] bool RequestCommandFromISR(uint16_t code, int32_t value, BaseType_t* higherPriorityTaskWoken) noexcept;
        [[nodiscard]] bool PublishEvent(uint16_t code, int32_t value = 0) noexcept;
        [[nodiscard]] bool PublishEventFromISR(uint16_t code, int32_t value, BaseType_t* higherPriorityTaskWoken) noexcept;

    private:
        friend class ComponentManager;

        void SetRuntime(ComponentRuntime* runtime) noexcept { pRuntime = runtime; }
        void SetEnabled(bool value) noexcept { pEnabled.store(value, std::memory_order_relaxed); }
        void SetConfigured(bool value) noexcept { pConfigured.store(value, std::memory_order_release); }
        void SetInitialized(bool value) noexcept { pInitialized.store(value, std::memory_order_release); }

        const String pName;
        const int16_t pID;
        std::atomic<bool> pEnabled;
        std::atomic<bool> pConfigured{false};
        std::atomic<bool> pInitialized{false};
        const Buses pBus;
        const uint8_t pAddress;
        ComponentRuntime* pRuntime = nullptr;
};
