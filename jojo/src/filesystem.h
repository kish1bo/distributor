#pragma once
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

class Kernel;

class FileSystem {
public:
    FileSystem();

    explicit FileSystem(Kernel* kernel);

    void pwd(const std::string& path = ".");
    void ls();
    void mkdir(const std::string& name);
    void mkfile(const std::string& name, const std::string& format);
    void cd(const std::string& path);
    void rmdir(const std::string& dir);
    void rm(const std::string& target);
    void readFile(const std::string& target);
    void writeFile(const std::string& target, const std::string& content, bool append);
    void syncToPermissions();

    // register interactive commands into kernel
    void registerCommands(class Kernel* kernel);

private:
    Kernel* kernel;
    fs::path root;
    fs::path cage;
    fs::path current;

    fs::path detectRoot() const;
    bool canAccessRootArea() const;
    bool isLoggedIn() const;
    fs::path activeBoundary() const;
    bool isInsideBoundary(const fs::path& path, const fs::path& boundary) const;
    fs::path resolvePath(const std::string& input) const;
    std::string extensionFromFormat(const std::string& format) const;
    std::string defaultContentForExtension(const std::string& extension) const;
};
