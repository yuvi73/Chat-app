#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>

#define PORT 8888
#define BUFFER_SIZE 1024
#define SERVER_IP "127.0.0.1"

// Global variables
int sock = 0;
volatile int connected = 0;

// Signal handler for Ctrl+C
void sigint_handler(int sig) {
    if (connected) {
        printf("\nDisconnecting from server...\n");
        close(sock);
    }
    exit(0);
}

// Function to receive messages from server
void *receive_messages(void *socket_desc) {
    int sock = *(int*)socket_desc;
    char buffer[BUFFER_SIZE];
    
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int valread = read(sock, buffer, BUFFER_SIZE);
        
        if (valread <= 0) {
            // Server disconnected or error
            printf("\nServer disconnected\n");
            connected = 0;
            break;
        }
        
        printf("%s", buffer);
    }
    
    return NULL;
}

// Function to display help message
void display_help() {
    printf("\nAvailable commands:\n");
    printf("/private <username> <message> - Send a private message\n");
    printf("/pm <username> <message> - Alias for private\n");
    printf("/join <room> - Join a chat room\n");
    printf("/rooms - List available rooms\n");
    printf("/users - List users in current room\n");
    printf("/help - Show this help message\n");
    printf("/quit - Exit the chat application\n\n");
}

int main() {
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    pthread_t receive_thread;
    
    // Set up signal handler for graceful exit
    signal(SIGINT, sigint_handler);
    
    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Set up server address structure
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    
    // Convert IPv4 address from text to binary form
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        exit(EXIT_FAILURE);
    }
    
    // Connect to server
    printf("Connecting to server at %s:%d...\n", SERVER_IP, PORT);
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }
    
    connected = 1;
    printf("Connected to server\n");
    
    // Create thread to receive messages
    if (pthread_create(&receive_thread, NULL, receive_messages, (void*)&sock) < 0) {
        perror("Thread creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Get username
    printf("Enter your username: ");
    if (!fgets(buffer, BUFFER_SIZE, stdin)) {
        printf("Error reading username\n");
        close(sock);
        exit(EXIT_FAILURE);
    }
    
    // Send username to server
    send(sock, buffer, strlen(buffer), 0);
    
    // Display local help
    printf("\nEnhanced Chat Client\n");
    printf("Type /help to see available commands\n");
    
    // Main loop to send messages
    while (connected) {
        // Get user input
        memset(buffer, 0, BUFFER_SIZE);
        if (!fgets(buffer, BUFFER_SIZE, stdin)) {
            break;
        }
        
        // Check for quit command
        if (strncmp(buffer, "/quit", 5) == 0) {
            printf("Disconnecting from server...\n");
            break;
        }
        
        // Check for local help command
        if (strncmp(buffer, "/help", 5) == 0 && strlen(buffer) <= 6) {
            display_help();
            continue;
        }
        
        // Send message to server
        send(sock, buffer, strlen(buffer), 0);
    }
    
    // Clean up
    close(sock);
    connected = 0;
    pthread_join(receive_thread, NULL);
    
    return 0;
}