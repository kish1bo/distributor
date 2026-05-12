#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#define VERSION = "0.3.1";

struct User {
    std::string username;
    std::string passwordHash;
    bool isAdmin;
    bool hasRoot;
};

struct Command {
    std::string name;
    std::vector<std::string> args;
};

enum class SystemState {
    LOGGED_OUT,
    GUEST,
    USER,
    ADMIN
};

class FileSystem;

class Kernel {
private:
    std::unordered_map<std::string, std::function<void(const Command&)>> commands;
    std::vector<User> users;
    User* currentUser;
    SystemState systemState;
    std::unique_ptr<FileSystem> fileSystem;

    // Command handlers
    void cmdLogin(const Command& cmd);
    void cmdLogout(const Command& cmd);
    void cmdHelp(const Command& cmd);
    void cmdWhoiam(const Command& cmd);
    void cmdRoot(const Command& cmd);
    void cmdRead(const Command& cmd);
    void cmdWrite(const Command& cmd);
    void cmdAppend(const Command& cmd);
    void cmdProcesses(const Command& cmd);
    void cmdJojo(const Command& cmd);
    void timeof(const Command& cmd);
    void printVersionPage() const;

    // Utility functions
    std::string hashPassword(const std::string& password);
    bool login(const std::string& username, const std::string& password);
    Command parseCommand(const std::string& input);
    User* findUser(const std::string& username);
    std::string commandTail(const Command& cmd, size_t start = 1) const;
    std::string currentRoleName() const;

public:
    Kernel();
    ~Kernel();
    void loadConfig();
    bool userExists(const std::string& login) const;
    void boot();
    void run();
    void initCommands();
    void handleCommand(const Command& cmd);

    bool isRootUser() const;
    bool canAccessRootArea() const;
    std::string currentUsername() const;
    std::string currentRoleLabel() const;
    SystemState getSystemState() const { return systemState; }
};
