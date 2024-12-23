#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include "../aesd-char-driver/aesd_ioctl.h"

#define PORT 9000

#ifdef USE_AESD_CHAR_DEVICE
#define DATA_FILE "/dev/aesdchar"
#else
#define DATA_FILE "/var/tmp/aesdsocketdata"
#endif

volatile sig_atomic_t running = 1;

struct ThreadNode
{
    pthread_t tid;           // Thread ID
    struct ThreadNode *next; // Pointer to the next node
};

// Define a structure to pass necessary arguments to the thread
struct ThreadArgs
{
    int client_socket;
    int server_socket;
};

// Define a mutex
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
struct ThreadNode *head = NULL; // Global variable to store the head of thread linked list

// Function to add a new thread to the linked list
void add_thread(pthread_t tid)
{
    struct ThreadNode *new_node = (struct ThreadNode *)malloc(sizeof(struct ThreadNode));
    if (new_node == NULL)
    {
        perror("Error creating thread node");
        exit(EXIT_FAILURE);
    }
    new_node->tid = tid;
    new_node->next = head;
    head = new_node;
}

// Function to remove a thread from the linked list
void remove_thread(pthread_t tid)
{
    struct ThreadNode *prev = NULL;
    struct ThreadNode *current = head;

    while (current != NULL)
    {
        if (pthread_equal(current->tid, tid))
        {
            if (prev == NULL)
            {
                head = current->next;
            }
            else
            {
                prev->next = current->next;
            }
            free(current);
            break;
        }
        prev = current;
        current = current->next;
    }
}

// Function to join completed threads
void join_threads()
{
    struct ThreadNode *current = head;
    while (current != NULL)
    {
        pthread_join(current->tid, NULL);
        struct ThreadNode *temp = current;
        current = current->next;
        free(temp);
    }
    head = NULL; // Reset the head of the list after joining all threads
}

void signal_handler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM)
    {
        running = 0;
        syslog(LOG_INFO, "Caught signal, exiting");
    }
}

void cleanup(int server_socket)
{
    // Clean up and exit
    syslog(LOG_INFO, "Cleaning up and exiting");

    // Close server socket
    if (shutdown(server_socket, SHUT_RDWR) == -1)
    {
        perror("Error shutting down server socket");
    }

    if (close(server_socket) == -1)
    {
        perror("Error closing server socket");
    }

#ifndef USE_AESD_CHAR_DEVICE
    // Delete the data file
    unlink(DATA_FILE);
#endif

    // Close syslog
    closelog();

    exit(EXIT_SUCCESS);
}

void daemonize()
{
    pid_t pid = fork();

    if (pid < 0)
    {
        perror("Error forking");
        exit(EXIT_FAILURE);
    }

    if (pid > 0)
    {
        // Parent process exits
        exit(EXIT_SUCCESS);
    }

    // Child process continues
    umask(0);
    if (setsid() < 0)
    {
        perror("Error setting session ID");
        exit(EXIT_FAILURE);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

void handle_client(int client_socket, int server_socket)
{
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    if (getpeername(client_socket, (struct sockaddr *)&client_addr, &client_addr_len) == 0)
    {
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);
    }

    char buffer[1024];
    ssize_t bytes_received;

    pthread_mutex_lock(&file_mutex);

    while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0)
    {
        short seekset = 0;
        int data_fd = open(DATA_FILE, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        if (data_fd == -1)
        {
            perror("Error opening data file");
            pthread_mutex_unlock(&file_mutex);
            cleanup(server_socket);
        }

        if (strncmp(buffer, "AESDCHAR_IOCSEEKTO:", 19) == 0)
        {
            unsigned int write_cmd, write_cmd_offset;
            if (sscanf(buffer + 19, "%u,%u", &write_cmd, &write_cmd_offset) == 2)
            {
                struct aesd_seekto seekto;
                seekto.write_cmd = write_cmd;
                seekto.write_cmd_offset = write_cmd_offset;

                // Perform the ioctl seek command
                ssize_t seek_val = ioctl(data_fd, AESDCHAR_IOCSEEKTO, &seekto);
                if (seek_val == -1)
                {
                    perror("ioctl error");
                    pthread_mutex_unlock(&file_mutex);
                    cleanup(server_socket);
                }
                seekset = 1;
            }
            else
            {
                pthread_mutex_unlock(&file_mutex);
                perror("invalid ioctl command");
                cleanup(server_socket);
            }
        }
        else
        {
            write(data_fd, buffer, bytes_received);
        }

        if (memchr(buffer, '\n', bytes_received) != NULL)
        {
            if (seekset == 0)
            {
                if (lseek(data_fd, 0, SEEK_SET) == -1)
                {
                    perror("Error seeking to start of file");
                    close(data_fd);
                    pthread_mutex_unlock(&file_mutex);
                    cleanup(server_socket);
                }
            }
            char file_buffer[1024];
            ssize_t bytes_read;

            while ((bytes_read = read(data_fd, file_buffer, sizeof(file_buffer))) > 0)
            {
                send(client_socket, file_buffer, bytes_read, 0);
            }

            close(data_fd);

            pthread_mutex_unlock(&file_mutex);

            break;
        }
    }

    syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(client_addr.sin_addr));
    close(client_socket);
}

void *client_thread(void *args)
{
    struct ThreadArgs *thread_args = (struct ThreadArgs *)args;
    int client_socket = thread_args->client_socket;
    int server_socket = thread_args->server_socket;

    // Handle the client as before
    handle_client(client_socket, server_socket);

    // Free allocated memory and exit the thread
    remove_thread(pthread_self()); // Remove the completed thread from the linked list
    free(thread_args);
    pthread_exit(NULL);
}

// Function to append timestamp every 10 seconds
void *timestamp_thread(void *args)
{
    struct ThreadArgs *thread_args = (struct ThreadArgs *)args;
    int server_socket = thread_args->server_socket;
    while (running)
    {
        // Get current time
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %T %z\n", tm_info);

        // Append timestamp to the file
        pthread_mutex_lock(&file_mutex);
        int data_fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
        if (data_fd == -1)
        {
            perror("Error opening data file");
            cleanup(server_socket);
        }
        write(data_fd, timestamp, strlen(timestamp));
        close(data_fd);
        pthread_mutex_unlock(&file_mutex);

        // Sleep for 10 seconds
        sleep(10);
    }
    remove_thread(pthread_self()); // Remove the completed thread from the linked list
    free(thread_args);
    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
    int daemon_mode = 0;

    if (argc > 1 && strcmp(argv[1], "-d") == 0)
    {
        daemon_mode = 1;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);
    syslog(LOG_INFO, "Starting aesdsocket");

    // Set up signal handlers using sigaction for better control
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1)
    {
        perror("Error setting up signal handlers");
        exit(EXIT_FAILURE);
    }

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1)
    {
        perror("Error creating socket");
        cleanup(server_socket);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    int reuse = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        perror("setsockopt(SO_REUSEADDR) failed");
        cleanup(server_socket);
    }

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("Error binding socket");
        cleanup(server_socket);
    }

    if (listen(server_socket, 5) == -1)
    {
        perror("Error listening for connections");
        cleanup(server_socket);
    }

    if (daemon_mode)
    {
        daemonize();
    }

    struct ThreadArgs *thread_data_timer = (struct ThreadArgs *)malloc(sizeof(struct ThreadArgs));
    thread_data_timer->server_socket = server_socket;

#ifndef USE_AESD_CHAR_DEVICE
    pthread_t timestamp_tid;
    if (pthread_create(&timestamp_tid, NULL, timestamp_thread, (void *)thread_data_timer) != 0)
    {
        perror("Error creating timestamp thread");
        // Handle error
    }
    add_thread(timestamp_tid); // Add the new thread to the linked list
#endif

    while (running)
    {
        int client_socket = accept(server_socket, NULL, NULL);
        if (client_socket == -1)
        {
            perror("Error accepting connection");
            cleanup(server_socket);
        }

        // handle_client(client_socket, server_socket);

        // Create a new thread to handle the connection
        pthread_t tid;
        struct ThreadArgs *thread_data = (struct ThreadArgs *)malloc(sizeof(struct ThreadArgs));
        thread_data->client_socket = client_socket;
        thread_data->server_socket = server_socket;
        if (pthread_create(&tid, NULL, client_thread, (void *)thread_data) != 0)
        {
            perror("Error creating thread");
            close(client_socket);
            continue;
        }
        add_thread(tid); // Add the new thread to the linked list
    }

    join_threads(); // Join all completed threads before exiting

    cleanup(server_socket); // Cleanup if the loop exits

    return 0;
}
