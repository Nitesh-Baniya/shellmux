#include "client.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

// ── ANSI colour helpers ───────────────────────────────────────────────────────
#define C_RESET  "\033[0m"
#define C_GREEN  "\033[32m"
#define C_YELLOW "\033[33m"
#define C_RED    "\033[31m"
#define C_CYAN   "\033[36m"
#define C_BOLD   "\033[1m"
#define C_DIM    "\033[2m"

static termios g_saved_termios;
static bool    g_raw_mode = false;

// ── Constructor / Destructor ──────────────────────────────────────────────────
Client::Client()  : sock_fd_(-1) {}
Client::~Client() { restore_terminal(); if (sock_fd_ >= 0) close(sock_fd_); }

// ── connect ───────────────────────────────────────────────────────────────────
bool Client::connect_to_server() {
    sock_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd_ < 0) { perror("socket"); return false; }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << C_RED "[Error]" C_RESET
                  << " Cannot connect to server at " << SOCKET_PATH
                  << "\n" C_DIM "       Start it with: ./app server\n" C_RESET;
        return false;
    }
    return true;
}

// ── public commands ───────────────────────────────────────────────────────────
void Client::cmd_new_session(const std::string& name) {
    send_msg(Message::NEW_SESSION, -1, name.c_str(), name.size());
    print_response();
}

void Client::cmd_list() {
    send_msg(Message::LIST, -1, nullptr, 0);
    print_response();
}

void Client::cmd_attach(int session_id) {
    send_msg(Message::ATTACH, session_id, nullptr, 0);

    // First message is the banner (or error)
    Message hdr{}; char payload[MAX_PAYLOAD]{};
    if (!recv_msg(hdr, payload)) return;
    if (hdr.type == Message::ERROR_MSG) {
        std::cerr << C_RED "[Error] " C_RESET << payload << "\n";
        return;
    }
    std::cout << payload;
    std::cout.flush();
    attach_loop(session_id);
}

void Client::cmd_kill(int session_id) {
    send_msg(Message::KILL_SESSION, session_id, nullptr, 0);
    print_response();
}

void Client::cmd_rename(int session_id, const std::string& new_name) {
    send_msg(Message::RENAME, session_id, new_name.c_str(), new_name.size());
    print_response();
}

void Client::cmd_status() {
    send_msg(Message::STATUS, -1, nullptr, 0);
    print_response();
}

void Client::cmd_help(const char* prog) {
    std::cout
        << C_BOLD C_CYAN
        << "╔═══════════════════════════════════════════════════╗\n"
        << "║     Multi-Session Terminal Manager  —  Help       ║\n"
        << "╚═══════════════════════════════════════════════════╝\n"
        << C_RESET
        << C_BOLD "  COMMAND                   DESCRIPTION\n" C_RESET
        << "  ─────────────────────────────────────────────────\n"
        << C_GREEN "  " << prog << " server" C_RESET
        << "                Start the server (run in background)\n"
        << C_GREEN "  " << prog << " new <name>" C_RESET
        << "            Create a new named session\n"
        << C_GREEN "  " << prog << " list" C_RESET
        << "                  List all sessions with status\n"
        << C_GREEN "  " << prog << " attach <id>" C_RESET
        << "           Attach to a session by ID\n"
        << C_GREEN "  " << prog << " kill <id>" C_RESET
        << "             Kill (destroy) a session\n"
        << C_GREEN "  " << prog << " rename <id> <name>" C_RESET
        << "    Rename a session\n"
        << C_GREEN "  " << prog << " status" C_RESET
        << "                Show server uptime & stats\n"
        << C_GREEN "  " << prog << " help" C_RESET
        << "                  Show this help message\n"
        << "\n"
        << C_DIM "  While attached:  Ctrl+D = detach from session\n" C_RESET
        << "\n"
        << C_DIM "  Example workflow:\n"
        << "    ./app server &\n"
        << "    ./app new work\n"
        << "    ./app new logs\n"
        << "    ./app list\n"
        << "    ./app attach 0\n"
        << "    ./app rename 1 monitoring\n"
        << "    ./app status\n"
        << "    ./app kill 1\n" C_RESET;
}

// ── attach_loop: interactive I/O while attached ───────────────────────────────
void Client::attach_loop(int /*session_id*/) {
    set_raw_mode();

    // ── Session colour theme ──────────────────────────────────────────────────
    // Dark navy background + bright white text so it's obvious you're "inside"
    // \033[48;5;17m  = background: dark navy blue (256-colour palette #17)
    // \033[38;5;15m  = foreground: bright white
    // \033[2J\033[H  = clear screen, cursor to top-left
    const char* SESSION_THEME = "\033[48;5;17m\033[38;5;15m\033[2J\033[H";
    write(STDOUT_FILENO, SESSION_THEME, 18);

    char buf[MAX_PAYLOAD];

    while (true) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(sock_fd_,     &rfds);
        int maxfd = std::max(STDIN_FILENO, sock_fd_) + 1;
        timeval tv{ 0, 100000 };
        int ready = select(maxfd, &rfds, nullptr, nullptr, &tv);
        if (ready < 0) { if (errno == EINTR) continue; break; }

        // keyboard → server
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            int n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) break;
            // Ctrl+D (0x04) detaches
            if (n == 1 && buf[0] == 0x04) {
                send_msg(Message::DETACH, -1, nullptr, 0);
                break;
            }
            send_msg(Message::DATA, -1, buf, n);
        }

        // server → terminal
        if (FD_ISSET(sock_fd_, &rfds)) {
            Message hdr{}; char payload[MAX_PAYLOAD]{};
            if (!recv_msg(hdr, payload)) break;
            if (hdr.type == Message::DATA || hdr.type == Message::RESPONSE) {
                write(STDOUT_FILENO, payload, hdr.data_len);
            } else if (hdr.type == Message::ERROR_MSG) {
                restore_terminal();
                std::cerr << "\n" C_RED "[Error] " C_RESET << payload << "\n";
                set_raw_mode();
            }
        }
    }

    // ── Restore normal terminal colours before leaving ──────────────────────
    // \033[0m resets ALL attributes (bg, fg, bold, etc.)
    // \033[2J\033[H clears the coloured screen so normal prompt looks clean
    write(STDOUT_FILENO, "\033[0m\033[2J\033[H", 11);
    restore_terminal();

    // ── detach footer bar ─────────────────────────────────────────────────────
    std::cout << C_BOLD C_CYAN
              << "└─ Detached from session ───────────────────────────┘"
              << C_RESET "\n";
}

// ── terminal raw mode ─────────────────────────────────────────────────────────
void Client::set_raw_mode() {
    if (!isatty(STDIN_FILENO)) return;
    tcgetattr(STDIN_FILENO, &g_saved_termios);
    termios raw = g_saved_termios;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    g_raw_mode = true;
}

void Client::restore_terminal() {
    if (!g_raw_mode) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved_termios);
    g_raw_mode = false;
}

// ── helpers ───────────────────────────────────────────────────────────────────
void Client::print_response() {
    Message hdr{}; char payload[MAX_PAYLOAD]{};
    if (!recv_msg(hdr, payload)) return;
    if (hdr.type == Message::ERROR_MSG)
        std::cerr << C_RED "[Error] " C_RESET << payload << "\n";
    else
        std::cout << payload << "\n";
}

bool Client::send_msg(Message::Type type, int session_id,
                      const char* payload, int payload_len) {
    Message hdr{};
    hdr.type = type; hdr.session_id = session_id; hdr.data_len = payload_len;
    if (write(sock_fd_, &hdr, MSG_HDR_SIZE) != MSG_HDR_SIZE) return false;
    if (payload_len > 0 && payload)
        if (write(sock_fd_, payload, payload_len) != payload_len) return false;
    return true;
}

bool Client::recv_msg(Message& hdr, char* payload_buf) {
    if (read(sock_fd_, &hdr, MSG_HDR_SIZE) != MSG_HDR_SIZE) return false;
    if (hdr.data_len > 0 && hdr.data_len <= MAX_PAYLOAD) {
        int total = 0;
        while (total < hdr.data_len) {
            int r = read(sock_fd_, payload_buf + total, hdr.data_len - total);
            if (r <= 0) return false;
            total += r;
        }
        payload_buf[hdr.data_len] = '\0';
    }
    return true;
}
