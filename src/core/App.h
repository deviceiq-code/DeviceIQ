#pragma once

class App {
    public:
        App();
        void begin();
    private:
        void initializeCore();
        void initializeConfig();
        void initializeNetwork();
        void initializeComponents();
        void initializeAutomation();
        void createTasks();
};