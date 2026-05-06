#include "sysctl.h"
#include "console.h"
#include "kernel.h"
#include <iostream>
#include <string>
#include <windows.h>
#include <vector>
#include <handleapi.h>
#include <map>

std::map<std::string, std::string> Services = {{"Wireless Network", "Running"}, {"Ethernet", "Stopped"}, {"Bluetooth", "Stopped"}, {"Manifest", "Running"}};

void Sysctl::services(const std::string& parameter) {
    Console::titlebar("Services");
    if (parameter == "--ls") {
        Console::println("Available services:");
        if (Services.empty()) {
            Console::println("No services yet");
        } else {
            for (const auto& service : Services) {
                if (service.second == "Running") {
                    Console::println("*" + service.first + ": ");
                    Console::colortxt("Running", "Green");
                }
                else {
                    Console::println("- " + service.first + ": ");
                    Console::colortxt("Stopped", "Red");
                }
            }
        }
    }
    else if (parameter == "--start" || parameter == "--stop") {
        Console::println("Usage: systemctl services --start/--stop <service name>");
    }
    else if (parameter.rfind("--start ", 0) == 0) {
        std::string serviceName = parameter.substr(8);
        if (Services.find(serviceName) != Services.end()) {
            Services[serviceName] = "Running";
            Console::println("Service '" + serviceName + "' started.");
        } else {
            Console::errormsg("SERVICE_NOT_FOUND", "Service '" + serviceName + "' not found.");
        }
    }
    else if (parameter.rfind("--stop ", 0) == 0) {
        std::string serviceName = parameter.substr(7);
        if (Services.find(serviceName) != Services.end()) {
            Services[serviceName] = "Stopped";
            Console::println("Service '" + serviceName + "' stopped.");
        } else {
            Console::errormsg("SERVICE_NOT_FOUND", "Service '" + serviceName + "' not found.");
        }
    }
    else {
        Console::errormsg("UNKNOWN_PARAMETER", "Unknown parameter. Use --ls, --start <service>, or --stop <service>.");
    }
}

void Sysctl::netmngr(const std::string& parameter, const std::string& extra) {
    if (parameter == "-ping" && extra.empty()) {
        
    }
    else if (parameter == "-stop" && extra.empty()) {
        Console::println("Are you want to stop Network Manager? Y/n");
        std::string input;
        getline(std::cin, input);
        if (input == "Y" || "y") {
            Console::print("Network Manager");
            Console::colortxt(" is stopped", "Red");
            Services.at("Wireless Network") = "Stopped";
        }
    else if (input == "N" || "n") {
        Console::println("Action denied.");
    }
    else {
            Console::println("Action denied.");
        }
    }
    
}