#include "session.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <fcntl.h>
#include <pty.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <unistd.h>

Session::Session(int id, const std::string& name)
    : id(id), name(name), shell_pid(-1), master_fd(-1),
      active(false), created_at(time(nullptr)), cmd_count(0) {}

Session::~Session() { stop(); }

bool Session::start() {
    int slave_fd = -1;
    if (openpty(&master_fd, &slave_fd, nullptr, nullptr, nullptr) < 0) {
        std::cerr << "[Session] openpty() failed: " << strerror(errno) << "\n";
        return false;
    }

    shell_pid = fork();
    if (shell_pid < 0) {
        std::cerr << "[Session] fork() failed: " << strerror(errno) << "\n";
        close(master_fd); close(slave_fd);
        return false;
    }

    if (shell_pid == 0) {
        // ── child: become the shell ──
        close(master_fd);
        setsid();
        if (ioctl(slave_fd, TIOCSCTTY, 0) < 0)
            std::cerr << "[Session/child] TIOCSCTTY failed\n";
        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);
        if (slave_fd > STDERR_FILENO) close(slave_fd);
        execlp("/bin/bash", "bash", nullptr);
        _exit(1);
    }

    // ── parent ──
    close(slave_fd);
    int flags = fcntl(master_fd, F_GETFL, 0);
    fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);
    active = true;
    std::cout << "[Session] Started '" << name
              << "' id=" << id << " pid=" << shell_pid << "\n";
    return true;
}

void Session::stop() {
    if (!active) return;
    active = false;
    if (shell_pid > 0) {
        kill(shell_pid, SIGTERM);
        usleep(100000);
        kill(shell_pid, SIGKILL);
        waitpid(shell_pid, nullptr, 0);
        shell_pid = -1;
    }
    if (master_fd >= 0) { close(master_fd); master_fd = -1; }
    std::cout << "[Session] Stopped '" << name << "'\n";
}

bool Session::write_to_shell(const char* buf, int len) {
    if (!active || master_fd < 0) return false;
    return write(master_fd, buf, len) == len;
}

int Session::read_from_shell(char* buf, int max_len) {
    if (!active || master_fd < 0) return -1;
    int n = read(master_fd, buf, max_len);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
    return n;
}

std::string Session::uptime_str() const {
    long secs = static_cast<long>(time(nullptr) - created_at);
    long h = secs / 3600, m = (secs % 3600) / 60, s = secs % 60;
    std::ostringstream oss;
    if (h > 0) oss << h << "h ";
    if (h > 0 || m > 0) oss << m << "m ";
    oss << s << "s";
    return oss.str();
}
