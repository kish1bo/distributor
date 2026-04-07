#pragma once

#include <unordered_map>
#include <functional>
#include <string>
#include <vector>

struct User {
    std::string username;
    std::string passwordHash;
    bool isAdmin;
};

struct Command {
    std::string def;
    std::string value;
    std::string parameter;
};

enum class SystemState {
    LOGGED_OUT,
    USER,
    ADMIN
};

class Kernel {
private:
    std::unordered_map<std::string, std::function<void(const Command&)>> commands;
    std::vector<User> users;
    User* currentUser;
    SystemState systemState;

    // Command handlers
    void cmdLogin(const Command& cmd);
    void cmdLogout(const Command& cmd);
    void cmdHelp(const Command& cmd);
    void cmdWhoiam(const Command& cmd);
    void cmdWhere(const Command& cmd);

    // Utility functions
    std::string hashPassword(const std::string& password);
    bool login(const std::string& username, const std::string& password);
    Command parseCommand(const std::string& input);

public:
    Kernel();
    void boot();
    void run();
    void initCommands();
    void handleCommand(const Command& cmd);
};
