#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>

#define PORT "9000"
#define BACKLOG 10
#define DATA_FILE "/var/tmp/aesdsocketdata"


volatile sig_atomic_t caught_sig = 0;

int server_sockfd = -1;


void sig_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        caught_sig = 1; 
        
        if (server_sockfd != -1) {
            shutdown(server_sockfd, SHUT_RDWR);
        }
    }
}


void daemonize() {
    pid_t pid = fork();
    
    if (pid < 0) {
        syslog(LOG_ERR, "Fork failed: %s", strerror(errno));
        exit(-1);
    }
    if (pid > 0) {
        
        exit(0);
    }

    
    if (setsid() < 0) {
        syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
        exit(-1);
    }

    
    if (chdir("/") < 0) {
        syslog(LOG_ERR, "chdir failed: %s", strerror(errno));
        exit(-1);
    }

    
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    
    int fd = open("/dev/null", O_RDWR);
    if (fd != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2) {
            close(fd);
        }
    }
}


void get_ip_str(struct sockaddr_storage *client_addr, char *ip_str, size_t max_len) {
    if (client_addr->ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *)client_addr;
        inet_ntop(AF_INET, &s->sin_addr, ip_str, max_len);
    } else {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)client_addr;
        inet_ntop(AF_INET6, &s->sin6_addr, ip_str, max_len);
    }
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;

    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    
    openlog("aesdsocket", LOG_PID, LOG_USER);
    syslog(LOG_INFO, "Starting aesdsocket");

  
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));     
    sa.sa_handler = sig_handler;    

    
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        syslog(LOG_ERR, "Error registering SIGINT: %s", strerror(errno));
        return -1;
    }

   
    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        syslog(LOG_ERR, "Error registering SIGTERM: %s", strerror(errno));
        return -1;
    }
    
    struct addrinfo hints, *servinfo, *p;
    int yes = 1;
    int rv;

    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;     // Don't care if it's IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
    hints.ai_flags = AI_PASSIVE;     // Automatically fill in my IP for me

    
    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        syslog(LOG_ERR, "getaddrinfo: %s", gai_strerror(rv));
        return -1;
    }

   
    for(p = servinfo; p != NULL; p = p->ai_next) {
        
        if ((server_sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            continue;
        }

        
        if (setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
            freeaddrinfo(servinfo);
            close(server_sockfd);
            return -1;
        }

        
        if (bind(server_sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(server_sockfd);
            continue;
        }
        
        break; 
    }

    freeaddrinfo(servinfo); 

    if (p == NULL) {
        syslog(LOG_ERR, "Failed to bind to port %s", PORT);
        return -1;
    }

    
    if (daemon_mode) {
        syslog(LOG_INFO, "Running in daemon mode");
        daemonize();
    }

    
    if (listen(server_sockfd, BACKLOG) == -1) {
        syslog(LOG_ERR, "Listen failed: %s", strerror(errno));
        close(server_sockfd);
        return -1;
    }
    
    
    struct sockaddr_storage client_addr;
    socklen_t sin_size;
    int client_fd;
    char client_ip[INET6_ADDRSTRLEN];

 
    while (!caught_sig) {
        sin_size = sizeof(client_addr);
        client_fd = accept(server_sockfd, (struct sockaddr *)&client_addr, &sin_size);
        
        if (client_fd == -1) {
           
            if (errno == EINTR) {
                continue; 
            }
            syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            continue;
        }

        
        get_ip_str(&client_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        
        size_t rx_buf_size = 1024;
        char *rx_buffer = malloc(rx_buf_size);
        if (!rx_buffer) {
            syslog(LOG_ERR, "Malloc failed");
            close(client_fd);
            continue;
        }
        
        size_t rx_len = 0;
        int packet_complete = 0;
        ssize_t bytes_received;

        
        while (!packet_complete && (bytes_received = recv(client_fd, rx_buffer + rx_len, rx_buf_size - rx_len - 1, 0)) > 0) {
            rx_len += bytes_received;
            rx_buffer[rx_len] = '\0'; 
            
            
            if (strchr(rx_buffer, '\n') != NULL) {
                packet_complete = 1;
            } else {
                
                rx_buf_size *= 2;
                char *new_buffer = realloc(rx_buffer, rx_buf_size);
                if (!new_buffer) {
                    syslog(LOG_ERR, "Realloc failed");
                    free(rx_buffer);
                    rx_buffer = NULL;
                    break; 
                }
                rx_buffer = new_buffer;
            }
        }

       
        if (packet_complete && rx_buffer) {
    
            int file_fd = open(DATA_FILE, O_CREAT | O_WRONLY | O_APPEND, 0644);
            if (file_fd != -1) {
                write(file_fd, rx_buffer, rx_len);
                close(file_fd);
            } else {
                syslog(LOG_ERR, "Failed to open data file for writing");
            }

          
            file_fd = open(DATA_FILE, O_RDONLY);
            if (file_fd != -1) {
                char tx_buffer[1024];
                ssize_t bytes_read;
                while ((bytes_read = read(file_fd, tx_buffer, sizeof(tx_buffer))) > 0) {
                    send(client_fd, tx_buffer, bytes_read, 0);
                }
                close(file_fd);
            } else {
                syslog(LOG_ERR, "Failed to open data file for reading");
            }
        }

        
        free(rx_buffer);
        close(client_fd);
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

  
    if (server_sockfd != -1) {
        close(server_sockfd);
    }
    
 
    remove(DATA_FILE); 
    
    closelog();
    return 0;
   
}
