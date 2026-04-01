#include "server.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <unistd.h>

// ── ANSI colour helpers (server-side, for console logs only) ─────────────────
#define C_RESET  "\033[0m"
#define C_GREEN  "\033[32m"
#define C_YELLOW "\033[33m"
#define C_RED    "\033[31m"
#define C_CYAN   "\033[36m"
#define C_BOLD   "\033[1m"

Server::Server() : server_fd_(-1), start_time_(time(nullptr)) {}

Server::~Server() {
    for (auto* s : sessions_) delete s;
    if (server_fd_ >= 0) close(server_fd_);
    unlink(SOCKET_PATH);
}

// ── run ───────────────────────────────────────────────────────────────────────
void Server::run() {
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) { perror("socket"); return; }

    unlink(SOCKET_PATH);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        { perror("bind"); return; }
    if (listen(server_fd_, 5) < 0)
        { perror("listen"); return; }

    std::cout << C_BOLD C_GREEN
              << "╔══════════════════════════════════════╗\n"
              << "║  Terminal Manager Server — RUNNING   ║\n"
              << "╚══════════════════════════════════════╝" C_RESET "\n"
              << C_CYAN << "  Socket : " C_RESET << SOCKET_PATH << "\n"
              << C_CYAN << "  Limit  : " C_RESET << MAX_SESSIONS << " sessions\n\n";

    while (true) {
        int client_fd = accept(server_fd_, nullptr, nullptr);
        if (client_fd < 0) { perror("accept"); continue; }
        std::cout << C_YELLOW << "[Server] client connected\n" C_RESET;
        handle_client(client_fd);
        close(client_fd);
        std::cout << C_YELLOW << "[Server] client disconnected\n" C_RESET;
        cleanup_dead_sessions();
    }
}

// ── dispatch ─────────────────────────────────────────────────────────────────
void Server::handle_client(int client_fd) {
    Message hdr{};
    char    payload[MAX_PAYLOAD]{};
    if (!recv_msg(client_fd, hdr, payload)) return;

    switch (hdr.type) {
    case Message::NEW_SESSION:  handle_new_session(client_fd, payload); break;
    case Message::LIST:         handle_list(client_fd);                 break;
    case Message::ATTACH:       handle_attach(client_fd, hdr.session_id); break;
    case Message::KILL_SESSION: handle_kill(client_fd, hdr.session_id);   break;
    case Message::RENAME:       handle_rename(client_fd, hdr.session_id, payload); break;
    case Message::STATUS:       handle_status(client_fd);               break;
    default:
        send_msg(client_fd, Message::ERROR_MSG, -1, "Unknown command", 15);
    }
}

// ── handlers ─────────────────────────────────────────────────────────────────
void Server::handle_new_session(int client_fd, const char* payload) {
    if ((int)sessions_.size() >= MAX_SESSIONS) {
        std::string err = "Session limit reached (" + std::to_string(MAX_SESSIONS) + ")";
        send_msg(client_fd, Message::ERROR_MSG, -1, err.c_str(), err.size());
        return;
    }
    std::string session_name(payload);
    if (session_name.empty()) session_name = "session";

    int new_id = static_cast<int>(sessions_.size());
    auto* sess = new Session(new_id, session_name);
    if (!sess->start()) {
        delete sess;
        send_msg(client_fd, Message::ERROR_MSG, -1, "Failed to start session", 22);
        return;
    }
    sessions_.push_back(sess);

    std::string reply = "Session created: id=" + std::to_string(new_id)
                      + "  name=" + session_name;
    send_msg(client_fd, Message::RESPONSE, new_id, reply.c_str(), reply.size());
}

void Server::handle_list(int client_fd) {
    std::ostringstream oss;
    // Header with colour codes embedded — client will print as-is
    oss << "\033[1m\033[36m"
        << " ID  NAME             STATUS    UPTIME    CMDS\n"
        << "─────────────────────────────────────────────\033[0m\n";

    if (sessions_.empty()) {
        oss << "  (no sessions)\n";
    } else {
        for (auto* s : sessions_) {
            // Colour: green = active, red = dead
            std::string status_col = s->active ? "\033[32m active \033[0m"
                                                : "\033[31m  dead  \033[0m";
            oss << "  " << std::setw(2) << s->id << "  "
                << std::left << std::setw(16) << s->name
                << status_col
                << "  " << std::setw(8) << s->uptime_str()
                << "  " << s->cmd_count << "\n";
        }
    }
    std::string reply = oss.str();
    send_msg(client_fd, Message::RESPONSE, -1, reply.c_str(), reply.size());
}

void Server::handle_attach(int client_fd, int session_id) {
    Session* sess = find_session(session_id);
    if (!sess) {
        std::string err = "No session with id=" + std::to_string(session_id);
        send_msg(client_fd, Message::ERROR_MSG, -1, err.c_str(), err.size());
        return;
    }
    if (!sess->active) {
        std::string err = "Session " + std::to_string(session_id) + " is not active";
        send_msg(client_fd, Message::ERROR_MSG, -1, err.c_str(), err.size());
        return;
    }
    // Banner sent to client
    std::string ok = "\033[1m\033[32m"
                     "┌─ Attached: [" + std::to_string(session_id)
                   + "] " + sess->name
                   + " ─────────────────────────┐\033[0m\n"
                     "\033[90m  Press Ctrl+D to detach\033[0m\n";
    send_msg(client_fd, Message::RESPONSE, session_id, ok.c_str(), ok.size());
    relay_loop(client_fd, sess);
}

void Server::handle_kill(int client_fd, int session_id) {
    Session* sess = find_session(session_id);
    if (!sess) {
        std::string err = "No session with id=" + std::to_string(session_id);
        send_msg(client_fd, Message::ERROR_MSG, -1, err.c_str(), err.size());
        return;
    }
    sess->stop();
    std::string reply = "\033[31mSession " + std::to_string(session_id)
                      + " (" + sess->name + ") killed.\033[0m";
    send_msg(client_fd, Message::RESPONSE, session_id, reply.c_str(), reply.size());
}

void Server::handle_rename(int client_fd, int session_id, const char* payload) {
    Session* sess = find_session(session_id);
    if (!sess) {
        std::string err = "No session with id=" + std::to_string(session_id);
        send_msg(client_fd, Message::ERROR_MSG, -1, err.c_str(), err.size());
        return;
    }
    std::string old_name = sess->name;
    sess->name = std::string(payload);
    std::string reply = "Session " + std::to_string(session_id)
                      + " renamed: " + old_name + " → " + sess->name;
    send_msg(client_fd, Message::RESPONSE, session_id, reply.c_str(), reply.size());
}

void Server::handle_status(int client_fd) {
    long uptime = static_cast<long>(time(nullptr) - start_time_);
    long h = uptime/3600, m = (uptime%3600)/60, s = uptime%60;

    int active_count = 0;
    for (auto* sess : sessions_) if (sess->active) active_count++;

    std::ostringstream oss;
    oss << "\033[1m\033[36m"
        << "┌──────────────────────────────────┐\n"
        << "│      Terminal Manager Status      │\n"
        << "└──────────────────────────────────┘\033[0m\n"
        << "  Server PID  : " << getpid() << "\n"
        << "  Uptime      : " << h << "h " << m << "m " << s << "s\n"
        << "  Sessions    : " << active_count << " active / "
                              << sessions_.size() << " total\n"
        << "  Max allowed : " << MAX_SESSIONS << "\n"
        << "  Socket      : " << SOCKET_PATH << "\n";
    std::string reply = oss.str();
    send_msg(client_fd, Message::RESPONSE, -1, reply.c_str(), reply.size());
}

// ── relay loop ────────────────────────────────────────────────────────────────
void Server::relay_loop(int client_fd, Session* sess) {
    char buf[MAX_PAYLOAD];
    while (sess->active) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(client_fd,       &rfds);
        FD_SET(sess->master_fd, &rfds);
        int maxfd = std::max(client_fd, sess->master_fd) + 1;
        timeval tv{ 0, 100000 };
        int ready = select(maxfd, &rfds, nullptr, nullptr, &tv);
        if (ready < 0) { if (errno == EINTR) continue; break; }

        // client → shell
        if (FD_ISSET(client_fd, &rfds)) {
            Message hdr{}; char payload[MAX_PAYLOAD]{};
            if (!recv_msg(client_fd, hdr, payload)) break;
            if (hdr.type == Message::DETACH) break;
            if (hdr.type == Message::DATA && hdr.data_len > 0) {
                // count Enter keypresses as commands
                for (int i = 0; i < hdr.data_len; i++)
                    if (payload[i] == '\r' || payload[i] == '\n')
                        sess->cmd_count++;
                sess->write_to_shell(payload, hdr.data_len);
            }
        }

        // shell → client
        if (FD_ISSET(sess->master_fd, &rfds)) {
            int n = sess->read_from_shell(buf, sizeof(buf));
            if (n > 0) {
                send_msg(client_fd, Message::DATA, sess->id, buf, n);
            } else if (n < 0) {
                sess->active = false;
                std::string bye = "\n\033[31m[Session exited]\033[0m\n";
                send_msg(client_fd, Message::RESPONSE, sess->id,
                         bye.c_str(), bye.size());
                break;
            }
        }
    }
}

// ── helpers ───────────────────────────────────────────────────────────────────
bool Server::send_msg(int fd, Message::Type type, int session_id,
                      const char* payload, int payload_len) {
    Message hdr{};
    hdr.type = type; hdr.session_id = session_id; hdr.data_len = payload_len;
    if (write(fd, &hdr, MSG_HDR_SIZE) != MSG_HDR_SIZE) return false;
    if (payload_len > 0 && payload)
        if (write(fd, payload, payload_len) != payload_len) return false;
    return true;
}

bool Server::recv_msg(int fd, Message& hdr, char* payload_buf) {
    if (read(fd, &hdr, MSG_HDR_SIZE) != MSG_HDR_SIZE) return false;
    if (hdr.data_len > 0 && hdr.data_len <= MAX_PAYLOAD) {
        int total = 0;
        while (total < hdr.data_len) {
            int r = read(fd, payload_buf + total, hdr.data_len - total);
            if (r <= 0) return false;
            total += r;
        }
        payload_buf[hdr.data_len] = '\0';
    }
    return true;
}

Session* Server::find_session(int id) {
    for (auto* s : sessions_) if (s->id == id) return s;
    return nullptr;
}

void Server::cleanup_dead_sessions() {
    for (auto* s : sessions_) if (!s->active) s->stop();
}
