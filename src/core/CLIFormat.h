#pragma once

#include <Arduino.h>

namespace CLIFormat {
    static constexpr size_t LabelWidth = 15;

    inline String Prefix(String label) {
        label.trim();
        if (label.length() > LabelWidth) label.remove(LabelWidth);
        while (label.length() < LabelWidth) label += ' ';
        return label + "| ";
    }

    inline String ContinuationPrefix() {
        String prefix;
        prefix.reserve(LabelWidth + 2);
        for (size_t index = 0; index < LabelWidth; ++index) prefix += ' ';
        prefix += "| ";
        return prefix;
    }

    inline String Line(const String& label, const String& text) {
        return Prefix(label) + text + "\r\n";
    }

    inline String Block(const String& label, const String& text) {
        String output;
        output.reserve(text.length() + 32);
        const String firstPrefix = Prefix(label);
        const String continuation = ContinuationPrefix();
        size_t offset = 0;
        bool first = true;

        while (offset < text.length()) {
            const int newline = text.indexOf('\n', offset);
            const size_t end = newline < 0 ? text.length() : static_cast<size_t>(newline);
            size_t contentEnd = end;
            if (contentEnd > offset && text[contentEnd - 1] == '\r') --contentEnd;
            output += first ? firstPrefix : continuation;
            output += text.substring(offset, contentEnd);
            output += "\r\n";
            first = false;
            if (newline < 0) break;
            offset = static_cast<size_t>(newline) + 1;
        }

        if (first) output = firstPrefix + "\r\n";
        return output;
    }
}
