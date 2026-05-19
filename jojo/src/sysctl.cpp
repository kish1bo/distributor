#include "sysctl.h"
#include "console.h"
#include "kernel.h"
#include <iostream>
#include <string>
#include <windows.h>
#include <vector>
#include <map>
#include <algorithm>

struct ServiceInfo {
    std::string description;
    bool running;
    bool enabled;
};

static std::map<std::string, ServiceInfo> Services = {
    {"Wireless Network", {"Wireless network adapter", true, true}},
    {"Ethernet", {"Wired network adapter", false, false}},
    {"Bluetooth", {"Bluetooth connectivity service", false, false}},
    {"Manifest", {"System manifest manager", true, true}}
};

static std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

static void printServiceStatus(const std::string& name, const ServiceInfo& service) {
    Console::print(name + " ");
    Console::print(service.running ? "running" : "stopped");
    Console::print(" ");
    Console::print(service.enabled ? "enabled" : "disabled");
    Console::print(" ");
    Console::println(service.description);
}

void Sysctl::handle(const Command& cmd) {
    if (cmd.args.empty()) {
        Console::println("Usage: systemctl <command> [service]");
        Console::println("Commands: status, services, start, stop, restart, enable, disable, netmngr");
        return;
    }

    std::string action = toLowerCopy(cmd.args[0]);
    if (action == "services") {
        if (cmd.args.size() == 1 || cmd.args[1] == "list" || cmd.args[1] == "--list") {
            Console::titlebar("Services");
            Console::println("Available services:");
            for (const auto& pair : Services) {
                printServiceStatus(pair.first, pair.second);
            }
            return;
        }

        if (cmd.args.size() < 3) {
            Console::errormsg("MISSING_ACTION", "Usage: systemctl services <start|stop|restart|enable|disable> <service>");
            return;
        }

        std::string verb = toLowerCopy(cmd.args[1]);
        std::string serviceName = cmd.args[2];
        auto it = Services.find(serviceName);
        if (it == Services.end()) {
            Console::errormsg("SERVICE_NOT_FOUND", "Service '" + serviceName + "' not found.");
            return;
        }

        if (verb == "start") {
            it->second.running = true;
            Console::println("Service '" + serviceName + "' started.");
            return;
        }
        if (verb == "stop") {
            it->second.running = false;
            Console::println("Service '" + serviceName + "' stopped.");
            return;
        }
        if (verb == "restart") {
            it->second.running = false;
            it->second.running = true;
            Console::println("Service '" + serviceName + "' restarted.");
            return;
        }
        if (verb == "enable") {
            it->second.enabled = true;
            Console::println("Service '" + serviceName + "' enabled.");
            return;
        }
        if (verb == "disable") {
            it->second.enabled = false;
            Console::println("Service '" + serviceName + "' disabled.");
            return;
        }

        Console::errormsg("UNKNOWN_PARAMETER", "Unknown services command. Use list/start/stop/restart/enable/disable.");
        return;
    }

    if (action == "status") {
        if (cmd.args.size() == 1) {
            Console::println("Service status:");
            for (const auto& pair : Services) {
                printServiceStatus(pair.first, pair.second);
            }
            return;
        }
        std::string serviceName = cmd.args[1];
        auto it = Services.find(serviceName);
        if (it == Services.end()) {
            Console::errormsg("SERVICE_NOT_FOUND", "Service '" + serviceName + "' not found.");
            return;
        }
        printServiceStatus(it->first, it->second);
        return;
    }

    if (action == "start" || action == "stop" || action == "restart" || action == "enable" || action == "disable") {
        if (cmd.args.size() < 2) {
            Console::errormsg("MISSING_ACTION", "Usage: systemctl " + action + " <service>");
            return;
        }
        std::string serviceName = cmd.args[1];
        auto it = Services.find(serviceName);
        if (it == Services.end()) {
            Console::errormsg("SERVICE_NOT_FOUND", "Service '" + serviceName + "' not found.");
            return;
        }

        if (action == "start") {
            it->second.running = true;
            Console::println("Service '" + serviceName + "' started.");
            return;
        }
        if (action == "stop") {
            it->second.running = false;
            Console::println("Service '" + serviceName + "' stopped.");
            return;
        }
        if (action == "restart") {
            it->second.running = false;
            it->second.running = true;
            Console::println("Service '" + serviceName + "' restarted.");
            return;
        }
        if (action == "enable") {
            it->second.enabled = true;
            Console::println("Service '" + serviceName + "' enabled.");
            return;
        }
        if (action == "disable") {
            it->second.enabled = false;
            Console::println("Service '" + serviceName + "' disabled.");
            return;
        }
    }

    if (action == "netmngr") {
        if (cmd.args.size() < 2) {
            Console::println("Usage: systemctl netmngr ping|stop|status");
            return;
        }
        std::string subcmd = toLowerCopy(cmd.args[1]);
        if (subcmd == "ping") {
            Console::println("Pinging network...");
            Console::println("Network manager is " + std::string(Services["Wireless Network"].running ? "online" : "offline") + ".");
            return;
        }
        if (subcmd == "stop") {
            Services["Wireless Network"].running = false;
            Console::println("Network manager stopped.");
            return;
        }
        if (subcmd == "status") {
            printServiceStatus("Wireless Network", Services["Wireless Network"]);
            return;
        }
        Console::errormsg("UNKNOWN_PARAMETER", "Unknown netmngr command. Use ping/stop/status.");
        return;
    }

    Console::errormsg("UNKNOWN_COMMAND", "systemctl command not recognized. Use status/services/netmngr/start/stop/restart/enable/disable.");
}

void Sysctl::sysctl_init() {
    // Initialization hook for the sysctl module.
    // Currently no startup state is required.
}

void Sysctl::services(const std::string& parameter) {
    (void)parameter;
}

void Sysctl::netmngr(const std::string& parameter, const std::string& extra) {
    (void)parameter;
    (void)extra;
}

void Sysctl::registerCommands(Kernel* kernel) {
    if (!kernel) return;
    kernel->addCommand("systemctl", [this](const Command& cmd){ this->handle(cmd); }, SystemState::GUEST);
    kernel->addCommand("services", [this](const Command& cmd){ this->handle(cmd); }, SystemState::GUEST);
}
