#pragma once

// Small per-user single-instance coordinator used by SilverGallery.  A second
// process connects to the first process' Unix socket, asks it to activate its
// window, and exits.  All GLFW/window work remains on the main thread.

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

class SilverSingleInstance {
public:
    SilverSingleInstance() = default;
    ~SilverSingleInstance() { stop(); }

    SilverSingleInstance(const SilverSingleInstance&) = delete;
    SilverSingleInstance& operator=(const SilverSingleInstance&) = delete;

    // Returns true for the primary process. A false result means an existing
    // instance was notified and this process should exit immediately.
    bool becomePrimary(const std::string& applicationId) {
#ifdef _WIN32
        mutex_ = CreateMutexA(nullptr, FALSE, applicationId.c_str());
        if (!mutex_) return true; // Do not prevent launch if coordination fails.
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND window = FindWindowA(applicationId.c_str(), nullptr);
            if (window) {
                if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
                SetForegroundWindow(window);
            }
            CloseHandle(mutex_);
            mutex_ = nullptr;
            return false;
        }
        return true;
#else
        const char* runtime = std::getenv("XDG_RUNTIME_DIR");
        std::string base = (runtime && runtime[0]) ? runtime : "/tmp";
        std::string stem = base + "/" + applicationId + "-" + std::to_string((long long)getuid());
        lockPath_ = stem + ".lock";
        socketPath_ = stem + ".sock";

        lockFd_ = open(lockPath_.c_str(), O_CREAT | O_RDWR, 0600);
        if (lockFd_ < 0) return true;
        if (flock(lockFd_, LOCK_EX | LOCK_NB) != 0) {
            notifyExisting();
            close(lockFd_);
            lockFd_ = -1;
            return false;
        }

        listenFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (listenFd_ < 0) return true;

        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        if (socketPath_.size() >= sizeof(address.sun_path)) {
            close(listenFd_);
            listenFd_ = -1;
            return true;
        }
        std::strncpy(address.sun_path, socketPath_.c_str(), sizeof(address.sun_path) - 1);
        unlink(socketPath_.c_str()); // Safe: this process owns the lock.
        if (bind(listenFd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listenFd_, 4) != 0) {
            close(listenFd_);
            listenFd_ = -1;
            return true;
        }

        running_.store(true);
        listener_ = std::thread([this]() { listenLoop(); });
        return true;
#endif
    }

    bool consumeActivation() { return activationPending_.exchange(false); }

    // Called from the listener thread, so this must be a thread-safe wake-up
    // function (glfwPostEmptyEvent is explicitly thread safe).
    void setWakeFunction(std::function<void()> wake) {
        std::lock_guard<std::mutex> lock(wakeMutex_);
        wake_ = std::move(wake);
    }

    void stop() {
#ifdef _WIN32
        if (mutex_) {
            CloseHandle(mutex_);
            mutex_ = nullptr;
        }
#else
        running_.store(false);
        if (listenFd_ >= 0) shutdown(listenFd_, SHUT_RDWR);
        if (listener_.joinable()) listener_.join();
        if (listenFd_ >= 0) {
            close(listenFd_);
            listenFd_ = -1;
        }
        if (!socketPath_.empty()) unlink(socketPath_.c_str());
        if (lockFd_ >= 0) {
            flock(lockFd_, LOCK_UN);
            close(lockFd_);
            lockFd_ = -1;
        }
#endif
    }

private:
#ifdef _WIN32
    HANDLE mutex_ = nullptr;
#else
    int lockFd_ = -1;
    int listenFd_ = -1;
    std::string lockPath_;
    std::string socketPath_;
    std::atomic<bool> running_{false};
    std::thread listener_;

    void notifyExisting() {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return;
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        if (socketPath_.size() < sizeof(address.sun_path)) {
            std::strncpy(address.sun_path, socketPath_.c_str(), sizeof(address.sun_path) - 1);
            if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
                const char activate = 'A';
                (void)send(fd, &activate, 1, 0);
            }
        }
        close(fd);
    }

    void listenLoop() {
        while (running_.load()) {
            pollfd descriptor{listenFd_, POLLIN, 0};
            int result = poll(&descriptor, 1, 250);
            if (result <= 0 || !(descriptor.revents & POLLIN)) continue;
            int client = accept(listenFd_, nullptr, nullptr);
            if (client < 0) continue;
            char command = 0;
            if (recv(client, &command, 1, 0) == 1 && command == 'A') {
                activationPending_.store(true);
                std::function<void()> wake;
                {
                    std::lock_guard<std::mutex> lock(wakeMutex_);
                    wake = wake_;
                }
                if (wake) wake();
            }
            close(client);
        }
    }
#endif

    std::atomic<bool> activationPending_{false};
    std::mutex wakeMutex_;
    std::function<void()> wake_;
};
