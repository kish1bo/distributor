#include "kernel.h"
#include "console.h"
#include "filesystem.h"
#include "sysctl.h"
#include "instalator_tool.h"
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;
Kernel::Kernel() : currentUser(nullptr), systemState(SystemState::LOGGED_OUT) {
    users = {
        {"roman", hashPassword("1234"), true},
        {"natalia", hashPassword("4321"), false},
        {"guest", hashPassword("guest"), false}
    };

    initCommands();
}
FileSystem filesys;
Sysctl sysctl;
InstalatorTool instalator;

std::string Kernel::hashPassword(const std::string& password) {
    return std::to_string(std::hash<std::string>{}(password));
}

void Kernel::boot() {
    Console::titlebar("");
    initCommands();
}

void Kernel::initCommands() {
    commands["login"] = [this](const Command& cmd) { cmdLogin(cmd); };
    commands["logout"] = [this](const Command& cmd) { cmdLogout(cmd); };
    commands["help"] = [this](const Command& cmd) { cmdHelp(cmd); };
    commands["whoiam"] = [this](const Command& cmd) { cmdWhoiam(cmd); };
    commands["where"] = [this](const Command&) { filesys.pwd(); };
    commands["ls"] = [this](const Command&) { filesys.ls(); };
    commands["mkdir"] = [this](const Command& c) { filesys.mkdir(c.value); };
    commands["cd"] = [this](const Command& c) { filesys.cd(c.value); };
    commands["systemctl", "network"] = [this](const Command& c) {sysctl.netmngr();};
    commands["systemctl", "services"] = [this](const Command& c) {sysctl.services();};
    commands["instal"] = [this](const Command& c) {InstalatorTool tool; tool.run();};

    // Add more commands here as needed
}

Command Kernel::parseCommand(const std::string& input) {
    Command cmd;
    std::stringstream ss(input);
    std::vector<std::string> tokens;
    std::string temp;

    while (ss >> temp)
        tokens.push_back(temp);

    if (tokens.size() > 0) cmd.def = tokens[0];
    if (tokens.size() > 1) cmd.value = tokens[1];
    if (tokens.size() > 2) cmd.parameter = tokens[2];

    return cmd;
}

void Kernel::run() {
    std::string input;
    while (true) {
        if (systemState == SystemState::LOGGED_OUT) {
            std::cout << "Login required. Type 'login <username> <password>' or 'exit' to quit.\n";
        }

        std::string prompt = (systemState == SystemState::LOGGED_OUT) ? "jojo> " : 
                           (systemState == SystemState::ADMIN) ? "\033[32mjojo(admin)>\033[0m " : 
                           "\033[32mjojo>\033[0m ";
        std::cout << prompt;
        std::getline(std::cin, input);

        if (input == "exit") break;

        Command cmd = parseCommand(input);
        handleCommand(cmd);
    }
}

bool Kernel::login(const std::string& username, const std::string& password) {
    std::string hashed = hashPassword(password);

    for (auto& user : users) {
        if (user.username == username && user.passwordHash == hashed) {
            currentUser = &user;
            systemState = user.isAdmin ? SystemState::ADMIN : SystemState::USER;
            Console::clear();
            std::string message = "Login successful. Welcome, " + username + "!";
            Console::colortxt(message, "green");
            return true;
        }
    }
    Console::errormsg("Invalid username or password.");
    return false;
}

void Kernel::handleCommand(const Command& cmd) {
    // Special handling for login when logged out
    if (systemState == SystemState::LOGGED_OUT && cmd.def != "login") {
        Console::errormsg("Please login first.");
        return;
    }

    auto it = commands.find(cmd.def);
    if (it != commands.end()) {
        it->second(cmd);
    } else {
        Console::errormsg("Unknown command. Type 'help' for available commands.");
    }
}

void Kernel::cmdLogin(const Command& cmd) {
    if (systemState != SystemState::LOGGED_OUT) {
        Console::errormsg("Already logged in. Use 'logout' first.");
        return;
    }

    if (cmd.value.empty() || cmd.parameter.empty()) {
        Console::errormsg("Usage: login <username> <password>");
        return;
    }

    login(cmd.value, cmd.parameter);
}

void Kernel::cmdLogout(const Command& cmd) {
    if (systemState == SystemState::LOGGED_OUT) {
        Console::errormsg("Not logged in.");
        return;
    }

    currentUser = nullptr;
    systemState = SystemState::LOGGED_OUT;
    Console::println("Logged out successfully.");
}

void Kernel::cmdHelp(const Command& cmd) {
    Console::println("Available commands:");
    Console::println("  login <username> <password> - Login to the system");
    Console::println("  logout - Logout from the system");
    Console::println("  help - Show this help message");
    Console::println("  whoiam - Show who's logged in");
    Console::println("  where - Show current directory");
    Console::println("  cd <directory> - Change directory");
    Console::println("  mkdir <dirname> - Create directory");
    Console::println("  ls [directory] - List directory contents");
    // Add more help entries here as you add commands
}

void Kernel::cmdWhoiam(const Command& cmd) {
    if (systemState == SystemState::USER){
        Console::print("Now logged in: ");
        Console::colortxt(currentUser->username, "cyan");
        return;
    }

    else if(systemState == SystemState::ADMIN) {
        Console::print("Now logged in: ");
        Console::colortxt(currentUser->username, "blue");
        return;
    }

    else {
        Console::errormsg("You're not logged in.");
        return; 
    };
}

