#pragma once
#include <string>
#include <ctime>
#include <unistd.h>

class Session {
public:
    int         id;
    std::string name;
    pid_t       shell_pid;
    int         master_fd;
    bool        active;
    time_t      created_at;   // when session was created
    int         cmd_count;    // counts Enter keypresses (approx commands run)

    Session(int id, const std::string& name);
    ~Session();

    bool start();
    void stop();

    bool write_to_shell(const char* buf, int len);
    int  read_from_shell(char* buf, int max_len);

    // "3m 20s" style uptime string
    std::string uptime_str() const;
};
