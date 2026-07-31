#include "filesystem.h"
#include "console.h"
#include "kernel.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
    std::string toLowerCopy(std::string input) {
        std::transform(input.begin(), input.end(), input.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return input;
    }
}

FileSystem::FileSystem() {
    kernel = nullptr;
    root = detectRoot();
    cage = root / "jojo";

    fs::create_directories(cage);
}

FileSystem::FileSystem(Kernel* kernel) : FileSystem() {
    this->kernel = kernel;
    syncToPermissions();
}

fs::path FileSystem::detectRoot() const {
    fs::path cwd = fs::current_path();
    if (fs::exists(cwd / "src") && fs::exists(cwd / "build.bat")) {
        return fs::weakly_canonical(cwd);
    }
    if (fs::exists(cwd / "jojo" / "src") && fs::exists(cwd / "jojo" / "build.bat")) {
        return fs::weakly_canonical(cwd / "jojo");
    }
    return fs::weakly_canonical(cwd);
}

bool FileSystem::canAccessRootArea() const {
    return kernel && kernel->canAccessRootArea();
}

bool FileSystem::isLoggedIn() const {
    return kernel && kernel->getSystemState() != SystemState::LOGGED_OUT;
}

fs::path FileSystem::activeBoundary() const {
    return canAccessRootArea() ? root : cage;
}

bool FileSystem::isInsideBoundary(const fs::path& path, const fs::path& boundary) const {
    std::string pathStr = toLowerCopy(fs::weakly_canonical(path).lexically_normal().string());
    std::string boundaryStr = toLowerCopy(fs::weakly_canonical(boundary).lexically_normal().string());

    if (pathStr == boundaryStr) {
        return true;
    }

    if (pathStr.size() <= boundaryStr.size()) {
        return false;
    }

    if (pathStr.compare(0, boundaryStr.size(), boundaryStr) != 0) {
        return false;
    }

    char next = pathStr[boundaryStr.size()];
    return next == '\\' || next == '/';
}

fs::path FileSystem::resolvePath(const std::string& input) const {
    fs::path boundary = activeBoundary();
    if (input.empty() || input == ".") {
        return fs::weakly_canonical(current);
    }

    if (input == "/") {
        return fs::weakly_canonical(boundary);
    }

    fs::path raw(input);
    fs::path candidate;
    if (!input.empty() && (input[0] == '/' || input[0] == '\\')) {
        candidate = boundary / raw.relative_path();
    } else if (raw.is_absolute()) {
        candidate = raw;
    } else {
        candidate = current / raw;
    }
    return fs::weakly_canonical(candidate);
}

std::string FileSystem::extensionFromFormat(const std::string& format) const {
    if (format == "--txt") return ".txt";
    if (format == "--json") return ".json";
    if (format == "--csv") return ".csv";
    if (format == "--xml") return ".xml";
    if (format == "--md") return ".md";
    if (format == "--log") return ".log";
    return "";
}

std::string FileSystem::defaultContentForExtension(const std::string& extension) const {
    if (extension == ".json") {
        return "{\n  \"key\": \"value\"\n}\n";
    }
    if (extension == ".csv") {
        return "column1,column2\nvalue1,value2\n";
    }
    if (extension == ".xml") {
        return "<root>\n  <item>value</item>\n</root>\n";
    }
    return "";
}

void FileSystem::syncToPermissions() {
    fs::path boundary = activeBoundary();
    if (!fs::exists(current) || !fs::is_directory(current) || !isInsideBoundary(current, boundary)) {
        current = boundary;
    }
}

void FileSystem::registerCommands(Kernel* kernel) {
    if (!kernel) return;
    // register filesystem related commands; require logged-in user
    kernel->addCommand("where", [this](const Command& cmd) {
        std::string path = cmd.args.empty() ? "." : cmd.args[0];
        this->pwd(path);
    }, SystemState::GUEST);

    kernel->addCommand("ls", [this](const Command&){ this->ls(); }, SystemState::GUEST);
    kernel->addCommand("mkdir", [this](const Command& cmd){ this->mkdir(cmd.args.empty() ? "" : cmd.args[0]); }, SystemState::GUEST);
    kernel->addCommand("mkfile", [this](const Command& cmd){ std::string name = cmd.args.empty() ? "" : cmd.args[0]; std::string fmt = cmd.args.size() > 1 ? cmd.args[1] : ""; this->mkfile(name, fmt); }, SystemState::GUEST);
    kernel->addCommand("mktxt", [this](const Command& cmd){ this->mkfile(cmd.args.empty() ? "" : cmd.args[0], "--txt"); }, SystemState::GUEST);
    kernel->addCommand("mkjson", [this](const Command& cmd){ this->mkfile(cmd.args.empty() ? "" : cmd.args[0], "--json"); }, SystemState::GUEST);
    kernel->addCommand("mkcsv", [this](const Command& cmd){ this->mkfile(cmd.args.empty() ? "" : cmd.args[0], "--csv"); }, SystemState::GUEST);
    kernel->addCommand("mkxml", [this](const Command& cmd){ this->mkfile(cmd.args.empty() ? "" : cmd.args[0], "--xml"); }, SystemState::GUEST);
    kernel->addCommand("mkmd", [this](const Command& cmd){ this->mkfile(cmd.args.empty() ? "" : cmd.args[0], "--md"); }, SystemState::GUEST);
    kernel->addCommand("cd", [this](const Command& cmd){ this->cd(cmd.args.empty() ? "/" : cmd.args[0]); }, SystemState::GUEST);
    kernel->addCommand("rm", [this](const Command& cmd){ this->rm(cmd.args.empty() ? "" : cmd.args[0]); }, SystemState::GUEST);
    kernel->addCommand("rmdir", [this](const Command& cmd){ this->rmdir(cmd.args.empty() ? "" : cmd.args[0]); }, SystemState::GUEST);
    kernel->addCommand("read", [this](const Command& cmd){ this->readFile(cmd.args.empty() ? "" : cmd.args[0]); }, SystemState::GUEST);
    kernel->addCommand("write", [this](const Command& cmd){ if (cmd.args.size() < 2) { Console::errormsg("MISSING_ACTION","WRITE: file text required"); return; } std::string text; for (size_t i=1;i<cmd.args.size();++i){ if (i>1) text += " "; text += cmd.args[i]; } this->writeFile(cmd.args[0], text, false); }, SystemState::GUEST);
    kernel->addCommand("append", [this](const Command& cmd){ if (cmd.args.size() < 2) { Console::errormsg("MISSING_ACTION","APPEND: file text required"); return; } std::string text; for (size_t i=1;i<cmd.args.size();++i){ if (i>1) text += " "; text += cmd.args[i]; } this->writeFile(cmd.args[0], text, true); }, SystemState::GUEST);
}

void FileSystem::pwd(const std::string& path) {
    if (!isLoggedIn()) {
        Console::errormsg("MISSING_ACTION", "PWD: access denied");
        return;
    }
    syncToPermissions();

    fs::path boundary = activeBoundary();
    fs::path visible = current;
    if (path == "..") {
        visible = fs::weakly_canonical(current.parent_path());
        if (!isInsideBoundary(visible, boundary)) {
            visible = boundary;
        }
    } else if (path == "/") {
        visible = boundary;
    }

    std::string relative = fs::relative(visible, boundary).string();
    if (relative.empty() || relative == ".") {
        std::cout << "/\n";
    } else {
        std::cout << "/" << relative << "\n";
    }
}

void FileSystem::ls() {
    if (!isLoggedIn()) {
        Console::errormsg("MISSING_ACTION", "LS: access denied");
        return;
    }
    syncToPermissions();

    std::vector<std::string> entries;
    for (const auto& entry : fs::directory_iterator(current)) {
        entries.push_back(entry.path().filename().string());
    }
    std::sort(entries.begin(), entries.end());

    if (entries.empty()) {
        std::cout << "(empty)\n";
        return;
    }

    for (const auto& name : entries) {
        std::cout << name << "\n";
    }
}

void FileSystem::mkdir(const std::string& name) {
    if (!isLoggedIn()) {
        Console::errormsg("ACCES_DENIED", "MKDIR: access denied");
        return;
    }
    if (name.empty()) {
        Console::errormsg("MISSING_ACTION", "MKDIR: name required");
        return;
    }
    syncToPermissions();

    fs::path target = resolvePath(name);
    if (!isInsideBoundary(target, activeBoundary())) {
        Console::errormsg("ACCES_DENIED", "MKDIR: access denied");
        return;
    }
    if (fs::exists(target)) {
        Console::errormsg("ALREADY_EXISTS", "MKDIR: target already exists");
        return;
    }

    if (!fs::create_directory(target)) {
        Console::errormsg("IO_ERROR", "MKDIR: failed to create directory");
    }
}

void FileSystem::mkfile(const std::string& name, const std::string& format) {
    if (!isLoggedIn()) {
        Console::errormsg("ACCES_DENIED", "MKFILE: access denied");
        return;
    }
    if (name.empty()) {
        Console::errormsg("MISSING_ACTION", "MKFILE: name required");
        return;
    }
    syncToPermissions();

    fs::path fileName = fs::path(name);
    if (!format.empty()) {
        std::string extension = extensionFromFormat(format);
        if (extension.empty()) {
            Console::errormsg("INVALID_FORMAT", "MKFILE: use --txt|--json|--csv|--xml|--md|--log");
            return;
        }

        if (fileName.has_extension()) {
            if (toLowerCopy(fileName.extension().string()) != extension) {
                Console::errormsg("INVALID_FORMAT", "MKFILE: extension does not match format");
                return;
            }
        } else {
            fileName += extension;
        }
    } else if (!fileName.has_extension()) {
        fileName += ".txt";
    }

    fs::path targetPath = resolvePath(fileName.string());
    if (!isInsideBoundary(targetPath, activeBoundary())) {
        Console::errormsg("ACCES_DENIED", "MKFILE: access denied");
        return;
    }
    if (fs::exists(targetPath)) {
        Console::errormsg("ALREADY_EXISTS", "MKFILE: file already exists");
        return;
    }

    std::ofstream out(targetPath, std::ios::out | std::ios::trunc);
    if (!out) {
        Console::errormsg("IO_ERROR", "MKFILE: could not create file");
        return;
    }

    std::string defaultContent = defaultContentForExtension(
        toLowerCopy(targetPath.extension().string()));
    if (!defaultContent.empty()) {
        out << defaultContent;
    }
}

void FileSystem::cd(const std::string& path) {
    if (!isLoggedIn()) {
        Console::errormsg("ACCES_DENIED", "CD: access denied");
        return;
    }
    syncToPermissions();

    fs::path target = resolvePath(path.empty() ? "/" : path);
    if (!fs::exists(target) || !fs::is_directory(target)) {
        Console::errormsg("NO_DIRECTORY", "CD: no such file or directory");
        return;
    }
    if (!isInsideBoundary(target, activeBoundary())) {
        Console::errormsg("ACCES_DENIED", "CD: access denied");
        return;
    }

    current = target;
}

void FileSystem::rm(const std::string& target) {
    if (!isLoggedIn()) {
        Console::errormsg("ACCES_DENIED", "RM: access denied");
        return;
    }
    if (target.empty()) {
        Console::errormsg("MISSING_ACTION", "RM: target required");
        return;
    }
    syncToPermissions();

    fs::path targetPath = resolvePath(target);
    if (!isInsideBoundary(targetPath, activeBoundary())) {
        Console::errormsg("ACCES_DENIED", "RM: access denied");
        return;
    }
    if (!fs::exists(targetPath)) {
        Console::errormsg("NO_FILE", "RM: file not found");
        return;
    }
    if (fs::is_directory(targetPath)) {
        Console::errormsg("INVALID_TARGET", "RM: target is a directory, use rmdir");
        return;
    }

    if (!fs::remove(targetPath)) {
        Console::errormsg("IO_ERROR", "RM: remove failed");
    }
}

void FileSystem::rmdir(const std::string& dir) {
    if (!isLoggedIn()) {
        Console::errormsg("ACCES_DENIED", "RMDIR: access denied");
        return;
    }
    if (dir.empty()) {
        Console::errormsg("MISSING_ACTION", "RMDIR: directory required");
        return;
    }
    syncToPermissions();

    fs::path dirPath = resolvePath(dir);
    if (!isInsideBoundary(dirPath, activeBoundary())) {
        Console::errormsg("ACCES_DENIED", "RMDIR: access denied");
        return;
    }
    if (dirPath == activeBoundary()) {
        Console::errormsg("ACCES_DENIED", "RMDIR: cannot remove access root");
        return;
    }
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
        Console::errormsg("NO_DIRECTORY", "RMDIR: directory not found");
        return;
    }

    if (fs::remove_all(dirPath) == 0) {
        Console::errormsg("IO_ERROR", "RMDIR: remove failed");
    }
}

void FileSystem::readFile(const std::string& target) {
    if (!isLoggedIn()) {
        Console::errormsg("ACCES_DENIED", "READ: access denied");
        return;
    }
    if (target.empty()) {
        Console::errormsg("MISSING_ACTION", "READ: file name required");
        return;
    }
    syncToPermissions();

    fs::path targetPath = resolvePath(target);
    if (!isInsideBoundary(targetPath, activeBoundary())) {
        Console::errormsg("ACCES_DENIED", "READ: access denied");
        return;
    }
    if (!fs::exists(targetPath) || fs::is_directory(targetPath)) {
        Console::errormsg("NO_FILE", "READ: file not found");
        return;
    }

    std::ifstream in(targetPath, std::ios::in);
    if (!in) {
        Console::errormsg("IO_ERROR", "READ: cannot open file");
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        std::cout << line << "\n";
    }
}

void FileSystem::writeFile(const std::string& target, const std::string& content, bool append) {
    if (!isLoggedIn()) {
        Console::errormsg("ACCES_DENIED", "WRITE: access denied");
        return;
    }
    if (target.empty()) {
        Console::errormsg("MISSING_ACTION", "WRITE: file name required");
        return;
    }
    if (content.empty()) {
        Console::errormsg("MISSING_ACTION", "WRITE: text required");
        return;
    }
    syncToPermissions();

    fs::path targetPath = resolvePath(target);
    if (!isInsideBoundary(targetPath, activeBoundary())) {
        Console::errormsg("ACCES_DENIED", "WRITE: access denied");
        return;
    }
    if (fs::exists(targetPath) && fs::is_directory(targetPath)) {
        Console::errormsg("INVALID_TARGET", "WRITE: target is a directory");
        return;
    }

    fs::path parent = targetPath.parent_path();
    if (!parent.empty() && !fs::exists(parent)) {
        Console::errormsg("NO_DIRECTORY", "WRITE: parent directory does not exist");
        return;
    }

    std::ios::openmode mode = std::ios::out;
    mode |= append ? std::ios::app : std::ios::trunc;
    std::ofstream out(targetPath, mode);
    if (!out) {
        Console::errormsg("IO_ERROR", "WRITE: cannot open file");
        return;
    }

    out << content << "\n";
    if (!out.good()) {
        Console::errormsg("IO_ERROR", "WRITE: write failed");
    }
}

bool FileSystem::loadText(const std::string& target, std::string& content, std::string& error) {
    if (!isLoggedIn()) { error = "NANO: access denied"; return false; }
    if (target.empty()) { error = "NANO: file name required"; return false; }
    syncToPermissions();
    fs::path targetPath = resolvePath(target);
    if (!isInsideBoundary(targetPath, activeBoundary())) { error = "NANO: access denied"; return false; }
    if (!fs::exists(targetPath)) { content.clear(); return true; }
    if (fs::is_directory(targetPath)) { error = "NANO: target is a directory"; return false; }
    std::ifstream in(targetPath, std::ios::binary);
    if (!in) { error = "NANO: cannot open file"; return false; }
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool FileSystem::saveText(const std::string& target, const std::string& content, std::string& error) {
    if (!isLoggedIn()) { error = "NANO: access denied"; return false; }
    if (target.empty()) { error = "NANO: file name required"; return false; }
    syncToPermissions();
    fs::path targetPath = resolvePath(target);
    if (!isInsideBoundary(targetPath, activeBoundary())) { error = "NANO: access denied"; return false; }
    if (fs::exists(targetPath) && fs::is_directory(targetPath)) { error = "NANO: target is a directory"; return false; }
    if (!targetPath.parent_path().empty() && !fs::exists(targetPath.parent_path())) {
        error = "NANO: parent directory does not exist";
        return false;
    }
    std::ofstream out(targetPath, std::ios::binary | std::ios::trunc);
    if (!out) { error = "NANO: cannot save file"; return false; }
    out << content;
    if (!out.good()) { error = "NANO: write failed"; return false; }
    return true;
}
