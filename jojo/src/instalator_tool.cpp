#include "instalator_tool.h"
#include "console.h"
#include "kernel.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <algorithm>
#include <map>
#include <sstream>
#include <cctype>
#include <functional>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

struct PackageInfo {
    std::string name;
    std::string version;
    std::string url;
    std::string description;
    std::string entry;
};

static std::string trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        start++;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        end--;
    }
    return value.substr(start, end - start);
}

static bool parseJsonString(const std::string& content, size_t& pos, std::string& output) {
    output.clear();
    while (pos < content.size() && std::isspace(static_cast<unsigned char>(content[pos]))) {
        pos++;
    }
    if (pos >= content.size() || content[pos] != '"') {
        return false;
    }
    pos++;
    while (pos < content.size()) {
        char ch = content[pos++];
        if (ch == '\\') {
            if (pos >= content.size()) {
                return false;
            }
            output.push_back(content[pos++]);
            continue;
        }
        if (ch == '"') {
            return true;
        }
        output.push_back(ch);
    }
    return false;
}

static bool skipWhitespace(const std::string& content, size_t& pos) {
    while (pos < content.size() && std::isspace(static_cast<unsigned char>(content[pos]))) {
        pos++;
    }
    return pos < content.size();
}

static bool parseJsonObject(const std::string& content, size_t& pos, std::map<std::string, std::string>& object) {
    object.clear();
    if (!skipWhitespace(content, pos) || content[pos] != '{') {
        return false;
    }
    pos++;
    while (true) {
        if (!skipWhitespace(content, pos)) {
            return false;
        }
        if (content[pos] == '}') {
            pos++;
            return true;
        }
        std::string key;
        if (!parseJsonString(content, pos, key)) {
            return false;
        }
        if (!skipWhitespace(content, pos) || content[pos] != ':') {
            return false;
        }
        pos++;
        std::string value;
        if (!parseJsonString(content, pos, value)) {
            return false;
        }
        object[key] = value;
        if (!skipWhitespace(content, pos)) {
            return false;
        }
        if (content[pos] == '}') {
            pos++;
            return true;
        }
        if (content[pos] != ',') {
            return false;
        }
        pos++;
    }
}

static bool parsePackageIndex(const std::string& content, std::vector<PackageInfo>& packages) {
    packages.clear();
    size_t pos = 0;
    if (!skipWhitespace(content, pos) || pos >= content.size() || content[pos] != '[') {
        return false;
    }
    pos++;
    while (true) {
        if (!skipWhitespace(content, pos)) {
            return false;
        }
        if (content[pos] == ']') {
            pos++;
            return true;
        }
        std::map<std::string, std::string> object;
        if (!parseJsonObject(content, pos, object)) {
            return false;
        }
        PackageInfo pkg;
        pkg.name = object["name"];
        pkg.version = object["version"];
        pkg.url = object["url"];
        pkg.description = object["description"];
        pkg.entry = object["entry"];
        packages.push_back(pkg);
        if (!skipWhitespace(content, pos)) {
            return false;
        }
        if (content[pos] == ',') {
            pos++;
            continue;
        }
        if (content[pos] == ']') {
            pos++;
            return true;
        }
        return false;
    }
}

static std::vector<PackageInfo> loadPackageIndex() {
    std::vector<PackageInfo> packages;
    fs::path indexPath = fs::current_path() / "downloads" / "packages.json";
    std::ifstream in(indexPath, std::ios::binary);
    if (!in) {
        return packages;
    }
    std::ostringstream contents;
    contents << in.rdbuf();
    parsePackageIndex(contents.str(), packages);
    return packages;
}

static std::vector<std::string> loadInstalledPackages() {
    std::vector<std::string> installed;
    fs::path installedPath = fs::current_path() / "downloads" / "installed_packages.txt";
    std::ifstream in(installedPath);
    if (!in) {
        return installed;
    }
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (!line.empty()) {
            installed.push_back(line);
        }
    }
    return installed;
}

static bool saveInstalledPackages(const std::vector<std::string>& installed) {
    fs::path installedPath = fs::current_path() / "downloads" / "installed_packages.txt";
    std::ofstream out(installedPath, std::ios::trunc);
    if (!out) {
        return false;
    }
    for (const auto& item : installed) {
        out << item << "\n";
    }
    return true;
}

static bool isAlreadyInstalled(const std::vector<std::string>& installed, const std::string& name) {
    return std::find(installed.begin(), installed.end(), name) != installed.end();
}

static std::string toLower(const std::string& value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

static std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    bool inQuotes = false;
    char quoteChar = 0;
    bool escaped = false;

    for (char ch : line) {
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
    return tokens;
}

static fs::path resolvePackageSource(const std::string& url) {
    if (url.rfind("file://", 0) == 0) {
        std::string pathPart = url.substr(7);
        if (pathPart.rfind("./", 0) == 0 || pathPart.rfind(".\\", 0) == 0) {
            return fs::weakly_canonical(fs::current_path() / pathPart.substr(2));
        }
        return fs::weakly_canonical(pathPart);
    }
    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
        return fs::path();
    }
    return fs::weakly_canonical(fs::current_path() / url);
}

static bool downloadFile(const std::string& url, const fs::path& targetPath) {
    if (url.rfind("file://", 0) == 0 || (!url.empty() && url.find("://") == std::string::npos)) {
        fs::path source = resolvePackageSource(url);
        if (!fs::exists(source)) {
            return false;
        }
        try {
            fs::create_directories(targetPath.parent_path());
            fs::copy_file(source, targetPath, fs::copy_options::overwrite_existing);
            return true;
        } catch (...) {
            return false;
        }
    }

#ifdef _WIN32
    using URLDownloadToFileAFunc = HRESULT(WINAPI*)(LPUNKNOWN, LPCSTR, LPCSTR, DWORD, void*);
    HMODULE hUrlmon = LoadLibraryA("urlmon.dll");
    if (!hUrlmon) {
        return false;
    }
    auto dllFunc = reinterpret_cast<URLDownloadToFileAFunc>(GetProcAddress(hUrlmon, "URLDownloadToFileA"));
    if (!dllFunc) {
        FreeLibrary(hUrlmon);
        return false;
    }
    HRESULT hr = dllFunc(nullptr, url.c_str(), targetPath.string().c_str(), 0, nullptr);
    FreeLibrary(hUrlmon);
    return SUCCEEDED(hr);
#else
    (void)url;
    (void)targetPath;
    return false;
#endif
}

static std::string filenameFromUrl(const std::string& url, const std::string& name, const std::string& version) {
    fs::path candidate;
    if (url.rfind("file://", 0) == 0) {
        candidate = resolvePackageSource(url);
    } else if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
        size_t pos = url.find_last_of("/");
        if (pos != std::string::npos && pos + 1 < url.size()) {
            candidate = url.substr(pos + 1);
        }
    } else {
        candidate = resolvePackageSource(url);
    }
    std::string base = name + "-" + version;
    if (!candidate.empty() && candidate.has_extension()) {
        base += candidate.extension().string();
    } else {
        base += ".pkg";
    }
    return base;
}

void InstalatorTool::listAvailable() const {
    auto packages = loadPackageIndex();
    auto installed = loadInstalledPackages();
    Console::println("Available packages:");
    if (packages.empty()) {
        Console::println("  No package repository found. Create downloads/packages.json.");
        return;
    }
    for (const auto& pkg : packages) {
        Console::print("  " + pkg.name + " @ " + pkg.version + " - ");
        Console::print(pkg.description.empty() ? "No description." : pkg.description);
        if (isAlreadyInstalled(installed, pkg.name)) {
            Console::print(" [installed]");
        }
        Console::println("");
    }
}

void InstalatorTool::listInstalled() const {
    auto installed = loadInstalledPackages();
    Console::println("Installed packages:");
    if (installed.empty()) {
        Console::println("  (none)");
        return;
    }
    for (const auto& pkg : installed) {
        Console::println("  " + pkg);
    }
}

void InstalatorTool::search(const std::string& filter) const {
    auto packages = loadPackageIndex();
    if (packages.empty()) {
        Console::println("No package repository available.");
        return;
    }
    std::string query = toLower(filter);
    bool found = false;
    for (const auto& pkg : packages) {
        if (toLower(pkg.name).find(query) != std::string::npos || toLower(pkg.description).find(query) != std::string::npos) {
            Console::println("  " + pkg.name + " @ " + pkg.version + " - " + pkg.description);
            found = true;
        }
    }
    if (!found) {
        Console::println("No packages matched '" + filter + "'.");
    }
}

void InstalatorTool::showInfo(const std::string& package) const {
    auto packages = loadPackageIndex();
    for (const auto& pkg : packages) {
        if (pkg.name == package) {
            Console::println("Name: " + pkg.name);
            Console::println("Version: " + pkg.version);
            Console::println("URL: " + pkg.url);
            Console::println("Description: " + pkg.description);
            if (!pkg.entry.empty()) {
                Console::println("Entry: " + pkg.entry);
            }
            return;
        }
    }
    Console::errormsg("NO_PACKAGE", "Package '" + package + "' not found in repository.");
}

void InstalatorTool::install(const std::vector<std::string>& args) {
    if (args.empty()) {
        Console::println("Usage: install <package>");
        return;
    }

    std::string packageName = args[0];
    auto packages = loadPackageIndex();
    auto installed = loadInstalledPackages();
    if (isAlreadyInstalled(installed, packageName)) {
        Console::println("Package '" + packageName + "' is already installed.");
        return;
    }

    const PackageInfo* selected = nullptr;
    for (const auto& pkg : packages) {
        if (pkg.name == packageName) {
            selected = &pkg;
            break;
        }
    }

    if (!selected) {
        Console::errormsg("NO_PACKAGE", "Package '" + packageName + "' not found in repository.");
        return;
    }

    Console::println("Preparing installation of '" + selected->name + "'...");
    fs::path packageDir = fs::current_path() / "downloads" / "package_repo";
    fs::create_directories(packageDir);
    std::string filename = filenameFromUrl(selected->url, selected->name, selected->version);
    fs::path destination = packageDir / filename;

    Console::println("Downloading package from " + selected->url + "...");
    if (!downloadFile(selected->url, destination)) {
        Console::errormsg("DOWNLOAD_FAILED", "Failed to download package from " + selected->url);
        return;
    }

    // inspect package for simple manifest instructions
    try {
        std::ifstream pkgIn(destination);
        if (pkgIn) {
            std::string line;
            while (std::getline(pkgIn, line)) {
                auto toks = tokenize(line);
                if (toks.empty()) continue;
                if (toks[0] == "create_user") {
                    if (toks.size() >= 3) {
                        std::string username = toks[1];
                        std::string password = toks[2];
                        bool isAdmin = (toks.size() > 3 && (toks[3] == "1" || toLower(toks[3]) == "true"));
                        if (g_kernel) {
                            g_kernel->addUser(username, password, isAdmin);
                        } else {
                            fs::path usersPath = fs::current_path() / "var" / "users.txt";
                            fs::create_directories(usersPath.parent_path());
                            std::ofstream out(usersPath, std::ios::app);
                            if (out) {
                                out << username << ":" << std::to_string(std::hash<std::string>{}(password)) << ":" << (isAdmin ? "1" : "0") << ":0\n";
                            }
                        }
                    }
                    continue;
                }

                if (toks[0] == "enable_process_control") {
                    fs::path flag = fs::current_path() / "var" / "process_control_enabled";
                    fs::create_directories(flag.parent_path());
                    std::ofstream flagFile(flag.string());
                    continue;
                }
            }
        }
    } catch (...) {
        // ignore manifest errors
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
    installed.push_back(selected->name);
    if (!saveInstalledPackages(installed)) {
        Console::errormsg("IO_ERROR", "Failed to record installed package metadata.");
        return;
    }

    Console::println("Package '" + selected->name + "' installed to " + destination.string() + ".");
}

void InstalatorTool::uninstall(const std::string& package) {
    if (package.empty()) {
        Console::println("Usage: uninstall <package>");
        return;
    }

    auto installed = loadInstalledPackages();
    if (!isAlreadyInstalled(installed, package)) {
        Console::errormsg("NO_PACKAGE", "Package '" + package + "' is not installed.");
        return;
    }

    installed.erase(std::remove(installed.begin(), installed.end(), package), installed.end());
    if (!saveInstalledPackages(installed)) {
        Console::errormsg("IO_ERROR", "Failed to update installed packages list.");
        return;
    }

    Console::println("Package '" + package + "' has been uninstalled.");
}

void InstalatorTool::run() {
    Console::titlebar("Instalator");
    Console::println("Welcome to the Instalator Tool. Type 'help' for commands.");
    std::string input;
    while (true) {
        Console::print("instalator> ");
        std::getline(std::cin, input);
        if (input.empty()) {
            continue;
        }

        auto tokens = tokenize(input);
        if (tokens.empty()) {
            continue;
        }

        std::string command = toLower(tokens[0]);
        if (command == "exit") {
            break;
        }
        if (command == "help") {
            Console::println("Available commands:");
            Console::println("  list - List available repository packages");
            Console::println("  list installed - Show installed packages");
            Console::println("  search <term> - Search available packages");
            Console::println("  info <package> - Show package details");
            Console::println("  install <package> - Install a package");
            Console::println("  uninstall <package> - Remove an installed package");
            Console::println("  exit - Exit the installer");
            continue;
        }
        if (command == "list") {
            if (tokens.size() > 1 && toLower(tokens[1]) == "installed") {
                listInstalled();
            } else {
                listAvailable();
            }
            continue;
        }
        if (command == "search") {
            if (tokens.size() < 2) {
                Console::println("Usage: search <term>");
                continue;
            }
            search(tokens[1]);
            continue;
        }
        if (command == "info") {
            if (tokens.size() < 2) {
                Console::println("Usage: info <package>");
                continue;
            }
            showInfo(tokens[1]);
            continue;
        }
        if (command == "install") {
            install(std::vector<std::string>(tokens.begin() + 1, tokens.end()));
            continue;
        }
        if (command == "uninstall") {
            if (tokens.size() < 2) {
                Console::println("Usage: uninstall <package>");
                continue;
            }
            uninstall(tokens[1]);
            continue;
        }

        Console::errormsg("UNKNOWN_COMMAND", "Type 'help' for available commands.");
    }
}

void InstalatorTool::registerCommands(Kernel* kernel) {
    if (!kernel) return;
    kernel->addCommand("instalator", [this](const Command&){ this->run(); }, SystemState::GUEST);
    kernel->addCommand("install", [this](const Command& cmd){ this->install(cmd.args); }, SystemState::GUEST);
    kernel->addCommand("uninstall", [this](const Command& cmd){ if (cmd.args.empty()) { Console::println("Usage: uninstall <package>"); return; } this->uninstall(cmd.args[0]); }, SystemState::GUEST);
    kernel->addCommand("list", [this](const Command& cmd){ if (!cmd.args.empty() && toLower(cmd.args[0]) == "installed") { this->listInstalled(); } else { this->listAvailable(); } }, SystemState::GUEST);
    kernel->addCommand("search", [this](const Command& cmd){ if (cmd.args.empty()) { Console::println("Usage: search <term>"); return; } this->search(cmd.args[0]); }, SystemState::GUEST);
    kernel->addCommand("info", [this](const Command& cmd){ if (cmd.args.empty()) { Console::println("Usage: info <package>"); return; } this->showInfo(cmd.args[0]); }, SystemState::GUEST);
}
