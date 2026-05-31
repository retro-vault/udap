// util.cpp
// Common utilities for parsing SDCC compiler output files.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <fstream>
#include <algorithm>

#include <sdcc/util.h>

namespace sdcc::util {

std::optional<std::vector<std::string>> read_lines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream file(path);
    if (!file.is_open())
        return std::nullopt;
    std::string line;
    while (std::getline(file, line))
        lines.push_back(line);
    return lines;
}

std::string_view trim(std::string_view str) {
    auto first = str.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    auto last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

// Split str by delimiter.  Returns ALL tokens, including empty ones, so
// callers can index the result by field position without offset errors.
std::vector<std::string_view> split(std::string_view str, char delim) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (start <= str.size()) {
        auto end = str.find(delim, start);
        if (end == std::string_view::npos) end = str.size();
        parts.push_back(str.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

std::optional<std::vector<std::string>> match(std::string_view line,
                                               const std::regex& pattern) {
    std::cmatch m;
    if (!std::regex_match(line.begin(), line.end(), m, pattern))
        return std::nullopt;
    std::vector<std::string> groups;
    groups.reserve(m.size());
    for (size_t i = 1; i < m.size(); ++i)
        groups.emplace_back(m[i].str());
    return groups;
}

} // namespace sdcc::util
