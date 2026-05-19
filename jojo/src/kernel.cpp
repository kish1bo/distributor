#define UNICODE
#define _UNICODE
#include "kernel.h"
#include "console.h"
#include "filesystem.h"
#include "sysctl.h"
#include "instalator_tool.h"
#include <fstream>
#include <functional>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include <tlhelp32.h>

namespace {
    constexpr const char* kProgramVersion = "0.3.2";

    std::string formatClock(long long seconds) {
        long long h = seconds / 3600;
        long long m = (seconds % 3600) / 60;
        long long s = seconds % 60;

        std::ostringstream out;
        out << std::setw(2) << std::setfill('0') << h << ":"
            << std::setw(2) << std::setfill('0') << m << ":"
            << std::setw(2) << std::setfill('0') << s;
        return out.str();
    }

    class SystemUptime {
    private:
        time_t programStart;
        time_t loginTime;
        bool loggedIn;

    public:
        SystemUptime()
            : programStart(time(nullptr)), loginTime(0), loggedIn(false) {}

        void onLogin() {
            loginTime = time(nullptr);
            loggedIn = true;
        }

        void onLogout() {
            loginTime = 0;
            loggedIn = false;
        }

        std::string systemUptime() const {
            long long s = static_cast<long long>(difftime(time(nullptr), programStart));
            return formatClock(s);
        }

        std::string sessionUptime() const {
            if (!loggedIn) return "00:00:00";
            long long s = static_cast<long long>(difftime(time(nullptr), loginTime));
            return formatClock(s);
        }
    
        time_t currentTime() const {
            return time(nullptr);
        }
    };

    namespace logs{
        void log(const std::string& message) {
            std::ofstream logFile("log/history.log", std::ios::app);
            if (logFile.is_open()) {
                time_t now = time(nullptr);
                char timeBuffer[20];
                strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", localtime(&now));
                logFile << "[" << timeBuffer << "] " << message << "\n";
            }
        }
    }

    std::string toLower(std::string input) {
        std::transform(input.begin(), input.end(), input.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return input;
    }

    bool endsWith(const std::string& value, const std::string& suffix) {
        if (suffix.size() > value.size()) {
            return false;
        }
        return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin());
    }
}

// Global pointer to allow tools (installer) to interact with the running kernel
Kernel* g_kernel = nullptr;

    ULONGLONG fileTimeToULL(const FILETIME& ft) {
        ULARGE_INTEGER value;
        value.LowPart = ft.dwLowDateTime;
        value.HighPart = ft.dwHighDateTime;
        return value.QuadPart;
    }

    bool isProcessNameMatch(const std::string& inputName, const std::string& exeName) {
        std::string input = toLower(inputName);
        std::string exe = toLower(exeName);
        if (input == exe) {
            return true;
        }
        if (!endsWith(input, ".exe") && (input + ".exe") == exe) {
            return true;
        }
        return false;
    }

    bool queryProcessUptimeSeconds(const std::string& processName,
        long long& seconds,
        int& matchCount,
        std::string& matchedExe,
        std::string& error) {
        matchCount = 0;
        matchedExe.clear();

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            error = "snapshot_failed";
            return false;
        }

        PROCESSENTRY32 entry;
        entry.dwSize = sizeof(entry);

        ULONGLONG oldestCreate = 0;
        int readableMatches = 0;

        if (Process32First(snapshot, &entry)) {
            do {
                std::wstring exe = entry.szExeFile;
                char exeBuffer[260];
                WideCharToMultiByte(CP_ACP, 0, exe.c_str(), -1, exeBuffer, sizeof(exeBuffer), NULL, NULL);
                std::string exeName = exeBuffer;
                if (!isProcessNameMatch(processName, exeName)) {
                    continue;
                }

                matchCount++;
                HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
                if (!proc) {
                    continue;
                }

                FILETIME create;
                FILETIME exit;
                FILETIME kernel;
                FILETIME user;
                if (GetProcessTimes(proc, &create, &exit, &kernel, &user)) {
                    ULONGLONG createTicks = fileTimeToULL(create);
                    if (oldestCreate == 0 || createTicks < oldestCreate) {
                        oldestCreate = createTicks;
                        matchedExe = exeName;
                    }
                    readableMatches++;
                }
                CloseHandle(proc);
            } while (Process32Next(snapshot, &entry));
        }

        CloseHandle(snapshot);

        if (matchCount == 0) {
            error = "not_found";
            return false;
        }
        if (readableMatches == 0 || oldestCreate == 0) {
            error = "access_denied";
            return false;
        }

        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        ULONGLONG nowTicks = fileTimeToULL(now);
        seconds = static_cast<long long>((nowTicks - oldestCreate) / 10000000ULL);
        return true;
    }

    std::string cpuArchName(WORD arch) {
        switch (arch) {
        case PROCESSOR_ARCHITECTURE_AMD64:
            return "x64";
        case PROCESSOR_ARCHITECTURE_INTEL:
            return "x86";
        case PROCESSOR_ARCHITECTURE_ARM64:
            return "ARM64";
        default:
            return "unknown";
        }
    }

    double bytesToGiB(ULONGLONG bytes) {
        return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    }

    std::string detectWindowsVersion() {
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll) {
            auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
                GetProcAddress(ntdll, "RtlGetVersion"));
            if (rtlGetVersion) {
                OSVERSIONINFOW osInfo{};
                osInfo.dwOSVersionInfoSize = sizeof(osInfo);
                if (rtlGetVersion(&osInfo) == 0) {
                    std::ostringstream out;
                    out << "Windows "
                        << osInfo.dwMajorVersion << "."
                        << osInfo.dwMinorVersion
                        << " (build " << osInfo.dwBuildNumber << ")";
                    return out.str();
                }
            }
        }

        OSVERSIONINFOEXA fallback{};
        fallback.dwOSVersionInfoSize = sizeof(fallback);
        if (GetVersionExA(reinterpret_cast<OSVERSIONINFOA*>(&fallback))) {
            std::ostringstream out;
            out << "Windows "
                << fallback.dwMajorVersion << "."
                << fallback.dwMinorVersion
                << " (build " << fallback.dwBuildNumber << ")";
            return out.str();
        }

        return "Windows";
    }

Kernel::Kernel() : currentUser(nullptr), systemState(SystemState::LOGGED_OUT) {
    // attempt to load persisted users; fall back to defaults
    loadUsers();
    if (users.empty()) {
        users = {
            {"roman", hashPassword("1234"), true, false},
            {"natalia", hashPassword("4321"), false, false},
            {"guest", hashPassword("guest"), false, false}
        };
    }

    fileSystem = std::make_unique<FileSystem>(this);
    initCommands();
}

Kernel::~Kernel() = default;
Sysctl sysctl;
InstalatorTool instl;
SystemUptime uptime;

std::string Kernel::hashPassword(const std::string& password) {
    return std::to_string(std::hash<std::string>{}(password));
}

void Kernel::boot() {
    Console::titlebar("");
    if (fileSystem) {
        fileSystem->syncToPermissions();
    }
}

void Kernel::initCommands() {
    commands.clear();
    // core commands
    addCommand("login", [this](const Command& cmd) { cmdLogin(cmd); }, SystemState::LOGGED_OUT);
    addCommand("logout", [this](const Command& cmd) { cmdLogout(cmd); }, SystemState::GUEST);
    addCommand("help", [this](const Command& cmd) { cmdHelp(cmd); }, SystemState::LOGGED_OUT);
    addCommand("whoiam", [this](const Command& cmd) { cmdWhoiam(cmd); }, SystemState::GUEST);
    addCommand("read", [this](const Command& cmd) { cmdRead(cmd); }, SystemState::GUEST);
    addCommand("cat", [this](const Command& cmd) { cmdRead(cmd); }, SystemState::GUEST);
    addCommand("write", [this](const Command& cmd) { cmdWrite(cmd); }, SystemState::GUEST);
    addCommand("append", [this](const Command& cmd) { cmdAppend(cmd); }, SystemState::GUEST);
    addCommand("root", [this](const Command& cmd) { cmdRoot(cmd); }, SystemState::ADMIN);
    addCommand("ps", [this](const Command& cmd) { cmdProcesses(cmd); }, SystemState::GUEST);
    addCommand("processes", [this](const Command& cmd) { cmdProcesses(cmd); }, SystemState::GUEST);
    addCommand("time", [this](const Command& cmd) { timeof(cmd); }, SystemState::GUEST);
    addCommand("jojo", [this](const Command& cmd) { cmdJojo(cmd); }, SystemState::LOGGED_OUT);
    addCommand("procctl", [this](const Command& cmd) { cmdProcctl(cmd); }, SystemState::GUEST, true);

    // let modules register their own commands
    if (fileSystem) fileSystem->registerCommands(this);
    // sysctl and instalator are global module objects
    sysctl.sysctl_init();
    instl.listAvailable(); // no-op call to ensure linkage (modules will register below)
    // actual module registration
    instl.registerCommands(this);
    sysctl.registerCommands(this);
}

Command Kernel::parseCommand(const std::string& input) {
    Command cmd;
    std::vector<std::string> tokens;
    std::string current;
    bool inQuotes = false;
    char quoteChar = 0;
    bool escaped = false;

    for (char ch : input) {
        if (escaped) {
            current.push_back(ch);
            escaped = false;
            continue;
        }

        if (ch == '\\') {
            escaped = true;
            continue;
        }

        if (inQuotes) {
            if (ch == quoteChar) {
                inQuotes = false;
                quoteChar = 0;
                tokens.push_back(current);
                current.clear();
            } else {
                current.push_back(ch);
            }
            continue;
        }

        if (ch == '"' || ch == '\'') {
            inQuotes = true;
            quoteChar = ch;
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }

        current.push_back(ch);
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    if (!tokens.empty()) {
        cmd.name = toLower(tokens[0]);
        cmd.args.assign(tokens.begin() + 1, tokens.end());
    }

    return cmd;
}

bool Kernel::userExists(const std::string& login) const { //check if user with given name exists
    return std::any_of(users.begin(), users.end(),
        [&](const User& u) { return u.username == login; });
}

void Kernel::run() { //main loop
    std::string input;
    while (true) {
        std::cout << Console::buildPrompt("");
        std::getline(std::cin, input);

        if (input == "exit") break;
        if (input.empty()) continue;

        Command cmd = parseCommand(input);
        handleCommand(cmd);
    }
}

bool Kernel::login(const std::string& username, const std::string& password) { //attempt to login with given credentials
    std::string hashed = hashPassword(password);

    for (auto& user : users) {
        if (user.username == username && user.passwordHash == hashed) {
            currentUser = &user;
            if (user.isAdmin) {
                systemState = SystemState::ADMIN;
            } else if (toLower(user.username) == "guest") {
                systemState = SystemState::GUEST;
                logs::log("User '" + currentUsername() + "' logged in as guest.");
            } else {
                systemState = SystemState::USER;
            }

            if (fileSystem) {
                fileSystem->syncToPermissions();
            }

            Console::clear();
            Console::titlebar("");
            Console::colortxt("Login successful. Welcome, ", "green");
            if (systemState == SystemState::ADMIN) {
                Console::colortxt(username, "red");
            } else if (isRootUser()) {
                Console::colortxt(username, "yellow");
            } else if (systemState == SystemState::GUEST) {
                Console::colortxt(username, "cyan");
            } else {
                Console::colortxt(username, "green");
            }
            if (isRootUser()) {
                Console::print(" (root)");
            }
            Console::println("");
            uptime.onLogin();
            return true;
        }
    }

    Console::errormsg("NO_MEMBER_FOUND", "Invalid username or password.");
    return false;
}

void Kernel::handleCommand(const Command& cmd) { //handle incoming commands
    if (cmd.name.empty()) {
        return;
    }
    auto it = commands.find(cmd.name);
    if (it == commands.end()) {
        Console::errormsg("UNKNOWN_COMMAND", "Type 'help' for available commands.");
        return;
    }

    const CommandEntry& entry = it->second;
    // check root requirement
    if (entry.requireRoot && !canAccessRootArea()) {
        Console::errormsg("ACCES_DENIED", "Please login with a user with root/admin rights.");
        return;
    }
    // check minimum state
    if (static_cast<int>(systemState) < static_cast<int>(entry.minState)) {
        Console::errormsg("MISSING_ACTION", "Please login first.");
        return;
    }

    entry.handler(cmd);
}

void Kernel::addCommand(const std::string& name, std::function<void(const Command&)> handler,
                       SystemState minState, bool requireRoot) {
    CommandEntry e{handler, minState, requireRoot};
    commands[name] = std::move(e);
}

void Kernel::cmdLogin(const Command& cmd) { //handle login command
    if (systemState != SystemState::LOGGED_OUT) {
        Console::errormsg("MISSING_ACTION", "Use 'logout' first.");
        return;
    }

    if (cmd.args.size() < 2) {
        Console::errormsg("", "Usage: login <username> <password>");
        return;
    }

    login(cmd.args[0], cmd.args[1]);
    logs::log("User '" + currentUsername() + "' logged in.");
}

void Kernel::cmdLogout(const Command& cmd) { //handle logout command
    (void)cmd;
    if (systemState == SystemState::LOGGED_OUT) {
        Console::errormsg("MISSING_ACTION", "Not logged in.");
        return;
    }

    currentUser = nullptr;
    systemState = SystemState::LOGGED_OUT;
    uptime.onLogout();
    if (fileSystem) {
        fileSystem->syncToPermissions();
    }

    Console::clear();
    Console::titlebar("");
    Console::println("Logged out successfully.");
}

void Kernel::cmdHelp(const Command& cmd) {
    Console::println("Available commands:");
    Console::println("  login <username> <password> - Login to the system");
    Console::println("  logout - Logout from the system");
    Console::println("  help [-e] - Show this help message");
    Console::println("  whoiam - Show who is logged in");
    Console::println("  root grant|revoke <username> - Manage root rights (admin only)");
    Console::println("  root list - Show users with root rights");
    Console::println("  where [.|..|/] - Show current directory");
    Console::println("  cd <directory> - Change directory");
    Console::println("  ls - List directory contents");
    Console::println("  mkdir <name> - Create directory");
    Console::println("  mkfile <name> [--txt|--json|--csv|--xml|--md|--log] - Create file");
    Console::println("  mktxt|mkjson|mkcsv|mkxml|mkmd <name> - Quick file creation");
    Console::println("  read <file> - Read file content");
    Console::println("  write <file> <text...> - Overwrite file");
    Console::println("  append <file> <text...> - Append to file");
    Console::println("  rm <file> - Remove file");
    Console::println("  rmdir <directory> - Remove directory recursively");
    Console::println("  ps - List running processes (duplicates shown as xN)");
    Console::println("  time sys - Show system + session uptime");
    Console::println("  time <process> --process - Show process uptime");
    Console::println("  jojo --version - Show GUI-style system page");
    Console::println("  instalator - Open installer tool");
    Console::println("  install <package> - Install package from repository index");
    Console::println("  systemctl <command> [args] - Manage services");
    Console::println("  exit - Exit the terminal");

    if (!cmd.args.empty() && cmd.args[0] == "-e") {
        Console::println("MISSING_ACTION - Missing required action");
        Console::println("NO_MEMBER_FOUND - No user found with given name");
        Console::println("ROOT: admin only - Command requires admin privileges");
        Console::println("UNKNOWN_COMMAND - Command not recognized");
        Console::println("ACCES_DENIED - You don't have permission to do this");
        Console::println("INVALID_TARGET - The specified target cannot be used for action");
        Console::println("ALREADY_EXISTS - The specified item already exists");
    }
}

void Kernel::cmdWhoiam(const Command& cmd) { //show current user and role
    (void)cmd;
    if (systemState == SystemState::LOGGED_OUT || currentUser == nullptr) {
        Console::errormsg("MISSING_ACTION", "You're not logged in.");
        return;
    }

    Console::print("Now logged in: ");
    if (systemState == SystemState::ADMIN) {
        Console::colortxt(currentUser->username, "red");
    } else if (isRootUser()) {
        Console::colortxt(currentUser->username, "yellow");
    } else if (systemState == SystemState::GUEST) {
        Console::colortxt(currentUser->username, "cyan");
    } else {
        Console::colortxt(currentUser->username, "green");
    }
    Console::print(" (");
    Console::print(currentRoleName());
    Console::println(")");
}

void Kernel::cmdRoot(const Command& cmd) { //grant or revoke root rights to user, or list users with root rights (admin only)
    if (systemState != SystemState::ADMIN) {
        Console::errormsg("ACCES_DENIED", "ROOT: admin only");
        return;
    }

    if (cmd.args.empty()) {
        Console::errormsg("MISSING_ACTION", "Usage: root grant|revoke <username> | root list");
        return;
    }

    std::string action = toLower(cmd.args[0]);
    if (action == "list") {
        Console::println("Users with root rights:");
        bool hasAny = false;
        for (const auto& user : users) {
            if (user.hasRoot && !user.isAdmin) {
                Console::println("  " + user.username);
                hasAny = true;
            }
        }
        if (!hasAny) {
            Console::println("  (none)");
        }
        return;
    }

    if (cmd.args.size() < 2) {
        Console::errormsg("MISSING_ACTION", "Usage: root grant|revoke <username>");
        return;
    }

    User* target = findUser(cmd.args[1]);
    if (!target) {
        Console::errormsg("NO_MEMBER_FOUND", "ROOT: user not found");
        return;
    }
    if (target->isAdmin) {
        Console::errormsg("INVALID_TARGET", "ROOT: admin already has full access");
        return;
    }
    if (toLower(target->username) == "guest") {
        Console::errormsg("INVALID_TARGET", "ROOT: guest cannot receive root");
        return;
    }

    if (action == "grant") {
        if (target->hasRoot) {
            Console::errormsg("ALREADY_EXISTS", "ROOT: user already has root");
            return;
        }
        target->hasRoot = true;
        Console::println("ROOT: access granted to " + target->username);
        return;
    }

    if (action == "revoke") {
        if (!target->hasRoot) {
            Console::errormsg("MISSING_ACTION", "ROOT: user does not have root");
            return;
        }
        target->hasRoot = false;
        Console::println("ROOT: access revoked from " + target->username);
        return;
    }

    Console::errormsg("MISSING_ACTION", "Usage: root grant|revoke <username> | root list");
}

void Kernel::cmdRead(const Command& cmd) { //read and cat do the same thing
    if (cmd.args.empty()) {
        Console::errormsg("MISSING_ACTION", "Usage: read <file>");
        return;
    }
    fileSystem->readFile(cmd.args[0]);
}

void Kernel::cmdWrite(const Command& cmd) { //overwrite file
    if (cmd.args.size() < 2) {
        Console::errormsg("MISSING_ACTION", "Usage: write <file> <text...");
        return;
    }

    std::string text = commandTail(cmd, 1);
    if (text.empty()) {
        Console::errormsg("MISSING_ACTION", "Usage: write <file> <text...");
        return;
    }

    fileSystem->writeFile(cmd.args[0], text, false);
}

void Kernel::cmdAppend(const Command& cmd) { //append to file
    if (cmd.args.size() < 2) {
        Console::errormsg("MISSING_ACTION", "Usage: append <file> <text...");
        return;
    }

    std::string text = commandTail(cmd, 1);
    if (text.empty()) {
        Console::errormsg("MISSING_ACTION", "Usage: append <file> <text...");
        return;
    }

    fileSystem->writeFile(cmd.args[0], text, true);
}

void Kernel::cmdProcesses(const Command& cmd) { //list running processes, showing duplicates as xN
    (void)cmd;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot == INVALID_HANDLE_VALUE) {
        Console::errormsg("PROCESS_ERROR", "Failed to query process list.");
        return;
    }

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(PROCESSENTRY32W);

    std::map<std::string, int> counts;
    std::unordered_map<std::string, std::string> displayName;

    if (Process32FirstW(snapshot, &entry)) {

        do {
            std::wstring exe = entry.szExeFile;

            char exeBuffer[260];

            WideCharToMultiByte(
                CP_UTF8,
                0,
                exe.c_str(),
                -1,
                exeBuffer,
                sizeof(exeBuffer),
                NULL,
                NULL
            );

            std::string name = exeBuffer;

            std::string key = toLower(name);

            counts[key]++;

            if (displayName.find(key) == displayName.end()) {
                displayName[key] = name;
            }

        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);

    if (counts.empty()) {
        Console::println("No running processes found.");
        return;
    }

    for (const auto& item : counts) {

        const std::string& key = item.first;
        int count = item.second;

        std::cout << displayName[key];

        if (count > 1) {
            std::cout << " x" << count;
        }

        std::cout << "\n";
    }
}

void Kernel::cmdJojo(const Command& cmd) {
    if (!cmd.args.empty() && cmd.args[0] == "--version") {
        printVersionPage();
        return;
    }

    Console::errormsg("MISSING_ACTION", "Usage: jojo --version");
}

void Kernel::timeof(const Command& cmd) { //time sys shows system uptime and session uptime, time <process> --process shows process uptime
    if (!cmd.args.empty() && cmd.args.back() == "--process") {
        if (cmd.args.size() < 2) {
            Console::errormsg("MISSING_ACTION", "Usage: time <process> --process");
            return;
        }

        std::string processName = cmd.args[0];
        long long seconds = 0;
        int matches = 0;
        std::string exeName;
        std::string error;
        if (!queryProcessUptimeSeconds(processName, seconds, matches, exeName, error)) {
            if (error == "not_found") {
                Console::errormsg("NO_PROCESS", "Process not found.");
            } else if (error == "access_denied") {
                Console::errormsg("ACCES_DENIED", "Access denied for process info.");
            } else {
                Console::errormsg("PROCESS_ERROR", "Failed to query process list.");
            }
            return;
        }

        Console::print("process ");
        Console::print(exeName);
        Console::print(" uptime: ");
        std::cout << formatClock(seconds) << "\n";
        if (matches > 1) {
            Console::println("Note: multiple processes matched; showing longest running.");
        }
        return;
    }

    if (!cmd.args.empty() && cmd.args[0] == "sys") {
        Console::print("from start: ");
        std::cout << uptime.systemUptime() << "\n";
        if (systemState == SystemState::LOGGED_OUT) {
            Console::println("no active session");
            return;
        }
        Console::print("session: ");
        std::cout << uptime.sessionUptime() << "\n";
        return;
    }

    Console::errormsg("MISSING_ACTION", "Usage: time sys | time <process> --process");
}

void Kernel::printVersionPage() const { //print version information
    char computerNameBuffer[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD computerNameSize = MAX_COMPUTERNAME_LENGTH + 1;
    std::string computerName = "unknown";
    if (GetComputerNameA(computerNameBuffer, &computerNameSize)) {
        computerName.assign(computerNameBuffer, computerNameSize);
    }

    SYSTEM_INFO systemInfo{};
    GetNativeSystemInfo(&systemInfo);

    MEMORYSTATUSEX memoryStatus{};
    memoryStatus.dwLength = sizeof(memoryStatus);
    bool hasMemoryInfo = GlobalMemoryStatusEx(&memoryStatus) != 0;

    std::string osVersion = detectWindowsVersion();

    std::ostringstream ramOut;
    if (hasMemoryInfo) {
        ramOut << std::fixed << std::setprecision(2) << bytesToGiB(memoryStatus.ullTotalPhys) << " GB";
    } else {
        ramOut << "unknown";
    }

    std::string userInfo = "not logged in";
    if (currentUser) {
        userInfo = currentUser->username + " (" + currentRoleName() + ")";
    }

    Console::clear();
    Console::println("");
    Console::println("            \033[48;2;8;20;64m\033[97m   *        *       *        *        \033[0m");
    Console::println("            \033[48;2;12;34;92m\033[97m      *         *         *           \033[0m");
    Console::println("            \033[48;2;20;52;130m      \033[97m*   \033[48;2;255;221;87m      \033[48;2;20;52;130m      \033[97m*      \033[0m");
    Console::println("            \033[48;2;24;72;154m    \033[97m*  \033[48;2;255;221;87m          \033[48;2;24;72;154m    \033[97m*    \033[0m");
    Console::println("            \033[48;2;26;89;170m   \033[48;2;255;221;87m          \033[48;2;26;89;170m   \033[48;2;255;221;87m  \033[48;2;26;89;170m   \033[97m*    \033[0m");
    Console::println("            \033[48;2;28;106;188m   \033[48;2;255;221;87m          \033[48;2;28;106;188m    \033[48;2;255;221;87m \033[48;2;28;106;188m    \033[97m *  \033[0m");
    Console::println("            \033[48;2;40;132;204m    \033[48;2;255;221;87m        \033[48;2;40;132;204m    \033[48;2;255;221;87m \033[48;2;40;132;204m    \033[97m*   \033[0m");
    Console::println("            \033[48;2;58;162;218m      \033[48;2;255;221;87m      \033[48;2;58;162;218m      \033[97m*     *\033[0m");
    Console::println("");
    Console::println("\033[1mProgram\033[0m");
    Console::println(std::string("  Version          : v") + kProgramVersion);
    Console::println("  Runtime mode     : normal");
    Console::println("");
    Console::println("\033[1mComputer\033[0m");
    Console::println("  Host name        : " + computerName);
    Console::println("  OS               : " + osVersion);
    Console::println("  CPU arch         : " + cpuArchName(systemInfo.wProcessorArchitecture));
    Console::println("  Logical cores    : " + std::to_string(systemInfo.dwNumberOfProcessors));
    Console::println("  RAM              : " + ramOut.str());
    Console::println("  Color palette    : \033[48;2;255;85;85m  \033[0m \033[48;2;255;165;0m  \033[0m \033[48;2;255;221;87m  \033[0m \033[48;2;90;200;250m  \033[0m \033[48;2;114;137;218m  \033[0m \033[48;2;186;85;211m  \033[0m \033[48;2;255;255;255m  \033[0m");
    Console::println("");
    Console::println("\033[1mSession\033[0m");
    Console::println("  Current user     : " + userInfo);
    Console::println("  System uptime    : " + uptime.systemUptime());
    if (systemState == SystemState::LOGGED_OUT) {
        Console::println("  Logged session uptime   : none");
    } else {
        Console::println("  Logged session uptime   : " + uptime.sessionUptime());
    }
    Console::println("");
    Console::println("Use 'help' to see available commands.");
}

User* Kernel::findUser(const std::string& username) { //find user by name and return pointer to it, or nullptr if not found
    for (auto& user : users) {
        if (user.username == username) {
            return &user;
        }
    }
    return nullptr;
}

std::string Kernel::commandTail(const Command& cmd, size_t start) const { //get the part of the command after the first N tokens, used for write and append text
    if (cmd.args.size() <= start) {
        return "";
    }

    std::ostringstream out;
    for (size_t i = start; i < cmd.args.size(); ++i) {
        if (i > start) {
            out << " ";
        }
        out << cmd.args[i];
    }
    return out.str();
}

bool Kernel::isRootUser() const { //check if current user has root rights (but is not admin, since admin has separate full access)
    return currentUser != nullptr && currentUser->hasRoot && !currentUser->isAdmin;
}

void Kernel::addUser(const std::string& username, const std::string& password, bool isAdmin) {
    if (userExists(username)) {
        return;
    }
    User u;
    u.username = username;
    u.passwordHash = hashPassword(password);
    u.isAdmin = isAdmin;
    u.hasRoot = false;
    users.push_back(u);
    saveUsers();
}

void Kernel::loadUsers() {
    users.clear();
    try {
        std::filesystem::path p = std::filesystem::current_path() / "var" / "users.txt";
        std::ifstream in(p);
        if (!in) return;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            // format: username:passwordHash:isAdmin:hasRoot
            std::vector<std::string> parts;
            std::istringstream ss(line);
            std::string tok;
            while (std::getline(ss, tok, ':')) parts.push_back(tok);
            if (parts.size() >= 2) {
                User u;
                u.username = parts[0];
                u.passwordHash = parts[1];
                u.isAdmin = (parts.size() > 2 && parts[2] == "1");
                u.hasRoot = (parts.size() > 3 && parts[3] == "1");
                users.push_back(u);
            }
        }
    } catch (...) {
        // ignore
    }
}

void Kernel::saveUsers() {
    try {
        std::filesystem::path dir = std::filesystem::current_path() / "var";
        std::filesystem::create_directories(dir);
        std::filesystem::path p = dir / "users.txt";
        std::ofstream out(p, std::ios::trunc);
        if (!out) return;
        for (const auto& u : users) {
            out << u.username << ":" << u.passwordHash << ":" << (u.isAdmin ? "1" : "0") << ":" << (u.hasRoot ? "1" : "0") << "\n";
        }
    } catch (...) {
        // ignore
    }
}

void Kernel::cmdProcctl(const Command& cmd) {
    // check if package enabled flag exists
    std::filesystem::path flag = std::filesystem::current_path() / "var" / "process_control_enabled";
    if (!std::filesystem::exists(flag)) {
        Console::errormsg("ACCES_DENIED", "Process control package not installed.");
        return;
    }

    if (cmd.args.empty()) {
        Console::println("Usage: procctl start <exe-path> | procctl kill <pid>");
        return;
    }

    std::string action = cmd.args[0];
    if (action == "start") {
        if (cmd.args.size() < 2) {
            Console::errormsg("MISSING_ACTION", "Usage: procctl start <exe-path>");
            return;
        }
        std::string exe = cmd.args[1];
        STARTUPINFOA si{};
        PROCESS_INFORMATION pi{};
        si.cb = sizeof(si);
        BOOL ok = CreateProcessA(nullptr, const_cast<char*>(exe.c_str()), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
        if (!ok) {
            Console::errormsg("PROCESS_ERROR", "Failed to start process.");
            return;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        Console::println("Process started: " + exe);
        return;
    }

    if (action == "kill") {
        if (cmd.args.size() < 2) {
            Console::errormsg("MISSING_ACTION", "Usage: procctl kill <pid>");
            return;
        }
        int pid = std::stoi(cmd.args[1]);
        HANDLE proc = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
        if (!proc) {
            Console::errormsg("PROCESS_ERROR", "Failed to open process.");
            return;
        }
        if (!TerminateProcess(proc, 1)) {
            CloseHandle(proc);
            Console::errormsg("PROCESS_ERROR", "Failed to terminate process.");
            return;
        }
        CloseHandle(proc);
        Console::println("Process " + std::to_string(pid) + " terminated.");
        return;
    }

    Console::errormsg("UNKNOWN_COMMAND", "procctl command not recognized.");
}

bool Kernel::canAccessRootArea() const { //check if user can access root area, which is either admin or has root rights
    return systemState == SystemState::ADMIN || isRootUser();
}

std::string Kernel::currentUsername() const { //get current username or empty string if not logged in
    return currentUser ? currentUser->username : "";
}

std::string Kernel::currentRoleName() const { //get current role name
    if (systemState == SystemState::ADMIN) return "admin";
    if (isRootUser()) return "root";
    if (systemState == SystemState::GUEST) return "guest";
    if (systemState == SystemState::USER) return "user";
    return "logged_out";
}

std::string Kernel::currentRoleLabel() const { //get current role label for display, e.g. in prompt
    return currentRoleName();
}
