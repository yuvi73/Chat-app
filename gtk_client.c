#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <gdk/gdkkeysyms.h>

#define PORT 8888
#define BUFFER_SIZE 1024
#define SERVER_IP "127.0.0.1"

// GUI Components
GtkWidget *window;
GtkWidget *chat_view;
GtkTextBuffer *chat_buffer;
GtkWidget *entry;
GtkWidget *send_button;
GtkWidget *room_combo;
GtkWidget *user_list;
GtkListStore *user_store;

// Global variables
int sock = 0;
volatile int connected = 0;
pthread_t receive_thread;
char current_room[32] = "General";
char username[32] = "";

// Forward declarations
void append_text_to_chat(const char *text, const char *tag);

// Function to append text to the chat view
void append_text_to_chat(const char *text, const char *tag) {
    GtkTextIter iter;
    
    // Get end iterator and insert text
    gtk_text_buffer_get_end_iter(chat_buffer, &iter);
    
    // Apply tag if provided
    if (tag != NULL) {
        gtk_text_buffer_insert_with_tags_by_name(chat_buffer, &iter, text, -1, tag, NULL);
    } else {
        gtk_text_buffer_insert(chat_buffer, &iter, text, -1);
    }
    
    // Scroll to the end
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(
        gtk_widget_get_parent(chat_view)));
    gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj) - 
                             gtk_adjustment_get_page_size(adj));
}

// Parse and update the user list
gboolean update_user_list(gchar *msg) {
    // Clear the current list
    gtk_list_store_clear(user_store);
    
    // Skip the header part "Server: Users in room 'xxx':"
    const char *users_start = strstr(msg, ":\n");
    if (users_start == NULL) {
        g_free(msg);
        return FALSE;
    }
    
    users_start += 2; // Skip the ":\n"
    
    char *users_copy = strdup(users_start);
    char *line = strtok(users_copy, "\n");
    
    while (line != NULL) {
        if (strncmp(line, "- ", 2) == 0) {
            // Skip the "- " prefix
            char *username = line + 2;
            
            // Add to the list store
            GtkTreeIter iter;
            gtk_list_store_append(user_store, &iter);
            gtk_list_store_set(user_store, &iter, 0, username, -1);
        }
        line = strtok(NULL, "\n");
    }
    
    free(users_copy);
    g_free(msg);
    return FALSE;
}

// Function to update chat with server messages
gboolean update_chat(gchar *text) {
    // Check if this is a server message
    const char *tag = (strncmp(text, "Server:", 7) == 0) ? "server" : NULL;
    
    // Append to chat
    append_text_to_chat(text, tag);
    g_free(text);
    return FALSE;
}

// Function to receive messages from server
void *receive_messages(void *socket_desc) {
    int sock = *(int *)socket_desc;
    char buffer[BUFFER_SIZE];
    
    while (connected) {
        memset(buffer, 0, BUFFER_SIZE);
        int valread = read(sock, buffer, BUFFER_SIZE);
        
        if (valread <= 0) {
            // Server disconnected or error
            gdk_threads_add_idle((GSourceFunc)gtk_main_quit, NULL);
            connected = 0;
            break;
        }
        
        // Check for user list update
        if (strstr(buffer, "Server: Users in room") != NULL) {
            gdk_threads_add_idle_full(G_PRIORITY_DEFAULT_IDLE, 
                                   (GSourceFunc)update_user_list, 
                                   g_strdup(buffer), NULL);
        }
        
        // Update the chat view from the main thread
        gdk_threads_add_idle_full(G_PRIORITY_DEFAULT_IDLE, 
                               (GSourceFunc)update_chat, 
                               g_strdup(buffer), NULL);
    }
    
    return NULL;
}

// Callback for the send button
void on_send_clicked(GtkWidget *widget, gpointer data) {
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));
    
    if (strlen(text) > 0 && connected) {
        char buffer[BUFFER_SIZE];
        
        // Copy text to buffer
        strncpy(buffer, text, BUFFER_SIZE - 2); // Leave space for newline and null terminator
        buffer[BUFFER_SIZE - 2] = '\0';
        
        // Add newline for server
        strcat(buffer, "\n");
        
        // Send message
        if (send(sock, buffer, strlen(buffer), 0) < 0) {
            append_text_to_chat("Error: Failed to send message\n", "server");
        }
        
        // Clear entry
        gtk_entry_set_text(GTK_ENTRY(entry), "");
    }
}

// Handle room change
void on_room_changed(GtkComboBox *widget, gpointer user_data) {
    gchar *room = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));
    
    if (room != NULL && strcmp(room, current_room) != 0 && connected) {
        char join_cmd[BUFFER_SIZE];
        snprintf(join_cmd, BUFFER_SIZE, "/join %s\n", room);
        
        // Save the new room
        strncpy(current_room, room, sizeof(current_room) - 1);
        current_room[sizeof(current_room) - 1] = '\0';
        
        // Send command to server
        if (send(sock, join_cmd, strlen(join_cmd), 0) < 0) {
            append_text_to_chat("Error: Failed to join room\n", "server");
        }
        
        // Free memory
        g_free(room);
    } else if (room != NULL) {
        g_free(room);
    }
}

// Callback for key press in entry field
gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    if (event->keyval == GDK_KEY_Return) {
        on_send_clicked(NULL, NULL);
        return TRUE;
    }
    return FALSE;
}

// Clean up resources before exit
void cleanup_resources() {
    if (connected) {
        // Send logout message if needed
        // send(sock, "/quit\n", 6, 0);
        
        close(sock);
        connected = 0;
        
        // Cancel and wait for the thread to finish
        pthread_cancel(receive_thread);
        pthread_join(receive_thread, NULL);
    }
}

// Handle window closing
gboolean on_window_closed(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    cleanup_resources();
    gtk_main_quit();
    return FALSE;
}

// Initialize chat rooms
void populate_room_combo() {
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(room_combo), "General");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(room_combo), "Tech");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(room_combo), "Gaming");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(room_combo), "Music");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(room_combo), "Movies");
    
    // Set active room
    gtk_combo_box_set_active(GTK_COMBO_BOX(room_combo), 0);
}

// Create a private message dialog
void on_private_message(GtkWidget *widget, gpointer user_data) {
    GtkTreeIter iter;
    GtkTreeModel *model;
    
    if (!connected) {
        append_text_to_chat("Error: Not connected to server\n", "server");
        return;
    }
    
    // Get selected user
    if (gtk_tree_selection_get_selected(GTK_TREE_SELECTION(user_data), &model, &iter)) {
        gchar *selected_user;
        gtk_tree_model_get(model, &iter, 0, &selected_user, -1);
        
        // Create dialog for PM
        GtkWidget *dialog = gtk_dialog_new_with_buttons(
            "Send Private Message",
            GTK_WINDOW(window),
            GTK_DIALOG_MODAL,
            "_Cancel", GTK_RESPONSE_CANCEL,
            "_Send", GTK_RESPONSE_ACCEPT,
            NULL);
            
        // Create content area
        GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);
        
        // Create label
        char label_text[100];
        snprintf(label_text, sizeof(label_text), "Message to %s:", selected_user);
        GtkWidget *label = gtk_label_new(label_text);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_container_add(GTK_CONTAINER(content_area), label);
        
        // Create text entry
        GtkWidget *pm_entry = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(pm_entry), BUFFER_SIZE - 50);
        gtk_container_add(GTK_CONTAINER(content_area), pm_entry);
        
        // Show all widgets
        gtk_widget_show_all(dialog);
        
        // Run the dialog
        gint result = gtk_dialog_run(GTK_DIALOG(dialog));
        
        if (result == GTK_RESPONSE_ACCEPT) {
            const gchar *message = gtk_entry_get_text(GTK_ENTRY(pm_entry));
            
            if (strlen(message) > 0) {
                char pm_cmd[BUFFER_SIZE];
                snprintf(pm_cmd, BUFFER_SIZE, "/pm %s %s\n", selected_user, message);
                
                // Send to server
                if (send(sock, pm_cmd, strlen(pm_cmd), 0) < 0) {
                    append_text_to_chat("Error: Failed to send private message\n", "server");
                }
            }
        }
        
        g_free(selected_user);
        gtk_widget_destroy(dialog);
    } else {
        append_text_to_chat("Please select a user from the list first\n", "server");
    }
}

// Get username from user
int get_username() {
    GtkWidget *dialog, *content_area, *username_entry, *label;
    gint result;
    
    // Create dialog
    dialog = gtk_dialog_new_with_buttons(
        "Enter Username",
        NULL,
        GTK_DIALOG_MODAL,
        "_OK", GTK_RESPONSE_ACCEPT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        NULL);
    
    // Set dialog size
    gtk_window_set_default_size(GTK_WINDOW(dialog), 300, 100);
    
    // Get content area
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);
    
    // Add username label and entry
    label = gtk_label_new("Enter your username:");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_container_add(GTK_CONTAINER(content_area), label);
    
    username_entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(username_entry), 31);
    gtk_container_add(GTK_CONTAINER(content_area), username_entry);
    
    // Show dialog
    gtk_widget_show_all(dialog);
    
    // Run dialog
    result = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (result == GTK_RESPONSE_ACCEPT) {
        const gchar *entered_username = gtk_entry_get_text(GTK_ENTRY(username_entry));
        
        if (strlen(entered_username) > 0) {
            strncpy(username, entered_username, sizeof(username) - 1);
            username[sizeof(username) - 1] = '\0';
            gtk_widget_destroy(dialog);
            return 1;
        }
    }
    
    gtk_widget_destroy(dialog);
    return 0;
}

// Create help dialog
void show_help_dialog() {
    GtkWidget *dialog, *content_area, *label;
    
    // Create dialog
    dialog = gtk_dialog_new_with_buttons(
        "Chat Help",
        GTK_WINDOW(window),
        GTK_DIALOG_MODAL,
        "_OK", GTK_RESPONSE_ACCEPT,
        NULL);
    
    // Set dialog size
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 300);
    
    // Get content area
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);
    
    // Create label with help text
    label = gtk_label_new(
        "Available Commands:\n\n"
        "/private <username> <message> - Send private message\n"
        "/pm <username> <message> - Alias for private\n"
        "/join <room> - Join a chat room\n"
        "/rooms - List available rooms\n"
        "/users - List users in current room\n"
        "/help - Show server help message\n\n"
        "You can also select a user from the list and click 'Private Message'"
    );
    
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_container_add(GTK_CONTAINER(content_area), label);
    
    // Show dialog
    gtk_widget_show_all(dialog);
    
    // Run dialog
    gtk_dialog_run(GTK_DIALOG(dialog));
    
    // Destroy dialog
    gtk_widget_destroy(dialog);
}

// Display error dialog
void show_error_dialog(const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                             GTK_DIALOG_DESTROY_WITH_PARENT,
                             GTK_MESSAGE_ERROR,
                             GTK_BUTTONS_CLOSE,
                             "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// Create UI
void create_ui() {
    // Create window
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Enhanced Chat Client");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);
    g_signal_connect(window, "delete-event", G_CALLBACK(on_window_closed), NULL);
    
    // Create main box (vertical)
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), main_box);
    
    // Create top box (horizontal) for room selection
    GtkWidget *top_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(main_box), top_box, FALSE, FALSE, 0);
    
    // Add room label
    GtkWidget *room_label = gtk_label_new("Room:");
    gtk_box_pack_start(GTK_BOX(top_box), room_label, FALSE, FALSE, 5);
    
    // Add room combobox
    room_combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(top_box), room_combo, TRUE, TRUE, 0);
    g_signal_connect(room_combo, "changed", G_CALLBACK(on_room_changed), NULL);
    
    // Add help button
    GtkWidget *help_button = gtk_button_new_with_label("Help");
    gtk_box_pack_end(GTK_BOX(top_box), help_button, FALSE, FALSE, 0);
    g_signal_connect(help_button, "clicked", G_CALLBACK(show_help_dialog), NULL);
    
    // Create middle box (horizontal) for chat and users
    GtkWidget *middle_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(main_box), middle_box, TRUE, TRUE, 0);
    
    // Create scrolled window for chat view
    GtkWidget *chat_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(chat_scroll),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(middle_box), chat_scroll, TRUE, TRUE, 0);
    
    // Create chat view (text view)
    chat_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(chat_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(chat_view), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(chat_scroll), chat_view);
    
    // Get buffer and add tags
    chat_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat_view));
    gtk_text_buffer_create_tag(chat_buffer, "server", "foreground", "blue", NULL);
    
    // Create user list section (vertical box)
    GtkWidget *user_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(middle_box), user_box, FALSE, FALSE, 0);
    
    // Add user list label
    GtkWidget *user_label = gtk_label_new("Users in Room:");
    gtk_widget_set_halign(user_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(user_box), user_label, FALSE, FALSE, 0);
    
    // Create scrolled window for user list
    GtkWidget *user_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(user_scroll),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(user_scroll, 150, -1);
    gtk_box_pack_start(GTK_BOX(user_box), user_scroll, TRUE, TRUE, 0);
    
    // Create user list view
    user_store = gtk_list_store_new(1, G_TYPE_STRING);
    user_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(user_store));
    gtk_container_add(GTK_CONTAINER(user_scroll), user_list);
    
    // Add column to user list
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("Username", 
                                                                        renderer, 
                                                                        "text", 0, 
                                                                        NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(user_list), column);
    
    // Create private message button
    GtkWidget *pm_button = gtk_button_new_with_label("Private Message");
    gtk_box_pack_start(GTK_BOX(user_box), pm_button, FALSE, FALSE, 0);
    
    // Connect PM button to tree selection
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(user_list));
    g_signal_connect(pm_button, "clicked", G_CALLBACK(on_private_message), selection);
    
    // Create bottom box (horizontal) for message entry
    GtkWidget *bottom_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(main_box), bottom_box, FALSE, FALSE, 0);
    
    // Create message entry
    entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(entry), BUFFER_SIZE - 10);
    gtk_box_pack_start(GTK_BOX(bottom_box), entry, TRUE, TRUE, 0);
    g_signal_connect(entry, "key-press-event", G_CALLBACK(on_key_press), NULL);
    
    // Create send button
    send_button = gtk_button_new_with_label("Send");
    gtk_box_pack_start(GTK_BOX(bottom_box), send_button, FALSE, FALSE, 0);
    g_signal_connect(send_button, "clicked", G_CALLBACK(on_send_clicked), NULL);
    
    // Show all widgets
    gtk_widget_show_all(window);
}

// Signal handler for SIGINT (Ctrl+C)
void sigint_handler(int sig) {
    cleanup_resources();
    exit(0);
}

int main(int argc, char *argv[]) {
    struct sockaddr_in serv_addr;
    
    // Set up signal handler for Ctrl+C
    signal(SIGINT, sigint_handler);
    
    // Initialize GTK
    gtk_init(&argc, &argv);
    
    // Get username first
    if (!get_username()) {
        return 1;
    }
    
    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        return 1;
    }
    
    // Set up server address structure
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    
    // Convert IPv4 address from text to binary form
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        return 1;
    }
    
    // Create UI
    create_ui();
    
    // Populate room dropdown
    populate_room_combo();
    
    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        char error_msg[100];
        snprintf(error_msg, sizeof(error_msg), "Failed to connect to server at %s:%d", SERVER_IP, PORT);
        show_error_dialog(error_msg);
        return 1;
    }
    
    connected = 1;
    append_text_to_chat("Connected to server\n", "server");
    
    // Send username
    if (send(sock, username, strlen(username), 0) < 0 || send(sock, "\n", 1, 0) < 0) {
        append_text_to_chat("Error: Failed to send username\n", "server");
        close(sock);
        connected = 0;
        return 1;
    }
    
    // Create thread to receive messages
    if (pthread_create(&receive_thread, NULL, receive_messages, (void *)&sock) < 0) {
        perror("Thread creation failed");
        close(sock);
        connected = 0;
        return 1;
    }
    
    // Request user list
    if (send(sock, "/users\n", 7, 0) < 0) {
        append_text_to_chat("Error: Failed to request user list\n", "server");
    }
    
    // Start GTK main loop
    gtk_main();
    
    // Clean up
    cleanup_resources();
    
    return 0;
}