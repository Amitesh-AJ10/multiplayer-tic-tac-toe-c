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
#include <signal.h>
#include <poll.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>

#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

// Global variables
int client_socket;
struct termios orig_termios;
int connected = 0;

// Function prototypes
void cleanup();
void reset_terminal();
void set_terminal_raw_mode();
void handle_server_message(char* message);
void print_help();
int connect_to_server(const char* server_ip);
void send_message(const char* message);

void cleanup() {
    if (connected) {
        close(client_socket);
        connected = 0;
    }
    reset_terminal();
}

void reset_terminal() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void set_terminal_raw_mode() {
    struct termios raw;
    
    // Save original terminal attributes
    tcgetattr(STDIN_FILENO, &orig_termios);
    
    // Register cleanup function to restore terminal on exit
    atexit(reset_terminal);
    
    // Modify terminal attributes for raw mode
    raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);  // Disable echo and canonical mode
    
    // Apply new terminal attributes
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int connect_to_server(const char* server_ip) {
    // Create standard socket for client
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    // Create raw socket for advanced control if needed
    int raw_socket = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (raw_socket < 0) {
        perror("Raw socket creation failed (requires root privileges)");
        // Continue anyway, we'll use the standard socket for communication
    } else {
        close(raw_socket);  // We won't use it directly in this client
    }
    
    // Prepare server address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    
    // Convert IP address from text to binary
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address / Address not supported");
        close(client_socket);
        return -1;
    }
    
    // Connect to server
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(client_socket);
        return -1;
    }
    
    connected = 1;
    return 0;
}

void handle_server_message(char* message) {
    // Simply print the message, as server sends formatted board and game info
    printf("%s", message);
}

void send_message(const char* message) {
    send(client_socket, message, strlen(message), 0);
}

void print_help() {
    printf("\n--- Tic-Tac-Toe Client Help ---\n");
    printf("Commands:\n");
    printf("  move <row> <col>  - Make a move (rows and cols are 0-2)\n");
    printf("  help              - Show this help message\n");
    printf("  quit              - Exit the game\n");
    printf("\nExample: move 0 1 (places your mark in the top-middle position)\n\n");
}

int main(int argc, char *argv[]) {
    // Check command line arguments
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    // Set up signal handling for clean exit
    signal(SIGINT, (void (*)(int))cleanup);
    
    // Set terminal to raw mode for better input handling
    set_terminal_raw_mode();
    
    // Connect to server
    printf("Connecting to %s:%d...\n", argv[1], SERVER_PORT);
    if (connect_to_server(argv[1]) < 0) {
        fprintf(stderr, "Failed to connect to server.\n");
        return EXIT_FAILURE;
    }
    
    printf("Connected to server!\n");
    print_help();
    
    // Set up poll for multiple input sources
    struct pollfd fds[2];
    
    // Monitor stdin for user input
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    
    // Monitor server socket for incoming messages
    fds[1].fd = client_socket;
    fds[1].events = POLLIN;
    
    char buffer[BUFFER_SIZE];
    char command[BUFFER_SIZE];
    int command_pos = 0;
    
    // Main client loop
    while (1) {
        int poll_result = poll(fds, 2, -1);
        
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;  // Interrupted by signal, continue loop
            }
            perror("Poll failed");
            break;
        }
        
        // Check for server messages
        if (fds[1].revents & POLLIN) {
            memset(buffer, 0, BUFFER_SIZE);
            int bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
            
            if (bytes_read <= 0) {
                if (bytes_read == 0) {
                    printf("Server closed the connection.\n");
                } else {
                    perror("recv failed");
                }
                break;
            }
            
            // Process server message
            handle_server_message(buffer);
        }
        
        // Check for user input
        if (fds[0].revents & POLLIN) {
            char c;
            int bytes_read = read(STDIN_FILENO, &c, 1);
            
            if (bytes_read > 0) {
                // Process user input character by character
                if (c == '\n') {
                    // Process completed command
                    command[command_pos] = '\0';
                    
                    // Echo the command
                    printf("\nCommand: %s\n", command);
                    
                    // Send command to server
                    send_message(command);
                    
                    // Check for quit command
                    if (strncmp(command, "quit", 4) == 0) {
                        printf("Exiting...\n");
                        break;
                    }
                    
                    // Reset command buffer
                    command_pos = 0;
                } else if (c == 127 || c == '\b') {
                    // Backspace/Delete
                    if (command_pos > 0) {
                        command_pos--;
                        printf("\b \b");  // Erase character from terminal
                    }
                } else if (command_pos < BUFFER_SIZE - 1) {
                    // Add character to command
                    command[command_pos++] = c;
                    printf("%c", c);  // Echo character
                }
            }
        }
    }
    
    // Clean up
    cleanup();
    printf("Disconnected from server.\n");
    
    return EXIT_SUCCESS;
}