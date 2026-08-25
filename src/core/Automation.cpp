#include "Automation.h"

#include <cstdlib>

#include "Globals.h"

namespace {
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
}

void automation::Clear() noexcept {
    for (size_t index = 0; index < pBindingCount; ++index) pBindings[index] = Binding();
    pBindingCount = 0;
}

bool automation::Register(component& source, const String& eventName, const String& script, String& error) noexcept {
    uint16_t eventCode = 0;
    if (!source.ResolveEvent(eventName, eventCode)) {
        error = "unknown event";
        return false;
    }

    Binding candidate;
    candidate.source = &source;
    candidate.eventCode = eventCode;
    candidate.eventName = eventName;

    if (!ParseScript(script, candidate, error)) return false;
    candidate.argument.replace("%NAME%", source.Name());
    candidate.targetSelector.replace("%NAME%", source.Name());
    candidate.property.replace("%NAME%", source.Name());
    candidate.value.replace("%NAME%", source.Name());

    for (size_t index = 0; index < pBindingCount; ++index) {
        if (pBindings[index].source == &source && pBindings[index].eventCode == eventCode) {
            pBindings[index] = std::move(candidate);
            return true;
        }
    }

    if (pBindingCount >= MAX_BINDINGS) {
        error = "event binding limit reached";
        return false;
    }

    pBindings[pBindingCount++] = std::move(candidate);
    return true;
}

bool automation::Execute(const ComponentEvent& event) noexcept {
    if (event.source == nullptr) return false;

    bool handled = false;
    for (size_t index = 0; index < pBindingCount; ++index) {
        const Binding& binding = pBindings[index];
        if (binding.source != event.source || binding.eventCode != event.code) continue;
        handled = true;
        (void)Execute(binding);
    }
    return handled;
}

bool automation::ParseScript(const String& script, Binding& binding, String& error) noexcept {
    String text = script;
    text.trim();
    if (text.isEmpty()) {
        error = "empty action";
        return false;
    }

    const int open = text.indexOf('(');
    if (open <= 0 || !text.endsWith(")")) {
        error = "expected command(arguments)";
        return false;
    }

    String command = text.substring(0, open);
    command.trim();
    String argument = text.substring(open + 1, text.length() - 1);
    argument.trim();

    if (command.equalsIgnoreCase("log")) {
        binding.action = Action::Log;
        binding.argument = argument;
        return true;
    }

    if (!command.equalsIgnoreCase("compset")) {
        error = "unsupported command '" + command + "'";
        return false;
    }

    const int separator = argument.indexOf(' ');
    if (separator <= 0) {
        error = "expected compset(selector property=value)";
        return false;
    }

    binding.targetSelector = argument.substring(0, separator);
    binding.targetSelector.trim();
    const String assignment = argument.substring(separator + 1);
    if (binding.targetSelector.isEmpty() || !SplitAssignment(assignment, binding.property, binding.value)) {
        error = "expected compset(selector property=value)";
        return false;
    }

    binding.action = Action::ComponentSet;
    return true;
}

component* automation::ResolveComponent(const String& selector) noexcept {
    if (selector.startsWith("#")) {
        if (selector.length() == 1) return nullptr;
        char* end = nullptr;
        const long id = std::strtol(selector.c_str() + 1, &end, 10);
        if (end == selector.c_str() + 1 || *end != '\0' || id < INT16_MIN || id > INT16_MAX) return nullptr;
        return ComponentController.FindByID(static_cast<int16_t>(id));
    }

    return ComponentController.FindByName(selector);
}

const char* automation::PropertyResultName(ComponentPropertyResult result) noexcept {
    switch (result) {
        case ComponentPropertyResult::Accepted: return "accepted";
        case ComponentPropertyResult::PropertyNotSupported: return "property not supported";
        case ComponentPropertyResult::InvalidValue: return "invalid value";
        case ComponentPropertyResult::ComponentDisabled: return "component disabled";
        case ComponentPropertyResult::CommandRejected: return "command rejected";
        default: return "unknown error";
    }
}

bool automation::Execute(const Binding& binding) noexcept {
    if (binding.action == Action::Log) {
        return Logger.Log(binding.argument, logger::LogLevels::Information);
    }

    component* target = ResolveComponent(binding.targetSelector);
    if (target == nullptr) {
        Logger.Log(
            "Automation: " + binding.source->Name() + "." + binding.eventName +
                " target '" + binding.targetSelector + "' not found",
            logger::LogLevels::Warning
        );
        return false;
    }

    if (binding.property.equalsIgnoreCase("state") && binding.value.equalsIgnoreCase("toggle")) {
        uint16_t commandCode = 0;
        if (!target->ResolveCommand("Toggle", commandCode)) {
            Logger.Log(
                "Automation: " + target->Name() + ".state does not support toggle",
                logger::LogLevels::Warning
            );
            return false;
        }

        ComponentCommand command;
        command.target = target;
        command.code = commandCode;
        if (ComponentController.SendCommand(command, COMMAND_TIMEOUT)) return true;

        Logger.Log("Automation: command rejected for " + target->Name(), logger::LogLevels::Warning);
        return false;
    }

    const ComponentPropertyResult result = target->SetProperty(binding.property, binding.value, COMMAND_TIMEOUT);
    if (result == ComponentPropertyResult::Accepted) return true;

    Logger.Log(
        "Automation: cannot set " + target->Name() + "." + binding.property +
            "=" + binding.value + " (" + PropertyResultName(result) + ")",
        logger::LogLevels::Warning
    );
    return false;
}
