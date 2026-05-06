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
    constexpr const char* kProgramVersion = "0.3.1";

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
                std::string exeName = entry.szExeFile;
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
    users = {
        {"roman", hashPassword("1234"), true, false},
        {"natalia", hashPassword("4321"), false, false},
        {"guest", hashPassword("guest"), false, false}
    };

    fileSystem = std::make_unique<FileSystem>(this);
    initCommands();
}

Kernel::~Kernel() = default;
Sysctl sysctl;
InstalatorTool instl;

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
    commands["login"] = [this](const Command& cmd) { cmdLogin(cmd); };
    commands["logout"] = [this](const Command& cmd) { cmdLogout(cmd); };
    commands["help"] = [this](const Command& cmd) { cmdHelp(cmd); };
    commands["whoiam"] = [this](const Command& cmd) { cmdWhoiam(cmd); };
    commands["where"] = [this](const Command& c) { fileSystem->pwd(c.value); };
    commands["ls"] = [this](const Command&) { fileSystem->ls(); };
    commands["mkdir"] = [this](const Command& c) { fileSystem->mkdir(c.value); };
    commands["mkfile"] = [this](const Command& c) { fileSystem->mkfile(c.value, c.parameter); };
    commands["mktxt"] = [this](const Command& c) { fileSystem->mkfile(c.value, "--txt"); };
    commands["mkjson"] = [this](const Command& c) { fileSystem->mkfile(c.value, "--json"); };
    commands["mkcsv"] = [this](const Command& c) { fileSystem->mkfile(c.value, "--csv"); };
    commands["mkxml"] = [this](const Command& c) { fileSystem->mkfile(c.value, "--xml"); };
    commands["mkmd"] = [this](const Command& c) { fileSystem->mkfile(c.value, "--md"); };
    commands["cd"] = [this](const Command& c) { fileSystem->cd(c.value); };
    commands["rm"] = [this](const Command& c) { fileSystem->rm(c.value); };
    commands["rmdir"] = [this](const Command& c) { fileSystem->rmdir(c.value); };
    commands["read"] = [this](const Command& c) { cmdRead(c); };
    commands["cat"] = [this](const Command& c) { cmdRead(c); };
    commands["write"] = [this](const Command& c) { cmdWrite(c); };
    commands["append"] = [this](const Command& c) { cmdAppend(c); };
    commands["root"] = [this](const Command& c) { cmdRoot(c); };
    commands["ps"] = [this](const Command& c) { cmdProcesses(c); };
    commands["processes"] = [this](const Command& c) { cmdProcesses(c); };
    commands["time"] = [this](const Command& c) { timeof(c); };
    commands["jojo"] = [this](const Command& c) { cmdJojo(c); };
    commands["systemctl", "net"] = [this](const Command& c) { sysctl.netmngr(c.parameter, c.extra); };
    commands["systemctl", "services"] = [this](const Command& c) { sysctl.services(c.parameter); };
    commands["install"] = [this](const Command& c) { instl.install(c.value, c.parameter); };
    commands["instalator"] = [this](const Command& c) { instl.run(); };
}

Command Kernel::parseCommand(const std::string& input) {
    Command cmd;
    std::stringstream ss(input);
    std::vector<std::string> tokens;
    std::string temp;

    while (ss >> temp) {
        tokens.push_back(temp);
    }

    if (!tokens.empty()) cmd.def = tokens[0];
    if (tokens.size() > 1) cmd.value = tokens[1];
    if (tokens.size() > 2) cmd.parameter = tokens[2];
    if (tokens.size() > 3) {
        std::ostringstream tail;
        for (size_t i = 3; i < tokens.size(); ++i) {
            if (i > 3) {
                tail << " ";
            }
            tail << tokens[i];
        }
        cmd.extra = tail.str();
    }

    return cmd;
}

bool Kernel::userExists(const std::string& login) const { //check if user with given name exists
    return std::any_of(users.begin(), users.end(),
        [&](const User& u) { return u.username == login; });
}

void Kernel::run() {
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

bool Kernel::login(const std::string& username, const std::string& password) {
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

void Kernel::handleCommand(const Command& cmd) { 
    if (cmd.def.empty()) {
        return;
    }

    if (systemState == SystemState::LOGGED_OUT &&
        cmd.def != "login" &&
        cmd.def != "help" &&
        cmd.def != "jojo") {
        Console::errormsg("MISSING_ACTION", "Please login first.");
        return;
    }

    auto it = commands.find(cmd.def);
    if (it != commands.end()) {
        it->second(cmd);
    } else {
        Console::errormsg("UNKNOWN_COMMAND", "Type 'help' for available commands.");
    }
}

void Kernel::cmdLogin(const Command& cmd) {
    if (systemState != SystemState::LOGGED_OUT) {
        Console::errormsg("MISSING_ACTION", "Use 'logout' first.");
        return;
    }

    if (cmd.value.empty() || cmd.parameter.empty()) {
        Console::errormsg("", "Usage: login <username> <password>");
        return;
    }

    login(cmd.value, cmd.parameter);
    logs::log("User '" + currentUsername() + "' logged in.");
}

void Kernel::cmdLogout(const Command& cmd) {
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
    (void)cmd;
    Console::println("Available commands:");
    Console::println("  login <username> <password> - Login to the system");
    Console::println("  logout - Logout from the system");
    Console::println("  help - Show this help message");
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
    Console::println("  exit - Exit the terminal");
    if(cmd.parameter == "-e") {
        Console::println("MISSING_ACTION - Miss of necessary action");
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

    std::string action = toLower(cmd.value);
    if (action.empty()) {
        Console::errormsg("MISSING_ACTION", "Usage: root grant|revoke <username> | root list");
        return;
    }

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

    if (cmd.parameter.empty()) {
        Console::errormsg("MISSING_ACTION", "Usage: root grant|revoke <username>");
        return;
    }

    User* target = findUser(cmd.parameter);
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
    if (cmd.value.empty()) {
        Console::errormsg("MISSING_ACTION", "Usage: read <file>");
        return;
    }
    fileSystem->readFile(cmd.value);
}

void Kernel::cmdWrite(const Command& cmd) {//overwrite file
    if (cmd.value.empty()) {
        Console::errormsg("MISSING_ACTION", "Usage: write <file> <text...>");
        return;
    }

    std::string text = commandTail(cmd);
    if (text.empty()) {
        Console::errormsg("MISSING_ACTION", "Usage: write <file> <text...>");
        return;
    }

    fileSystem->writeFile(cmd.value, text, false);
}

void Kernel::cmdAppend(const Command& cmd) { //append to file
    if (cmd.value.empty()) {
        Console::errormsg("MISSING_ACTION", "Usage: append <file> <text...>");
        return;
    }

    std::string text = commandTail(cmd);
    if (text.empty()) {
        Console::errormsg("MISSING_ACTION", "Usage: append <file> <text...>");
        return;
    }

    fileSystem->writeFile(cmd.value, text, true);
}

void Kernel::cmdProcesses(const Command& cmd) { //show process list with duplicates counted as xN, e.g.
    (void)cmd;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        Console::errormsg("PROCESS_ERROR", "Failed to query process list.");
        return;
    }

    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(entry);

    std::map<std::string, int> counts;
    std::unordered_map<std::string, std::string> displayName;

    if (Process32First(snapshot, &entry)) {
        do {
            std::string name = entry.szExeFile;
            std::string key = toLower(name);
            counts[key]++;
            if (displayName.find(key) == displayName.end()) {
                displayName[key] = name;
            }
        } while (Process32Next(snapshot, &entry));
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
    if (cmd.value == "--version") {
        printVersionPage();
        return;
    }

    Console::errormsg("MISSING_ACTION", "Usage: jojo --version");
}

void Kernel::timeof(const Command& cmd) {
    if (cmd.parameter == "--process") {
        if (cmd.value.empty()) {
            Console::errormsg("MISSING_ACTION", "Usage: time <process> --process");
            return;
        }

        long long seconds = 0;
        int matches = 0;
        std::string exeName;
        std::string error;
        if (!queryProcessUptimeSeconds(cmd.value, seconds, matches, exeName, error)) {
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

    if (cmd.value == "sys") {
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

void Kernel::printVersionPage() const {
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

User* Kernel::findUser(const std::string& username) {
    for (auto& user : users) {
        if (user.username == username) {
            return &user;
        }
    }
    return nullptr;
}

std::string Kernel::commandTail(const Command& cmd) const {
    if (cmd.parameter.empty()) {
        return "";
    }

    if (cmd.extra.empty()) {
        return cmd.parameter;
    }

    return cmd.parameter + " " + cmd.extra;
}

bool Kernel::isRootUser() const {
    return currentUser != nullptr && currentUser->hasRoot && !currentUser->isAdmin;
}

bool Kernel::canAccessRootArea() const {
    return systemState == SystemState::ADMIN || isRootUser();
}

std::string Kernel::currentUsername() const {
    return currentUser ? currentUser->username : "";
}

std::string Kernel::currentRoleName() const {
    if (systemState == SystemState::ADMIN) return "admin";
    if (isRootUser()) return "root";
    if (systemState == SystemState::GUEST) return "guest";
    if (systemState == SystemState::USER) return "user";
    return "logged_out";
}

std::string Kernel::currentRoleLabel() const {
    return currentRoleName();
}
