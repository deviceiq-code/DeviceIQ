#include "DateTimeCommands.h"

#include <cstdio>
#include <cstdlib>

#include "CommandHelpers.h"
#include "core/Globals.h"

namespace {
    using cli::AppendSetting;
    using cli::BoolValue;

    // Howard Hinnant's days_from_civil algorithm (public domain): converts a
    // proleptic Gregorian calendar date directly to a day count since the
    // 1970-01-01 epoch, with no dependency on timegm()/mktime() or the C
    // library's notion of the local timezone (not reliably configured on
    // this device, and timegm() isn't available in this toolchain at all).
    // Mirrors the same helper in src/web/HTTPServer.cpp.
    int64_t DaysFromCivil(int year, unsigned month, unsigned day) {
        year -= month <= 2;
        const int64_t era = (year >= 0 ? year : year - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(year - era * 400);
        const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + static_cast<int64_t>(doe) - 719468;
    }

    time_t UTCEpochFromFields(int year, int month, int day, int hour, int minute, int second) {
        const int64_t days = DaysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
        return static_cast<time_t>(days * 86400 + hour * 3600 + minute * 60 + second);
    }

    bool ParseDateTime(const String& dateValue, const String& timeValue, time_t& utcEpoch) {
        int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
        const int dateFields = sscanf(dateValue.c_str(), "%d-%d-%d", &year, &month, &day);
        const int timeFields = sscanf(timeValue.c_str(), "%d:%d:%d", &hour, &minute, &second);

        if (dateFields != 3 || timeFields < 2) return false;
        if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 ||
            hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return false;

        // The admin enters local wall-clock time, but Clock stores UTC and
        // only adds the configured time zone offset when formatting for
        // display (rtc::GetTimeInfo) - so that offset has to be subtracted
        // here to get back to the UTC epoch SetEpoch() expects.
        const time_t enteredAsUTC = UTCEpochFromFields(year, month, day, hour, minute, second);
        utcEpoch = enteredAsUTC - static_cast<time_t>(Clock.TimeZone()) * 3600;
        return true;
    }

    bool Usage(String& output) {
        output = "Usage: datetime [show]\r\n"
                 "       datetime set <YYYY-MM-DD> <HH:MM:SS>\r\n";
        return false;
    }

    void ShowAll(String& output) {
        AppendSetting(output, "Current Date", Clock.GetDate());
        AppendSetting(output, "Current Time", Clock.GetTime());
        AppendSetting(output, "NTP Enabled", BoolValue(Settings.General.NTPUpdate()));
        if (Settings.General.NTPUpdate()) {
            output += "\r\nNTP is enabled; manual changes are rejected. Use 'ntp enabled off' first.\r\n";
        }
    }

    bool IsDateTimeMutation(String* parameters) noexcept {
        String command = parameters[0];
        command.toLowerCase();
        return command == "set";
    }

    bool ExecuteDateTimeCommand(String* parameters, String& output) {
        output.clear();
        String command = parameters[0];
        command.toLowerCase();

        if (command.isEmpty() || command == "show") {
            if (!parameters[1].isEmpty()) return Usage(output);
            ShowAll(output);
            return true;
        }

        if (command != "set") return Usage(output);

        if (parameters[1].isEmpty() || parameters[2].isEmpty() || !parameters[3].isEmpty()) {
            return Usage(output);
        }

        if (Settings.General.NTPUpdate()) {
            output = "NTP is enabled; disable it first with 'ntp enabled off'.\r\n";
            return false;
        }

        time_t utcEpoch = 0;
        if (!ParseDateTime(parameters[1], parameters[2], utcEpoch)) {
            output = "Date must be YYYY-MM-DD and time HH:MM:SS (or HH:MM), with valid values.\r\n";
            return false;
        }

        Clock.SetEpoch(utcEpoch);

        AppendSetting(output, "Current Date", Clock.GetDate());
        AppendSetting(output, "Current Time", Clock.GetTime());
        output += "Applied immediately.\r\n";
        return true;
    }
}

bool cli::RegisterDateTimeCommands() {
    return TelnetServer.OnCommand(
        "datetime",
        "Show or manually set the device clock\r\n\r\n"
        "datetime [show]\r\n"
        "datetime set <YYYY-MM-DD> <HH:MM:SS>\r\n"
        "Setting the clock requires an administrative session and NTP to be\r\n"
        "disabled (it would just overwrite a manual change on its next sync).",
        [](WiFiClient& client, String* parameters) {
            const bool mutation = IsDateTimeMutation(parameters);
            if (mutation && !TelnetServer.IsSessionAdmin(client)) {
                Logger.Log("CLI datetime mutation denied for " + client.remoteIP().toString(), logger::LogLevels::Warning);
                client.write(telnetserver::FormatLine("DateTime", "Permission denied.").c_str());
                return;
            }

            String output;
            output.reserve(256);
            const bool success = ExecuteDateTimeCommand(parameters, output);
            if (mutation) {
                telnetserver::SessionInfo session;
                const String identity = TelnetServer.SessionInformation(client, session) ? session.user : String("unknown");
                Logger.Log(
                    "CLI datetime mutation " + String(success ? "accepted" : "rejected") + " for " + identity + "@" +
                        client.remoteIP().toString() + ": datetime " + parameters[0] + " " + parameters[1] + " " + parameters[2],
                    success ? logger::LogLevels::Information : logger::LogLevels::Warning
                );
            }
            const String formatted = telnetserver::FormatBlock("DateTime", output);
            client.write(reinterpret_cast<const uint8_t*>(formatted.c_str()), formatted.length());
        },
        false
    );
}
