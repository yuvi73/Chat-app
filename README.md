A feature-rich GTK-based chat client that allows users to communicate in rooms, send private messages, and manage their chat experience through an intuitive graphical interface.
Features

Multiple Chat Rooms: Join different themed chat rooms (General, Tech, Gaming, Music, Movies)
Private Messaging: Send private messages to specific users
User List: View all users currently in your chat room
Command Support: Use chat commands for various actions
Intuitive UI: Clean GTK3-based interface with message history, room selection, and user list

Requirements

GTK+ 3.0 or higher
GCC or compatible C compiler
POSIX-compliant operating system (Linux, macOS, etc.)
Make (for building)
Usage
Getting Started

Launch the application
Enter your username when prompted
The client will connect to the default server (127.0.0.1:8888)
Select a chat room from the dropdown menu at the top
Type messages in the text entry at the bottom and press Enter or click "Send"

Available Commands
You can type these commands in the message entry field:

/join <room> - Join a chat room
/pm <username> <message> - Send a private message
/private <username> <message> - Alternative for sending private messages
/rooms - List available chat rooms
/users - Show users in the current room
/help - Display server help

Sending Private Messages via UI

Select a user from the user list
Click the "Private Message" button
Enter your message in the dialog
Click "Send"

Configuration
The server IP and port are defined in the source code. By default:

Server: 127.0.0.1 (localhost)
Port: 8888

To change these settings, modify the SERVER_IP and PORT constants in the source code and recompile.
Server Requirements
This client is designed to work with a compatible chat server that:

Accepts username on initial connection
Supports room commands (/join, etc.)
Provides user lists in the expected format
Handles private messaging
