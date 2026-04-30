#include "sysctl.h"
#include "console.h"
#include "kernel.h"
#include <iostream>
#include <string>
#include <windows.h>
#include <vector>
#include <handleapi.h>

std::vector<std::string> Services = {};

void Sysctl::services() {
    Console::titlebar("sysctl services");
    Console::println("Available services:");
    if (Services.empty()) {
        Console::println("No services are currently running.");
    } else {
        for (const auto& service : Services) {
            Console::println("- " + service);
        }
    }
}

void Sysctl::netmngr() {
    Console::titlebar("sysctl: network manager");
    Console::println("Network Manager is currently under development. Please check back later for updates.");
}