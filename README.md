# shellmux

A lightweight terminal multiplexer written in **C++17** that lets you create and manage multiple named shell sessions from the command line. Built as a university OS project to demonstrate core operating system concepts in a clean, readable codebase.

---

## Features

- Create multiple named terminal sessions
- Attach to any session interactively
- Rename and kill sessions
- View session status, uptime, and command count
- Visual color theme change when inside a session
- UNIX domain socket IPC between client and server
- Session limit enforcement (max 10 sessions)

---

## OS Concepts Demonstrated

| Concept | Where Used |
|---|---|
| `fork()` + `exec()` | Spawning `/bin/bash` for each session |
| Pseudo-terminal (PTY) | `openpty()` gives the shell a real terminal |
| UNIX domain sockets | IPC between client and server process |
| `select()` | Non-blocking I/O multiplexing in the relay loop |
| `setsid()` + `TIOCSCTTY` | Process groups and controlling terminal setup |
| `termios` raw mode | Forwarding keystrokes unmodified to the shell |
| `SIGTERM` / `SIGKILL` | Graceful and forced session termination |

---

## Project Structure

```
shellmux/
├── src/
│   ├── main.cpp        # Entry point, CLI argument parsing
│   ├── server.cpp      # Server: socket accept loop, session dispatch
│   ├── server.h
│   ├── client.cpp      # Client: connects, sends commands, attach loop
│   ├── client.h
│   ├── session.cpp     # Session: fork/exec bash with PTY
│   └── session.h
├── build/              # Compiled output (generated, not tracked in git)
├── Makefile
├── .gitignore
└── README.md
```

---

## Requirements

- Linux (uses POSIX APIs: `openpty`, `fork`, UNIX sockets)
- g++ with C++17 support
- `libutil` (usually pre-installed on all Linux distros)

---

## Build

```bash
# Clone the repo
git clone https://github.com/niteshbaniya/shellmux.git
cd shellmux

# Compile
make

# Binary is placed at
./build/app
```

To clean build artifacts:
```bash
make clean
```

---

## Usage

### 1. Start the server
> **Terminal 1** — Run this first and leave it open.

The server must be running before any client commands work:
```bash
./build/app server
```

> Tip: You can also run it in the background with `./build/app server &` and use a single terminal.

### 2. Create a session
> **Terminal 2** — All client commands go here.

```bash
./build/app new work
./build/app new logs
```

### 3. List all sessions
> **Terminal 2**
```bash
./build/app list
```
Output:
```
 ID  NAME             STATUS    UPTIME    CMDS
─────────────────────────────────────────────
   0  work             active    12s       3
   1  logs             active    5s        0
```

### 4. Attach to a session
> **Terminal 2** — your terminal will visually change while attached.

```bash
./build/app attach 0
```
The terminal background turns **dark blue** to indicate you are inside the session.
Run any shell commands normally. Press `Ctrl+D` to detach and return to Terminal 2.

### 5. Rename a session
> **Terminal 2**
```bash
./build/app rename 0 backend
```

### 6. Kill a session
> **Terminal 2**
```bash
./build/app kill 1
```

### 7. Server status
> **Terminal 2**
```bash
./build/app status
```
Output:
```
┌──────────────────────────────────┐
│      Terminal Manager Status      │
└──────────────────────────────────┘
  Server PID  : 12345
  Uptime      : 0h 4m 32s
  Sessions    : 1 active / 2 total
  Max allowed : 10
  Socket      : /tmp/termmgr.sock
```

### 8. Help
> **Terminal 2** — works any time, no server needed.
```bash
./build/app help
```

---

## Full Example Workflow

> Open **two terminal windows** side by side.

**Terminal 1 — Start the server** (keep this running in the background)
```bash
./build/app server
```
You should see:
```
╔══════════════════════════════════════╗
║  Terminal Manager Server — RUNNING   ║
╚══════════════════════════════════════╝
  Socket : /tmp/termmgr.sock
  Limit  : 10 sessions
```

---

**Terminal 2 — Use the client**

Create two sessions:
```bash
./build/app new work
./build/app new monitoring
```

List them:
```bash
./build/app list
```

Attach to session 0:
```bash
./build/app attach 0
```
> Your terminal background turns **dark blue** — you are now inside the session.
> Run any shell commands here (`ls`, `pwd`, `top`, etc.)
> Press `Ctrl+D` to detach and return to normal terminal.

Rename session 1:
```bash
./build/app rename 1 logs
```

Check server stats:
```bash
./build/app status
```

Kill session 1:
```bash
./build/app kill 1
```

List again to confirm:
```bash
./build/app list
```

---

## Architecture

```
┌─────────────────────────────────────────────┐
│                   SERVER                     │
│                                              │
│   Session 0 "work"    Session 1 "logs"       │
│   shell_pid=1234      shell_pid=1235         │
│   PTY master_fd       PTY master_fd          │
│          ▲  select() relay loop  ▼           │
│     UNIX Domain Socket (/tmp/termmgr.sock)   │
└──────────────────┬──────────────────────────┘
                   │  Message { type, session_id, data_len } + payload
┌──────────────────▼──────────────────────────┐
│                   CLIENT                     │
│   raw terminal mode → stdin/stdout relay     │
└─────────────────────────────────────────────┘
```

### Communication Protocol

Every message between client and server uses a simple fixed-size header:

```cpp
struct Message {
    Type type;        // command type (1 byte)
    int  session_id;  // target session
    int  data_len;    // bytes of payload that follow
};
```

This is followed by `data_len` raw bytes of payload. The fixed header makes parsing straightforward with no framing complexity.

---

## Wire Protocol Message Types

| Type | Direction | Purpose |
|---|---|---|
| `NEW_SESSION` | client → server | Create a new session |
| `ATTACH` | client → server | Attach to a session |
| `LIST` | client → server | List all sessions |
| `DATA` | bidirectional | Raw I/O bytes (keystrokes / shell output) |
| `RESPONSE` | server → client | Text reply (status, list output) |
| `DETACH` | client → server | End the attach loop |
| `KILL_SESSION` | client → server | Kill a session by ID |
| `RENAME` | client → server | Rename a session |
| `STATUS` | client → server | Request server stats |
| `ERROR_MSG` | server → client | Error description |

---

## Viva / Interview Q&A

**Q: How does a session start?**  
`Session::start()` calls `openpty()` to create a PTY master/slave pair, then `fork()` to create a child process. The child calls `setsid()` to become a process group leader, sets the slave PTY as its controlling terminal via `ioctl(TIOCSCTTY)`, redirects stdin/stdout/stderr to the slave PTY using `dup2()`, then calls `execlp("/bin/bash")` to replace itself with a shell.

**Q: How does IPC work?**  
A UNIX domain socket at `/tmp/termmgr.sock` connects client and server. Every message has a fixed-size `Message` header (type, session_id, data_len) followed by a variable-length payload. The server accepts one client at a time in a sequential loop.

**Q: How is I/O forwarded between client and shell?**  
The server's `relay_loop()` uses `select()` to watch two file descriptors simultaneously — the client socket and the PTY master fd. When the client sends keystrokes (DATA message), they are written to the PTY master. When the shell produces output, it is read from the PTY master and sent back to the client as a DATA message.

**Q: Why use a PTY instead of a regular pipe?**  
Shells behave differently when connected to a terminal vs a pipe. A PTY makes the shell think it has a real terminal, enabling line editing, colors, job control, and correct `isatty()` checks. A plain pipe would make bash run in non-interactive mode and break many features.

**Q: What is raw terminal mode?**  
The client disables `ICANON` (line buffering) and `ECHO` in its own terminal using `tcsetattr()`. This means every single keypress is immediately available to read and forwarded to the shell, rather than waiting for Enter. It is restored when detaching.

---

## Limitations

- One client at a time (by design — simplifies the server)
- No session persistence across server restarts
- No scrollback buffer
- No window resize forwarding (`TIOCSWINSZ`) — may affect `vim`/`nano` layout

---

## License

MIT License — free to use, modify, and distribute.