#pragma once

// -----------------------------------------------------------------------------
// SilverPlatform - the small number of OS calls this project needs that differ
// between POSIX and Windows.
//
// Everything here has a POSIX implementation that is a thin pass-through, so the
// Linux build behaves exactly as before. The Windows side exists so the codebase
// compiles there; it is written against the documented CRT/Win32 behaviour but
// has not been built on Windows, so treat it as unverified.
// -----------------------------------------------------------------------------

#include <string>
#include <vector>
#include <ctime>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <filesystem>
#include <winsock2.h>   // gethostname
#else
#include <dirent.h>
#include <unistd.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>

// MSVC's CRT defines the _S_IF* constants but not the POSIX test macros.
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif

namespace silverplat {

// ---- directory listing ------------------------------------------------------

struct DirEntry {
    std::string name;
    bool isDir = false;
    bool isFile = false;
    bool typeKnown = false;   // false means the caller must stat() to find out
};

// List one directory (non-recursive). Returns false if it could not be opened.
// "." and ".." are never reported.
inline bool listDirectory(const std::string& path, std::vector<DirEntry>& out) {
    out.clear();

#ifdef _WIN32
    std::error_code ec;
    std::filesystem::directory_iterator it(path, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) return false;

    for (const auto& entry : it) {
        DirEntry e;
        e.name = entry.path().filename().string();
        if (e.name == "." || e.name == "..") continue;
        std::error_code ec2;
        e.isDir = entry.is_directory(ec2);
        e.isFile = entry.is_regular_file(ec2);
        e.typeKnown = !ec2;
        out.push_back(std::move(e));
    }
    return true;
#else
    DIR* dp = opendir(path.c_str());
    if (!dp) return false;

    struct dirent* de = nullptr;
    while ((de = readdir(dp)) != nullptr) {
        DirEntry e;
        e.name = de->d_name;
        if (e.name == "." || e.name == "..") continue;

#ifdef DT_DIR
        // d_type avoids a stat() per entry on filesystems that provide it,
        // which is most of the walk's cost on a large library.
        if (de->d_type == DT_DIR)       { e.isDir = true;  e.typeKnown = true; }
        else if (de->d_type == DT_REG)  { e.isFile = true; e.typeKnown = true; }
        else if (de->d_type == DT_UNKNOWN || de->d_type == DT_LNK) { e.typeKnown = false; }
        else continue;   // sockets, fifos and the like are never images
#endif
        out.push_back(std::move(e));
    }
    closedir(dp);
    return true;
#endif
}

// ---- time -------------------------------------------------------------------

// Thread-safe localtime.
inline bool localTime(std::time_t t, std::tm& out) {
#ifdef _WIN32
    return localtime_s(&out, &t) == 0;
#else
    return localtime_r(&t, &out) != nullptr;
#endif
}

// Parse "YYYY:MM:DD HH:MM:SS" - the only format EXIF uses for DateTimeOriginal.
// strptime is POSIX-only, and this is far narrower than what strptime accepts,
// so the same hand-rolled parser is used everywhere for consistent behaviour.
inline bool parseExifDateTime(const char* text, std::tm& out) {
    if (!text) return false;

    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (sscanf(text, "%d:%d:%d %d:%d:%d",
               &year, &month, &day, &hour, &minute, &second) != 6) {
        return false;
    }
    if (year < 1900 || month < 1 || month > 12 || day < 1 || day > 31) return false;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) return false;

    std::memset(&out, 0, sizeof(out));
    out.tm_year = year - 1900;
    out.tm_mon = month - 1;
    out.tm_mday = day;
    out.tm_hour = hour;
    out.tm_min = minute;
    out.tm_sec = second;
    out.tm_isdst = -1;   // let mktime work out DST for the local zone
    return true;
}

// ---- misc -------------------------------------------------------------------

inline std::string hostName() {
    char buf[256] = "localhost";
#ifdef _WIN32
    DWORD len = (DWORD)sizeof(buf);
    if (GetComputerNameA(buf, &len)) return std::string(buf, len);
#else
    if (gethostname(buf, sizeof(buf)) == 0) {
        buf[sizeof(buf) - 1] = '\0';
        return buf;
    }
#endif
    return "localhost";
}

} // namespace silverplat
