#include "filesystem.h"
#include <iostream>

FileSystem::FileSystem() {
    root = fs::absolute("distributor/virtualFS");
    current = root;

    fs::create_directories(root);
}

void FileSystem::pwd() const {
    std::cout << "/" << fs::relative(current, root).string() << '\n';
}

void FileSystem::ls() const {
    for (const auto& entry : fs::directory_iterator(current)) {
        std::cout << entry.path().filename().string() << '\n';
    }
}

void FileSystem::mkdir(const std::string& name) {
    fs::create_directory(current / name);
}

bool FileSystem::isInsideRoot(const fs::path& path) const {
    std::string pathStr = fs::weakly_canonical(path).string();
    std::string rootStr = root.string();
    return pathStr.compare(0, rootStr.length(), rootStr) == 0;
}

void FileSystem::cd(const std::string& path) {
    fs::path target;

    if (path == "/")
        target = root;
    else
        target = fs::weakly_canonical(current / path);

    if (!fs::exists(target) || !fs::is_directory(target)) {
        std::cout << "cd: no such directory\n";
        return;
    }

    if (!isInsideRoot(target)) {
        std::cout << "cd: access denied\n";
        return;
    }

    current = target;
}
