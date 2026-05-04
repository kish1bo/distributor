#include "instalator_tool.h"
#include "console.h"
#include "kernel.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <fstream>
#include <algorithm>

std::vector<std::string> installedPackages = {"git", "curl", "vim"};

void InstalatorTool::listInstalled() {
    Console::println("Installed packages:");
    std::ifstream infile("downloads/packages.txt");
    if (infile.is_open()) {
        std::string line;
        while (std::getline(infile, line)) {
            Console::println("- " + line);
        }
        infile.close();
    } else {
        Console::errormsg("FILE_NOT_FOUND", "Failed to read downloads/packages.txt");
    }
}

void InstalatorTool::install(const std::string& package) {
    installedPackages.push_back(package);
    Console::println("Installing package: " + package);
    std::this_thread::sleep_for(std::chrono::seconds(2)); // Simulate installation time
    std::ofstream outfile("downloads/packages.txt", std::ios::app);
    if (outfile.is_open()) {
        outfile << package << std::endl;
        outfile.close();
    } else {
        Console::errormsg("FILE_NOT_FOUND", "Failed to write to downloads/packages.txt");
    }
    Console::println("Package '" + package + "' installed successfully.");
}

void InstalatorTool::uninstall(const std::string& package) {
    installedPackages.erase(std::remove(installedPackages.begin(), installedPackages.end(), package), installedPackages.end());
    Console::println("Uninstalling package: " + package);
    std::this_thread::sleep_for(std::chrono::seconds(1)); // Simulate uninstallation time
    Console::println("Package '" + package + "' uninstalled successfully.");
}

void InstalatorTool::run() {
    Console::titlebar("Instalator");
    Console::println("Welcome to the Instalator Tool. Type 'help' for commands.");
    std::string input;
    while (true) {
        Console::print("> ");
        std::getline(std::cin, input);
        if (input == "exit") {
            break;
        } else if (input == "help") {
            Console::println("Available commands:");
            Console::println("- install <package>: Install a package");
            Console::println("- uninstall <package>: Uninstall a package");
            Console::println("- list: List all installed packages");
            Console::println("- exit: Exit the tool");
        } else if (input.rfind("install ", 0) == 0) {
            std::string package = input.substr(8);
            install(package);
        } else if (input.rfind("uninstall ", 0) == 0) {
            std::string package = input.substr(10);
            uninstall(package);
        } else if (input == "list") {
            listInstalled();
        } else {
            Console::errormsg("UNKNOWN_COMMAND", "Unknown command. Type 'help' for available commands.");
        }
    }
}
