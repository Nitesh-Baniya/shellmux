#pragma once
#include "server.h"
#include <string>

class Client {
public:
    Client();
    ~Client();

    bool connect_to_server();

    void cmd_new_session (const std::string& name);
    void cmd_list        ();
    void cmd_attach      (int session_id);
    void cmd_kill        (int session_id);
    void cmd_rename      (int session_id, const std::string& new_name);
    void cmd_status      ();
    void cmd_help        (const char* prog);

private:
    int sock_fd_;

    bool send_msg(Message::Type type, int session_id,
                  const char* payload, int payload_len);
    bool recv_msg(Message& hdr, char* payload_buf);
    void attach_loop (int session_id);
    void set_raw_mode    ();
    void restore_terminal();

    // Print a single-response message (RESPONSE or ERROR_MSG)
    void print_response();
};
