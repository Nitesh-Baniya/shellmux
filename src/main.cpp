//
// main.cpp — entry point
//
// ./app server
// ./app new   <name>
// ./app list
// ./app attach <id>
// ./app kill   <id>
// ./app rename <id> <new-name>
// ./app status
// ./app help
//

#include "server.h"
#include "client.h"
#include <iostream>
#include <string>
#include <cstdlib>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        Client tmp;
        tmp.cmd_help(argv[0]);
        return 1;
    }

    std::string cmd = argv[1];

    // ── server ────────────────────────────────────────────────────────────────
    if (cmd == "server") {
        Server server;
        server.run();
        return 0;
    }

    // ── help (no server needed) ───────────────────────────────────────────────
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        Client tmp;
        tmp.cmd_help(argv[0]);
        return 0;
    }

    // ── client commands ───────────────────────────────────────────────────────
    Client client;
    if (!client.connect_to_server()) return 1;

    if (cmd == "new") {
        std::string name = (argc >= 3) ? argv[2] : "session";
        client.cmd_new_session(name);

    } else if (cmd == "list") {
        client.cmd_list();

    } else if (cmd == "attach") {
        if (argc < 3) { std::cerr << "Usage: " << argv[0] << " attach <id>\n"; return 1; }
        client.cmd_attach(std::atoi(argv[2]));

    } else if (cmd == "kill") {
        if (argc < 3) { std::cerr << "Usage: " << argv[0] << " kill <id>\n"; return 1; }
        client.cmd_kill(std::atoi(argv[2]));

    } else if (cmd == "rename") {
        if (argc < 4) { std::cerr << "Usage: " << argv[0] << " rename <id> <new-name>\n"; return 1; }
        client.cmd_rename(std::atoi(argv[2]), argv[3]);

    } else if (cmd == "status") {
        client.cmd_status();

    } else {
        std::cerr << "Unknown command: " << cmd
                  << "\nRun '" << argv[0] << " help' for usage.\n";
        return 1;
    }

    return 0;
}
