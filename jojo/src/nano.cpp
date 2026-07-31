#include "nano.h"

#include <conio.h>
#include <iostream>
#include <sstream>
#include <vector>

namespace {
    void draw(const std::string& path, const std::vector<std::string>& lines,
        size_t row, size_t column, bool modified, const std::string& status) {
        std::cout << "\033[2J\033[H\033[1;36mJOJO nano - " << path
            << (modified ? " [modified]" : "") << "\033[0m\n";

        constexpr size_t visibleLines = 20;
        size_t firstLine = row >= visibleLines ? row - visibleLines + 1 : 0;
        for (size_t index = 0; index < visibleLines; ++index) {
            size_t lineIndex = firstLine + index;
            if (lineIndex < lines.size()) {
                std::cout << "\033[33m" << (lineIndex + 1) << "\033[0m " << lines[lineIndex];
            }
            std::cout << "\033[K\n";
        }

        std::cout << "\033[1;30m" << (status.empty() ? "Ctrl+O Save  |  Ctrl+X Exit  |  Ctrl+G Help" : status)
            << "\033[0m\033[K\n";
        std::cout << "\033[" << (row - firstLine + 2) << ";" << (column + 3) << "H";
        std::cout.flush();
    }

    char exitChoice() {
        std::cout << "\033[22;1H\033[KSave changes before exit? (y/n/c) ";
        std::cout.flush();
        int key = _getch();
        if (key == 'y' || key == 'Y') return 'y';
        if (key == 'n' || key == 'N') return 'n';
        return 'c';
    }
}

bool NanoEditor::edit(const std::string& path, const LoadFile& loadFile, const SaveFile& saveFile) {
    std::string content;
    std::string error;
    if (!loadFile(path, content, error)) {
        std::cout << error << "\n";
        return false;
    }

    std::vector<std::string> lines;
    std::stringstream input(content);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    if (lines.empty()) lines.emplace_back();

    size_t row = 0;
    size_t column = 0;
    bool modified = false;
    std::string status;
    bool running = true;

    while (running) {
        if (row >= lines.size()) row = lines.size() - 1;
        if (column > lines[row].size()) column = lines[row].size();
        draw(path, lines, row, column, modified, status);
        status.clear();

        int key = _getch();
        if (key == 0 || key == 224) {
            key = _getch();
            if (key == 72 && row > 0) --row;
            else if (key == 80 && row + 1 < lines.size()) ++row;
            else if (key == 75 && column > 0) --column;
            else if (key == 77 && column < lines[row].size()) ++column;
            else if (key == 71) column = 0;
            else if (key == 79) column = lines[row].size();
            else if (key == 83 && column < lines[row].size()) {
                lines[row].erase(column, 1);
                modified = true;
            }
            continue;
        }

        if (key == 15) {
            std::string output;
            for (const auto& currentLine : lines) output += currentLine + "\n";
            if (saveFile(path, output, error)) {
                modified = false;
                status = "Saved";
            } else status = error;
        } else if (key == 24) {
            char choice = modified ? exitChoice() : 'y';
            if (choice != 'c') {
                if (modified && choice == 'y') {
                    std::string output;
                    for (const auto& currentLine : lines) output += currentLine + "\n";
                    if (!saveFile(path, output, error)) {
                        status = error;
                        continue;
                    }
                }
                running = false;
            }
        } else if (key == 7) {
            status = "Ctrl+O Save  |  Ctrl+X Exit  |  Arrows move  |  Enter new line";
        } else if (key == 8) {
            if (column > 0) {
                lines[row].erase(--column, 1);
                modified = true;
            } else if (row > 0) {
                column = lines[row - 1].size();
                lines[row - 1] += lines[row];
                lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(row));
                --row;
                modified = true;
            }
        } else if (key == 13) {
            std::string remainder = lines[row].substr(column);
            lines[row].erase(column);
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(row + 1), remainder);
            ++row;
            column = 0;
            modified = true;
        } else if (key >= 32 && key <= 126) {
            lines[row].insert(column++, 1, static_cast<char>(key));
            modified = true;
        }
    }

    std::cout << "\033[2J\033[H";
    return true;
}