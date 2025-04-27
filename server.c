#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <time.h>
#include <stdbool.h>
#include <signal.h>
#include <errno.h>

#define SERVER_PORT 8080
#define MAX_CLIENTS 2
#define BUFFER_SIZE 1024
#define TIMEOUT_SECONDS 300 // 5 minutes timeout

// Game state
typedef struct {
    char board[3][3];
    int current_player;  // 0 for first player (X), 1 for second player (O)
    int connected_clients;
    int client_sockets[MAX_CLIENTS];
    bool game_active;
    time_t last_activity;
} GameState;

GameState game;

// Function prototypes
void initialize_game();
void handle_client_message(int client_socket, char* message);
bool make_move(int row, int col, int player);
bool check_win();
bool check_draw();
void send_to_all_clients(char* message);
void send_game_state();
void send_to_client(int client_socket, char* message);
void handle_client_disconnect(int client_socket);
void check_timeout();
void cleanup_and_exit(int sig);
void print_board();

int main() {
    // Set up signal handling for clean exit
    signal(SIGINT, cleanup_and_exit);
    
    // Initialize the game
    initialize_game();
    
    // Create server socket (raw socket)
    int server_fd = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (server_fd < 0) {
        perror("Raw socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Note: For raw sockets, we need root privileges
    if (getuid() != 0) {
        fprintf(stderr, "Raw sockets require root privileges. Please run as root.\n");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    // Create a standard TCP socket for accepting connections
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("Listen socket creation failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(server_fd);
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    
    // Prepare the sockaddr_in structure
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);
    
    // Bind the socket
    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    
    // Listen for connections
    if (listen(listen_fd, 2) < 0) {
        perror("Listen failed");
        close(server_fd);
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Tic-Tac-Toe server started on port %d\n", SERVER_PORT);
    printf("Waiting for players to connect...\n");
    
    // Main server loop
    while (1) {
        // Check for timeout
        check_timeout();
        
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);
        FD_SET(server_fd, &read_fds);
        
        int max_fd = listen_fd > server_fd ? listen_fd : server_fd;
        
        // Add client sockets to fd_set
        for (int i = 0; i < game.connected_clients; i++) {
            int client_fd = game.client_sockets[i];
            if (client_fd > 0) {
                FD_SET(client_fd, &read_fds);
                if (client_fd > max_fd) {
                    max_fd = client_fd;
                }
            }
        }
        
        // Set timeout for select
        struct timeval tv;
        tv.tv_sec = 1;  // 1 second timeout for select
        tv.tv_usec = 0;
        
        // Wait for activity on any socket
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
        
        if (activity < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, continue the loop
            }
            perror("Select error");
            continue;
        }
        
        // Handle raw socket packets
        if (FD_ISSET(server_fd, &read_fds)) {
            unsigned char buffer[BUFFER_SIZE];
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            int bytes_read = recvfrom(server_fd, buffer, BUFFER_SIZE, 0, 
                                     (struct sockaddr*)&client_addr, &client_len);
            
            if (bytes_read > 0) {
                struct iphdr* ip_header = (struct iphdr*)buffer;
                int ip_header_length = ip_header->ihl * 4;
                
                // Get the TCP header
                struct tcphdr* tcp_header = (struct tcphdr*)(buffer + ip_header_length);
                
                // Check if this packet is destined for our server port
                if (ntohs(tcp_header->dest) == SERVER_PORT) {
                    // Extract payload (if any)
                    int tcp_header_length = tcp_header->doff * 4;
                    char* payload = (char*)(buffer + ip_header_length + tcp_header_length);
                    int payload_length = bytes_read - ip_header_length - tcp_header_length;
                    
                    if (payload_length > 0) {
                        char client_ip[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
                        
                        printf("Received raw packet from %s:%d\n", 
                               client_ip, ntohs(tcp_header->source));
                        
                        // Find client socket by address/port if needed
                        // (Not directly used for processing in this example)
                    }
                }
            }
        }
        
        // Handle new connection on listen socket
        if (FD_ISSET(listen_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int new_socket = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
            
            if (new_socket < 0) {
                perror("Accept failed");
                continue;
            }
            
            // Check if game is full
            if (game.connected_clients >= MAX_CLIENTS) {
                char* message = "Game is full. Try again later.\n";
                send(new_socket, message, strlen(message), 0);
                close(new_socket);
                continue;
            }
            
            // Add new client
            game.client_sockets[game.connected_clients] = new_socket;
            game.connected_clients++;
            game.last_activity = time(NULL);
            
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            printf("New connection from %s:%d, assigned as Player %d\n", 
                   client_ip, ntohs(client_addr.sin_port), game.connected_clients);
            
            // Send welcome message
            char welcome_msg[BUFFER_SIZE];
            sprintf(welcome_msg, "Welcome! You are Player %d (%c)\n", 
                    game.connected_clients, (game.connected_clients == 1) ? 'X' : 'O');
            send_to_client(new_socket, welcome_msg);
            
            // If game is ready to start
            if (game.connected_clients == MAX_CLIENTS && !game.game_active) {
                game.game_active = true;
                send_to_all_clients("Game is starting!\n");
                send_game_state();
            } else if (game.connected_clients < MAX_CLIENTS) {
                send_to_client(new_socket, "Waiting for another player to join...\n");
            }
        }
        
        // Handle client messages
        for (int i = 0; i < game.connected_clients; i++) {
            int client_fd = game.client_sockets[i];
            
            if (FD_ISSET(client_fd, &read_fds)) {
                char buffer[BUFFER_SIZE] = {0};
                int valread = read(client_fd, buffer, BUFFER_SIZE);
                
                if (valread <= 0) {
                    // Client disconnected
                    handle_client_disconnect(client_fd);
                } else {
                    // Process message
                    buffer[valread] = '\0';  // Ensure null termination
                    handle_client_message(client_fd, buffer);
                }
            }
        }
    }
    
    // Clean up
    close(server_fd);
    close(listen_fd);
    return 0;
}

void initialize_game() {
    // Initialize the board
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            game.board[i][j] = ' ';
        }
    }
    
    game.current_player = 0;  // X goes first
    game.connected_clients = 0;
    game.game_active = false;
    game.last_activity = time(NULL);
    
    memset(game.client_sockets, 0, sizeof(game.client_sockets));
}

void handle_client_message(int client_socket, char* message) {
    printf("Received message: %s", message);
    game.last_activity = time(NULL);
    
    // Find which player this is
    int player_index = -1;
    for (int i = 0; i < game.connected_clients; i++) {
        if (game.client_sockets[i] == client_socket) {
            player_index = i;
            break;
        }
    }
    
    if (player_index == -1) {
        printf("Error: Client not found\n");
        return;
    }
    
    // Parse the message: expect format "move row col" (e.g., "move 0 1")
    int row, col;
    if (sscanf(message, "move %d %d", &row, &col) == 2) {
        // Check if it's this player's turn
        if (player_index != game.current_player) {
            send_to_client(client_socket, "Not your turn! Please wait.\n");
            return;
        }
        
        // Make the move
        if (make_move(row, col, player_index)) {
            char move_msg[BUFFER_SIZE];
            sprintf(move_msg, "Player %d (%c) placed at position (%d,%d)\n", 
                    player_index + 1, (player_index == 0) ? 'X' : 'O', row, col);
            send_to_all_clients(move_msg);
            
            // Send updated game state to all clients
            send_game_state();
            
            // Check for win or draw
            if (check_win()) {
                char win_msg[BUFFER_SIZE];
                sprintf(win_msg, "Player %d (%c) wins!\n", 
                        player_index + 1, (player_index == 0) ? 'X' : 'O');
                send_to_all_clients(win_msg);
                
                // Reset the game
                send_to_all_clients("Starting a new game...\n");
                initialize_game();
                game.connected_clients = 2;  // Keep players connected
                game.game_active = true;
                send_game_state();
            } else if (check_draw()) {
                send_to_all_clients("Game ended in a draw!\n");
                
                // Reset the game
                send_to_all_clients("Starting a new game...\n");
                initialize_game();
                game.connected_clients = 2;  // Keep players connected
                game.game_active = true;
                send_game_state();
            } else {
                // Switch to next player
                game.current_player = 1 - game.current_player;
                char turn_msg[BUFFER_SIZE];
                sprintf(turn_msg, "It's Player %d's (%c) turn\n", 
                        game.current_player + 1, (game.current_player == 0) ? 'X' : 'O');
                send_to_all_clients(turn_msg);
            }
        } else {
            // Invalid move
            send_to_client(client_socket, "Invalid move! Try again.\n");
        }
    } else if (strncmp(message, "quit", 4) == 0) {
        // Player wants to quit
        char quit_msg[BUFFER_SIZE];
        sprintf(quit_msg, "Player %d has quit the game.\n", player_index + 1);
        send_to_all_clients(quit_msg);
        handle_client_disconnect(client_socket);
    } else if (strncmp(message, "help", 4) == 0) {
        // Player asked for help
        char help_msg[BUFFER_SIZE];
        sprintf(help_msg, "Commands:\n"
                          "  move <row> <col> - Make a move (rows and cols are 0-2)\n"
                          "  quit - Exit the game\n"
                          "  help - Show this help message\n");
        send_to_client(client_socket, help_msg);
    } else {
        // Unknown command
        send_to_client(client_socket, "Unknown command. Type 'help' for available commands.\n");
    }
}

bool make_move(int row, int col, int player) {
    // Check if move is valid
    if (row < 0 || row > 2 || col < 0 || col > 2) {
        return false;
    }
    
    // Check if the cell is empty
    if (game.board[row][col] != ' ') {
        return false;
    }
    
    // Make the move
    game.board[row][col] = (player == 0) ? 'X' : 'O';
    return true;
}

bool check_win() {
    char mark = (game.current_player == 0) ? 'X' : 'O';
    
    // Check rows
    for (int i = 0; i < 3; i++) {
        if (game.board[i][0] == mark && game.board[i][1] == mark && game.board[i][2] == mark) {
            return true;
        }
    }
    
    // Check columns
    for (int i = 0; i < 3; i++) {
        if (game.board[0][i] == mark && game.board[1][i] == mark && game.board[2][i] == mark) {
            return true;
        }
    }
    
    // Check diagonals
    if (game.board[0][0] == mark && game.board[1][1] == mark && game.board[2][2] == mark) {
        return true;
    }
    if (game.board[0][2] == mark && game.board[1][1] == mark && game.board[2][0] == mark) {
        return true;
    }
    
    return false;
}

bool check_draw() {
    // Check if all cells are filled
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (game.board[i][j] == ' ') {
                return false;
            }
        }
    }
    return true;  // All cells are filled and no winner
}

void send_to_all_clients(char* message) {
    for (int i = 0; i < game.connected_clients; i++) {
        send_to_client(game.client_sockets[i], message);
    }
}

void print_board_to_string(char* buffer) {
    sprintf(buffer,
            "\n  0 1 2\n"
            "0 %c|%c|%c\n"
            "  -+-+-\n"
            "1 %c|%c|%c\n"
            "  -+-+-\n"
            "2 %c|%c|%c\n\n",
            game.board[0][0], game.board[0][1], game.board[0][2],
            game.board[1][0], game.board[1][1], game.board[1][2],
            game.board[2][0], game.board[2][1], game.board[2][2]);
}

void send_game_state() {
    char board_str[BUFFER_SIZE];
    print_board_to_string(board_str);
    send_to_all_clients(board_str);
    
    char turn_msg[BUFFER_SIZE];
    sprintf(turn_msg, "It's Player %d's (%c) turn\n", 
            game.current_player + 1, (game.current_player == 0) ? 'X' : 'O');
    send_to_all_clients(turn_msg);
}

void send_to_client(int client_socket, char* message) {
    if (send(client_socket, message, strlen(message), 0) < 0) {
        printf("Error sending message to client\n");
    }
}

void handle_client_disconnect(int client_socket) {
    printf("Client disconnected\n");
    
    // Find the client in the array
    int index = -1;
    for (int i = 0; i < game.connected_clients; i++) {
        if (game.client_sockets[i] == client_socket) {
            index = i;
            break;
        }
    }
    
    if (index == -1) {
        return;
    }
    
    // Close the socket
    close(client_socket);
    
    // Remove client from array by shifting remaining clients
    for (int i = index; i < game.connected_clients - 1; i++) {
        game.client_sockets[i] = game.client_sockets[i + 1];
    }
    
    game.connected_clients--;
    
    // Notify remaining clients
    send_to_all_clients("A player has disconnected.\n");
    
    // Reset game if it was active
    if (game.game_active) {
        game.game_active = false;
        initialize_game();
        if (game.connected_clients > 0) {
            send_to_all_clients("Waiting for another player to join...\n");
        }
    }
}

void check_timeout() {
    time_t current_time = time(NULL);
    
    // Check if game has been inactive for too long
    if (game.game_active && (current_time - game.last_activity) > TIMEOUT_SECONDS) {
        printf("Game timed out due to inactivity\n");
        send_to_all_clients("Game timed out due to inactivity.\n");
        initialize_game();
        game.connected_clients = 0;  // Reset client count
    }
}

void cleanup_and_exit(int sig) {
    printf("\nShutting down server...\n");
    
    // Close all client connections
    for (int i = 0; i < game.connected_clients; i++) {
        if (game.client_sockets[i] > 0) {
            send_to_client(game.client_sockets[i], "Server is shutting down. Goodbye!\n");
            close(game.client_sockets[i]);
        }
    }
    
    exit(0);
}

void print_board() {
    printf("\n  0 1 2\n");
    for (int i = 0; i < 3; i++) {
        printf("%d ", i);
        for (int j = 0; j < 3; j++) {
            printf("%c", game.board[i][j]);
            if (j < 2) printf("|");
        }
        printf("\n");
        if (i < 2) printf("  -+-+-\n");
    }
    printf("\n");
}