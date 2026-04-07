#pragma once
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

class FileSystem {
public:
    FileSystem();

    void pwd() const;
    void ls() const;
    void mkdir(const std::string& name);
    void cd(const std::string& path);

private:
    fs::path root;
    fs::path current;

    bool isInsideRoot(const fs::path& path) const;
};
