#pragma once
#include "session.h"
#include <cstdint>
#include <vector>
#include <string>
#include <ctime>

// ── Wire protocol ─────────────────────────────────────────────────────────────
struct Message {
    enum Type : uint8_t {
        NEW_SESSION  = 1,
        ATTACH       = 2,
        LIST         = 3,
        DATA         = 4,
        RESPONSE     = 5,
        DETACH       = 6,
        ERROR_MSG    = 7,
        KILL_SESSION = 8,
        RENAME       = 9,
        STATUS       = 10,
    };
    Type type;
    int  session_id;
    int  data_len;
};

constexpr int MSG_HDR_SIZE = sizeof(Message);
constexpr int MAX_PAYLOAD  = 4096;
constexpr int MAX_SESSIONS = 10;
constexpr const char* SOCKET_PATH = "/tmp/termmgr.sock";

// ─────────────────────────────────────────────────────────────────────────────
class Server {
public:
    Server();
    ~Server();
    void run();

private:
    int                   server_fd_;
    std::vector<Session*> sessions_;
    time_t                start_time_;

    bool send_msg(int fd, Message::Type type, int session_id,
                  const char* payload, int payload_len);
    bool recv_msg(int fd, Message& hdr, char* payload_buf);

    void handle_client      (int client_fd);
    void handle_new_session (int client_fd, const char* payload);
    void handle_list        (int client_fd);
    void handle_attach      (int client_fd, int session_id);
    void handle_kill        (int client_fd, int session_id);
    void handle_rename      (int client_fd, int session_id, const char* payload);
    void handle_status      (int client_fd);
    void relay_loop         (int client_fd, Session* sess);

    Session* find_session(int id);
    void     cleanup_dead_sessions();
};
