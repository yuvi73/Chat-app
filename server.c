#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#define PORT 8888
#define MAX_CLIENTS 30
#define BUFFER_SIZE 1024
#define USERNAME_SIZE 32
#define MAX_ROOMS 5
#define ROOM_NAME_SIZE 32
#define LOG_FILE "server_log.txt"

// Client structure to store client information
typedef struct {
    int socket;
    char username[USERNAME_SIZE];
    int room_id;  // Current room the client is in
    time_t join_time;
    char ip_address[INET_ADDRSTRLEN];
} Client;

// Room structure to represent chat rooms
typedef struct {
    char name[ROOM_NAME_SIZE];
    int is_active;
    int current_users;
    int total_messages;
} Room;

// Global variables
Client clients[MAX_CLIENTS];
Room rooms[MAX_ROOMS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rooms_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int server_running = 1;
FILE *log_file = NULL;

// Function prototypes
void *handle_client(void *arg);
void initialize_rooms();
void initialize_clients();
int find_client_by_socket(int socket);
int find_client_by_username(const char *username);
void broadcast_room_message(const char *message, int sender_socket, int room_id);
void send_private_message(const char *message, int recipient_socket);
int process_command(char *buffer, int sender_index);
void log_message(const char *format, ...);
void cleanup_on_exit();
void sigint_handler(int sig);

// Initialize rooms
void initialize_rooms() {
    pthread_mutex_lock(&rooms_mutex);
    
    strcpy(rooms[0].name, "General");
    rooms[0].is_active = 1;
    rooms[0].current_users = 0;
    rooms[0].total_messages = 0;
    
    strcpy(rooms[1].name, "Tech");
    rooms[1].is_active = 1;
    rooms[1].current_users = 0;
    rooms[1].total_messages = 0;
    
    strcpy(rooms[2].name, "Gaming");
    rooms[2].is_active = 1;
    rooms[2].current_users = 0;
    rooms[2].total_messages = 0;
    
    strcpy(rooms[3].name, "Music");
    rooms[3].is_active = 1;
    rooms[3].current_users = 0;
    rooms[3].total_messages = 0;
    
    strcpy(rooms[4].name, "Movies");
    rooms[4].is_active = 1;
    rooms[4].current_users = 0;
    rooms[4].total_messages = 0;
    
    pthread_mutex_unlock(&rooms_mutex);
    
    log_message("Initialized %d chat rooms", MAX_ROOMS);
}

// Initialize clients array
void initialize_clients() {
    pthread_mutex_lock(&clients_mutex);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].socket = 0;
        memset(clients[i].username, 0, USERNAME_SIZE);
        clients[i].room_id = 0; // Default to general room
        clients[i].join_time = 0;
        memset(clients[i].ip_address, 0, INET_ADDRSTRLEN);
    }
    
    pthread_mutex_unlock(&clients_mutex);
}

// Log messages to file and console
void log_message(const char *format, ...) {
    char buffer[BUFFER_SIZE];
    va_list args;
    time_t now;
    struct tm *local_time;
    
    time(&now);
    local_time = localtime(&now);
    
    // Format timestamp
    char timestamp[30];
    strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d %H:%M:%S]", local_time);
    
    // Format message
    va_start(args, format);
    vsnprintf(buffer, BUFFER_SIZE, format, args);
    va_end(args);
    
    // Print to console
    printf("%s %s\n", timestamp, buffer);
    
    // Log to file if open
    if (log_file != NULL) {
        fprintf(log_file, "%s %s\n", timestamp, buffer);
        fflush(log_file);
    }
}

// Find client index by socket
int find_client_by_socket(int socket) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket == socket) {
            return i;
        }
    }
    return -1;
}

// Find client index by username
int find_client_by_username(const char *username) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket != 0 && strcmp(clients[i].username, username) == 0) {
            return i;
        }
    }
    return -1;
}

// Send message to all clients in a room except the sender
void broadcast_room_message(const char *message, int sender_socket, int room_id) {
    pthread_mutex_lock(&clients_mutex);
    pthread_mutex_lock(&rooms_mutex);
    
    // Update room message count
    if (room_id >= 0 && room_id < MAX_ROOMS) {
        rooms[room_id].total_messages++;
    }
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket > 0 && clients[i].socket != sender_socket && clients[i].room_id == room_id) {
            send(clients[i].socket, message, strlen(message), 0);
        }
    }
    
    pthread_mutex_unlock(&rooms_mutex);
    pthread_mutex_unlock(&clients_mutex);
}

// Send message to a specific client
void send_private_message(const char *message, int recipient_socket) {
    send(recipient_socket, message, strlen(message), 0);
}

// Process commands from client
int process_command(char *buffer, int sender_index) {
    char *command = strtok(buffer + 1, " "); // Skip the '/' character
    
    if (strcmp(command, "private") == 0 || strcmp(command, "pm") == 0) {
        char *recipient = strtok(NULL, " ");
        char *message = strtok(NULL, "");
        
        if (!recipient || !message) {
            char response[BUFFER_SIZE];
            sprintf(response, "Server: Usage: /private <username> <message>\n");
            send(clients[sender_index].socket, response, strlen(response), 0);
            return 1;
        }
        
        pthread_mutex_lock(&clients_mutex);
        int recipient_index = find_client_by_username(recipient);
        pthread_mutex_unlock(&clients_mutex);
        
        if (recipient_index == -1) {
            char response[BUFFER_SIZE];
            sprintf(response, "Server: User '%s' not found\n", recipient);
            send(clients[sender_index].socket, response, strlen(response), 0);
            return 1;
        }
        
        char private_msg[BUFFER_SIZE];
        sprintf(private_msg, "[PM from %s]: %s\n", clients[sender_index].username, message);
        send_private_message(private_msg, clients[recipient_index].socket);
        
        char confirmation[BUFFER_SIZE];
        sprintf(confirmation, "[PM to %s]: %s\n", recipient, message);
        send_private_message(confirmation, clients[sender_index].socket);
        
        log_message("Private message from %s to %s", clients[sender_index].username, recipient);
        return 1;
    } 
    else if (strcmp(command, "join") == 0) {
        char *room_name = strtok(NULL, " ");
        if (!room_name) {
            char response[BUFFER_SIZE];
            sprintf(response, "Server: Usage: /join <room_name>\n");
            send(clients[sender_index].socket, response, strlen(response), 0);
            return 1;
        }
        
        int found = 0;
        int new_room_id = -1;
        
        pthread_mutex_lock(&rooms_mutex);
        for (int i = 0; i < MAX_ROOMS; i++) {
            if (rooms[i].is_active && strcasecmp(rooms[i].name, room_name) == 0) {
                found = 1;
                new_room_id = i;
                break;
            }
        }
        pthread_mutex_unlock(&rooms_mutex);
        
        if (!found) {
            char response[BUFFER_SIZE];
            sprintf(response, "Server: Room '%s' not found\n", room_name);
            send(clients[sender_index].socket, response, strlen(response), 0);
            return 1;
        }
        
        pthread_mutex_lock(&clients_mutex);
        pthread_mutex_lock(&rooms_mutex);
        
        int old_room_id = clients[sender_index].room_id;
        
        // Decrease user count in old room
        if (old_room_id >= 0 && old_room_id < MAX_ROOMS) {
            rooms[old_room_id].current_users--;
        }
        
        // Set new room and increase user count
        clients[sender_index].room_id = new_room_id;
        rooms[new_room_id].current_users++;
        
        pthread_mutex_unlock(&rooms_mutex);
        pthread_mutex_unlock(&clients_mutex);
        
        char leave_msg[BUFFER_SIZE];
        sprintf(leave_msg, "Server: %s has left the room\n", clients[sender_index].username);
        broadcast_room_message(leave_msg, clients[sender_index].socket, old_room_id);
        
        char join_msg[BUFFER_SIZE];
        sprintf(join_msg, "Server: %s has joined the room\n", clients[sender_index].username);
        broadcast_room_message(join_msg, clients[sender_index].socket, new_room_id);
        
        char response[BUFFER_SIZE];
        sprintf(response, "Server: You joined room '%s'\n", room_name);
        send(clients[sender_index].socket, response, strlen(response), 0);
        
        log_message("User %s moved from room %s to %s", 
                   clients[sender_index].username, 
                   rooms[old_room_id].name, 
                   rooms[new_room_id].name);
        
        return 1;
    }
    else if (strcmp(command, "rooms") == 0) {
        pthread_mutex_lock(&rooms_mutex);
        
        char response[BUFFER_SIZE] = "Server: Available rooms:\n";
        for (int i = 0; i < MAX_ROOMS; i++) {
            if (rooms[i].is_active) {
                char room_info[100];
                sprintf(room_info, "- %s (%d users)\n", rooms[i].name, rooms[i].current_users);
                strcat(response, room_info);
            }
        }
        
        pthread_mutex_unlock(&rooms_mutex);
        
        send(clients[sender_index].socket, response, strlen(response), 0);
        return 1;
    }
    else if (strcmp(command, "users") == 0) {
        pthread_mutex_lock(&clients_mutex);
        pthread_mutex_lock(&rooms_mutex);
        
        int room_id = clients[sender_index].room_id;
        char response[BUFFER_SIZE];
        sprintf(response, "Server: Users in room '%s':\n", rooms[room_id].name);
        
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].socket > 0 && clients[i].room_id == room_id) {
                char user_info[USERNAME_SIZE + 10];
                sprintf(user_info, "- %s\n", clients[i].username);
                strcat(response, user_info);
            }
        }
        
        pthread_mutex_unlock(&rooms_mutex);
        pthread_mutex_unlock(&clients_mutex);
        
        send(clients[sender_index].socket, response, strlen(response), 0);
        return 1;
    }
    else if (strcmp(command, "help") == 0) {
        char help_msg[BUFFER_SIZE] = 
            "Server: Available commands:\n"
            "/private <username> <message> - Send private message\n"
            "/pm <username> <message> - Alias for private\n"
            "/join <room> - Join a chat room\n"
            "/rooms - List available rooms\n"
            "/users - List users in current room\n"
            "/help - Show this help message\n";
        
        send(clients[sender_index].socket, help_msg, strlen(help_msg), 0);
        return 1;
    }
    
    return 0; // Not a recognized command
}

// Thread function to handle client connections
void *handle_client(void *arg) {
    int client_socket = *(int*)arg;
    free(arg);
    
    char buffer[BUFFER_SIZE] = {0};
    int valread;
    int client_index = -1;
    
    // Get client IP address
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    getpeername(client_socket, (struct sockaddr *)&addr, &addr_len);
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    
    // First message is the username
    valread = read(client_socket, buffer, BUFFER_SIZE);
    if (valread <= 0) {
        close(client_socket);
        return NULL;
    }
    
    // Trim newline
    buffer[strcspn(buffer, "\n")] = 0;
    
    // Check if username is valid
    if (strlen(buffer) == 0 || strlen(buffer) >= USERNAME_SIZE) {
        char msg[] = "Server: Invalid username. Please reconnect with a valid username.\n";
        send(client_socket, msg, strlen(msg), 0);
        close(client_socket);
        return NULL;
    }
    
    // Check if username is already in use
    pthread_mutex_lock(&clients_mutex);
    if (find_client_by_username(buffer) != -1) {
        pthread_mutex_unlock(&clients_mutex);
        char msg[] = "Server: Username already in use. Please reconnect with a different username.\n";
        send(client_socket, msg, strlen(msg), 0);
        close(client_socket);
        return NULL;
    }
    
    // Find free slot for client
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket == 0) {
            client_index = i;
            clients[i].socket = client_socket;
            strncpy(clients[i].username, buffer, USERNAME_SIZE - 1);
            clients[i].username[USERNAME_SIZE - 1] = '\0';
            clients[i].room_id = 0; // Default to general room
            clients[i].join_time = time(NULL);
            strncpy(clients[i].ip_address, client_ip, INET_ADDRSTRLEN - 1);
            clients[i].ip_address[INET_ADDRSTRLEN - 1] = '\0';
            
            // Increment room user count
            pthread_mutex_lock(&rooms_mutex);
            rooms[0].current_users++;
            pthread_mutex_unlock(&rooms_mutex);
            
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    
    if (client_index == -1) {
        char msg[] = "Server: Server is full. Please try again later.\n";
        send(client_socket, msg, strlen(msg), 0);
        close(client_socket);
        return NULL;
    }
    
    log_message("New user %s connected from %s", clients[client_index].username, client_ip);
    
    // Send welcome message
    char welcome_msg[BUFFER_SIZE];
    sprintf(welcome_msg, "Server: Welcome %s! You are now in room 'General'. Type /help for available commands.\n", 
            clients[client_index].username);
    send(client_socket, welcome_msg, strlen(welcome_msg), 0);
    
    // Announce new user to room
    char announce_msg[BUFFER_SIZE];
    sprintf(announce_msg, "Server: %s has joined the chat\n", clients[client_index].username);
    broadcast_room_message(announce_msg, client_socket, clients[client_index].room_id);
    
    // Main loop to handle client messages
    while (server_running) {
        memset(buffer, 0, BUFFER_SIZE);
        valread = read(client_socket, buffer, BUFFER_SIZE);
        
        if (valread <= 0) {
            break;
        }
        
        // Trim newline
        buffer[strcspn(buffer, "\n")] = 0;
        
        // Check if it's a command
        if (buffer[0] == '/') {
            process_command(buffer, client_index);
        } 
        // Regular message
        else if (strlen(buffer) > 0) {
            char msg[BUFFER_SIZE];
            sprintf(msg, "[%s]: %s\n", clients[client_index].username, buffer);
            broadcast_room_message(msg, client_socket, clients[client_index].room_id);
            
            log_message("Message from %s in room %s: %s", 
                       clients[client_index].username, 
                       rooms[clients[client_index].room_id].name, 
                       buffer);
        }
    }
    
    // Handle disconnection
    pthread_mutex_lock(&clients_mutex);
    pthread_mutex_lock(&rooms_mutex);
    
    // Get client details before clearing
    char username[USERNAME_SIZE];
    strcpy(username, clients[client_index].username);
    int room_id = clients[client_index].room_id;
    
    // Decrement room user count
    if (room_id >= 0 && room_id < MAX_ROOMS) {
        rooms[room_id].current_users--;
    }
    
    // Clear client data
    clients[client_index].socket = 0;
    memset(clients[client_index].username, 0, USERNAME_SIZE);
    
    pthread_mutex_unlock(&rooms_mutex);
    pthread_mutex_unlock(&clients_mutex);
    
    // Notify other clients
    char disconnect_msg[BUFFER_SIZE];
    sprintf(disconnect_msg, "Server: %s has left the chat\n", username);
    broadcast_room_message(disconnect_msg, client_socket, room_id);
    
    // Close socket
    close(client_socket);
    
    log_message("User %s disconnected", username);
    
    return NULL;
}

// Signal handler for Ctrl+C
void sigint_handler(int sig) {
    printf("\nShutting down server...\n");
    server_running = 0;
    
    // Notify all clients
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket > 0) {
            char msg[] = "Server: Server is shutting down. Goodbye!\n";
            send(clients[i].socket, msg, strlen(msg), 0);
            close(clients[i].socket);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    
    cleanup_on_exit();
    exit(0);
}

// Clean up resources
void cleanup_on_exit() {
    if (log_file != NULL) {
        fclose(log_file);
    }
    
    pthread_mutex_destroy(&clients_mutex);
    pthread_mutex_destroy(&rooms_mutex);
}

int main() {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    
    // Set up signal handler
    signal(SIGINT, sigint_handler);
    
    // Open log file
    log_file = fopen(LOG_FILE, "a");
    if (log_file == NULL) {
        perror("Failed to open log file");
        // Continue anyway, will log to console only
    }
    
    // Initialize data structures
    initialize_clients();
    initialize_rooms();
    
    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        cleanup_on_exit();
        exit(EXIT_FAILURE);
    }
    
    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        cleanup_on_exit();
        exit(EXIT_FAILURE);
    }
    
    // Setup address structure
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    // Bind
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        cleanup_on_exit();
        exit(EXIT_FAILURE);
    }
    
    // Listen
    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        cleanup_on_exit();
        exit(EXIT_FAILURE);
    }
    
    log_message("Server started on port %d", PORT);
    
    // Main server loop
    while (server_running) {
        // Accept incoming connection
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        
        int *new_socket = malloc(sizeof(int));
        if (!new_socket) {
            perror("Memory allocation failed");
            continue;
        }
        
        *new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (*new_socket < 0) {
            free(new_socket);
            if (!server_running) break; // Server is shutting down
            perror("Accept failed");
            continue;
        }
        
        // Create thread to handle client
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void *)new_socket) < 0) {
            perror("Thread creation failed");
            close(*new_socket);
            free(new_socket);
            continue;
        }
        
        // Detach thread
        pthread_detach(thread_id);
    }
    
    // Clean up before exit
    close(server_fd);
    cleanup_on_exit();
    
    return 0;
}