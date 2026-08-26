#pragma once

// File operations: trash, delete, rename, reveal, open-with.
//
// Deleting is done through the freedesktop Trash specification rather than
// unlink(), so a mistake is recoverable from the desktop's own trash UI. That
// means moving the file into $XDG_DATA_HOME/Trash/files and writing a matching
// .trashinfo record next to it, which is what every Linux file manager reads.
//
// Nothing here touches the database or the UI; callers decide what to do with
// the result.

#include <string>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <system_error>

#ifndef _WIN32
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

namespace silverfileops {

namespace fs = std::filesystem;

struct Result {
    bool ok = false;
    std::string error;
    std::string newPath;    // set by rename / trash
};

inline Result fail(const std::string& msg) { Result r; r.error = msg; return r; }
inline Result okay(std::string newPath = {}) { Result r; r.ok = true; r.newPath = std::move(newPath); return r; }

// ---------------------------------------------------------------------------
// Trash
// ---------------------------------------------------------------------------

inline std::string dataHome() {
    const char* xdg = getenv("XDG_DATA_HOME");
    if (xdg && *xdg) return xdg;
    const char* home = getenv("HOME");
    if (!home || !*home) {
#ifndef _WIN32
        struct passwd* pw = getpwuid(getuid());
        if (pw && pw->pw_dir) home = pw->pw_dir;
#endif
    }
    return std::string(home ? home : "/tmp") + "/.local/share";
}

// The Path field of a .trashinfo is percent-encoded per RFC 2396. Getting this
// wrong makes the desktop's "restore" silently put the file somewhere else.
inline std::string percentEncodePath(const std::string& path) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(path.size() * 2);
    for (unsigned char c : path) {
        bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') ||
                          c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
        if (unreserved) out.push_back((char)c);
        else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 0xF]); }
    }
    return out;
}

inline std::string isoNow() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmv);
    return buf;
}

// Move a file to the desktop trash. Returns the path it now lives at.
inline Result moveToTrash(const std::string& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) return fail("file no longer exists");

    fs::path trashRoot = fs::path(dataHome()) / "Trash";
    fs::path filesDir = trashRoot / "files";
    fs::path infoDir  = trashRoot / "info";
    fs::create_directories(filesDir, ec);
    fs::create_directories(infoDir, ec);
    if (ec) return fail("cannot create trash directory: " + ec.message());

    fs::path src(path);
    std::string stem = src.stem().string();
    std::string ext  = src.extension().string();

    // Trash names must be unique across everything already in there, and the
    // .trashinfo has to match the stored name exactly.
    std::string name = src.filename().string();
    fs::path dest = filesDir / name;
    for (int n = 1; fs::exists(dest, ec) && n < 10000; ++n) {
        name = stem + "." + std::to_string(n) + ext;
        dest = filesDir / name;
    }
    if (fs::exists(dest, ec)) return fail("could not find a free name in trash");

    fs::path infoPath = infoDir / (name + ".trashinfo");
    {
        FILE* fp = fopen(infoPath.c_str(), "wb");
        if (!fp) return fail("cannot write trashinfo");
        fs::path absolute = fs::absolute(src, ec);
        std::string abs = ec ? path : absolute.string();
        fprintf(fp, "[Trash Info]\nPath=%s\nDeletionDate=%s\n",
                percentEncodePath(abs).c_str(), isoNow().c_str());
        fclose(fp);
    }

    // Rename first; fall back to copy+remove when trash is on another device,
    // which is the usual case for files outside $HOME.
    fs::rename(src, dest, ec);
    if (ec) {
        std::error_code ec2;
        fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec2);
        if (ec2) {
            fs::remove(infoPath, ec2);
            return fail("cannot move to trash: " + ec.message());
        }
        fs::remove(src, ec2);
        if (ec2) {
            fs::remove(dest, ec2);
            fs::remove(infoPath, ec2);
            return fail("cannot remove original: " + ec2.message());
        }
    }
    return okay(dest.string());
}

inline Result deletePermanently(const std::string& path) {
    std::error_code ec;
    if (!fs::remove(path, ec)) return fail(ec ? ec.message() : "file no longer exists");
    return okay();
}

// ---------------------------------------------------------------------------
// Rename
// ---------------------------------------------------------------------------

inline bool isValidFilename(const std::string& name) {
    if (name.empty() || name == "." || name == "..") return false;
    return name.find('/') == std::string::npos && name.find('\0') == std::string::npos;
}

inline Result renameFile(const std::string& path, const std::string& newName) {
    if (!isValidFilename(newName)) return fail("invalid file name");
    std::error_code ec;
    fs::path src(path);
    fs::path dest = src.parent_path() / newName;
    if (fs::exists(dest, ec)) return fail("a file with that name already exists");
    fs::rename(src, dest, ec);
    if (ec) return fail(ec.message());
    return okay(dest.string());
}

// ---------------------------------------------------------------------------
// Handing a path to the desktop
// ---------------------------------------------------------------------------

#ifndef _WIN32
// Detached so the child never becomes a zombie the app has to reap, and never
// inherits the GL context's file descriptors.
inline void spawnDetached(const char* program, const char* const argv[]) {
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (fork() == 0) {
            execvp(program, (char* const*)argv);
            _exit(127);
        }
        _exit(0);
    } else if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);   // reap the intermediate child immediately
    }
}
#endif

inline Result openWithDefaultApp(const std::string& path) {
    if (path.empty()) return fail("no path");
#ifndef _WIN32
    const char* argv[] = { "xdg-open", path.c_str(), nullptr };
    spawnDetached("xdg-open", argv);
    return okay();
#else
    return fail("unsupported");
#endif
}

// Reveal in the file manager. Falls back to opening the containing folder,
// since only some managers understand --select.
inline Result revealInFileManager(const std::string& path) {
    if (path.empty()) return fail("no path");
#ifndef _WIN32
    std::error_code ec;
    fs::path parent = fs::path(path).parent_path();
    if (parent.empty() || !fs::exists(parent, ec)) return fail("folder not found");
    std::string folder = parent.string();
    const char* argv[] = { "xdg-open", folder.c_str(), nullptr };
    spawnDetached("xdg-open", argv);
    return okay();
#else
    return fail("unsupported");
#endif
}

} // namespace silverfileops
