#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cctype>

namespace fs = std::filesystem;

class FolderScanner {
public:
    std::string currentDir;
    std::vector<std::string> fileList;
    int currentIndex = -1;

    static bool isSupportedImageExtension(const std::string& ext) {
        std::string lower = ext;
        for (char& c : lower) c = tolower(c);
        return lower == ".png"  || lower == ".jpg"  || lower == ".jpeg" ||
               lower == ".bmp"  || lower == ".tga"  || lower == ".gif"  ||
               lower == ".psd"  || lower == ".hdr"  || lower == ".pic"  ||
               lower == ".pnm"  || lower == ".ppm"  || lower == ".pgm"  ||
               lower == ".webp" || lower == ".ico";
    }

    static bool naturalSortComparator(const std::string& a, const std::string& b) {
        #ifdef __linux__
        return strverscmp(a.c_str(), b.c_str()) < 0;
        #else
        return a < b;
        #endif
    }

    bool scan(const std::string& targetPath) {
        fileList.clear();
        currentIndex = -1;

        std::string targetFile = "";
        try {
            fs::path p(targetPath);
            if (fs::is_directory(p)) {
                currentDir = fs::canonical(p).string();
            } else if (fs::exists(p)) {
                currentDir = fs::canonical(p.parent_path()).string();
                targetFile = fs::canonical(p).string();
            } else {
                currentDir = fs::current_path().string();
            }

            for (const auto& entry : fs::directory_iterator(currentDir)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    if (isSupportedImageExtension(ext)) {
                        fileList.push_back(entry.path().string());
                    }
                }
            }

            std::sort(fileList.begin(), fileList.end(), naturalSortComparator);

            if (!targetFile.empty()) {
                for (size_t i = 0; i < fileList.size(); ++i) {
                    if (fileList[i] == targetFile) {
                        currentIndex = (int)i;
                        break;
                    }
                }
            }

            if (currentIndex == -1 && !fileList.empty()) {
                currentIndex = 0;
            }

            return !fileList.empty();
        } catch (const std::exception& e) {
            fprintf(stderr, "Folder scan error: %s\n", e.what());
            return false;
        }
    }

    bool hasFiles() const {
        return !fileList.empty() && currentIndex >= 0 && currentIndex < (int)fileList.size();
    }

    std::string currentPath() const {
        if (hasFiles()) return fileList[currentIndex];
        return "";
    }

    int count() const {
        return (int)fileList.size();
    }

    bool next() {
        if (fileList.empty()) return false;
        if (currentIndex + 1 < (int)fileList.size()) {
            currentIndex++;
            return true;
        }
        return false;
    }

    bool prev() {
        if (fileList.empty()) return false;
        if (currentIndex - 1 >= 0) {
            currentIndex--;
            return true;
        }
        return false;
    }

    bool first() {
        if (fileList.empty()) return false;
        currentIndex = 0;
        return true;
    }

    bool last() {
        if (fileList.empty()) return false;
        currentIndex = (int)fileList.size() - 1;
        return true;
    }

    bool jumpTo(int idx) {
        if (idx >= 0 && idx < (int)fileList.size()) {
            currentIndex = idx;
            return true;
        }
        return false;
    }
};
