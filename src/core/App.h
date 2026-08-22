#pragma once

class App {
    public:
        App() = default;
        void Start();

    private:
        bool InitializeFileSystem();
        bool InitializeLogger();
        bool InitializeNetwork();
        bool InitializeTelnetServer();
        bool RegisterTelnetCommands();
        void DeviceRestart();
        void LogConfigurationStatus(bool configurationLoaded);

        static void LogNetworkStatus();
};
