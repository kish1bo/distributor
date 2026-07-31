#pragma once

#include <functional>
#include <string>

class NanoEditor {
public:
    using LoadFile = std::function<bool(const std::string&, std::string&, std::string&)>;
    using SaveFile = std::function<bool(const std::string&, const std::string&, std::string&)>;

    bool edit(const std::string& path, const LoadFile& loadFile, const SaveFile& saveFile);
};