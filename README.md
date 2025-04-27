# Multiplayer Tic-Tac-Toe (C, Raw Sockets)

A simple multiplayer Tic-Tac-Toe game built using C and TCP sockets (IPv4).

## Features
- Multiplayer support over a network.
- Server manages turns and game logic.
- Raw socket programming with manual packet handling.

## Getting Started

### Prerequisites
- GCC compiler
- Linux environment (for raw socket support)

### Build Steps
1. Compile the project:
   ```bash
   make
   ```
   This will create two executables:
   - `ttt_server`
   - `ttt_client`

2. Run the server:
   ```bash
   sudo ./ttt_server
   ```

3. Run the client:
   ```bash
   ./ttt_client <server_ip>
   ```
   If the server and client are on the same machine, use:
   ```bash
   ./ttt_client 127.0.0.1
   ```

### Gameplay Commands
- **Move**:  
  ```plaintext
  move <row> <col>
  ```
  Example: `move 0 1` places your mark in the top-middle position.

- **Help**:  
  Type `help` for instructions during the game.

- **Quit**:  
  Type `quit` to exit the game.

- **Terminate**:  
  Press `Ctrl + C` to stop the server or client manually.

### Cleaning Up
To remove compiled files:
```bash
make clean
```

## Project Structure
- `server.c`: Server-side code
- `client.c`: Client-side code
- `Makefile`: Build automation

## License
This project is licensed under the MIT License.
