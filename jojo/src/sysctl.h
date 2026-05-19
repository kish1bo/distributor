#pragma once

#include "kernel.h"
class Sysctl {
public:
    Sysctl() = default;
    void sysctl_init();
    void handle(const Command& cmd);
    void services(const std::string& parameter);
    void netmngr(const std::string& parameter, const std::string& extra);
    void registerCommands(class Kernel* kernel);
};