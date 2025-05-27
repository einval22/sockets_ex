#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h> // for select and tval
#include <fcntl.h>
#include <linux/if_link.h>
//#include <sys/socket.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define MAX_CLIENTS 30
#define BUFFER_SIZE 1024


void set_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        exit(EXIT_FAILURE);
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
        exit(EXIT_FAILURE);
    }
}

int main() {
    int listener_fd, new_socket_fd;
    int client_sockets[MAX_CLIENTS] = {-1};
    int max_sd, activity, i, valread;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];
    int opt = 1;

    for (i = 0; i < MAX_CLIENTS; i++) {
        printf("clien sock val=%d\n", client_sockets[i]);
    }

    if ((listener_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    set_non_blocking(listener_fd);

    if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(listener_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    printf("Listener on port %d \n", PORT);

    if (listen(listener_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // set of fds for select
    fd_set read_fds;

    printf("Waiting for connections ...\n");

    while (1) {
        // clear set
        FD_ZERO(&read_fds);

        // add listener to monitored set
        FD_SET(listener_fd, &read_fds);
        max_sd = listener_fd;

        // add connection socks
        for (i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_sockets[i];
            if (sd > 0) {
                FD_SET(sd, &read_fds);
            }
            if (sd > max_sd) {
                max_sd = sd;
            }
        }

        // wait for the event on socket
        // add tval for timeout
        activity = select(max_sd + 1, &read_fds, NULL, NULL, NULL);

        if ((activity < 0) && (errno != EINTR)) {
            perror("select error");
        }

        // get event on listener, new client !
        if (FD_ISSET(listener_fd, &read_fds)) {
            // accept will not block as listener is non blocking and we know that there is a new client
            if ((new_socket_fd = accept(listener_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                if (errno != EWOULDBLOCK || errno != EAGAIN) {
                    perror("accept is failed: %s (%d)\n", strerror(errno), errno);
                }
            } else {
                printf("New connection, socket fd is %d, ip is : %s, port : %d\n",
                       new_socket_fd, inet_ntoa(address.sin_addr), ntohs(address.sin_port));

                set_non_blocking(new_socket_fd);

                // add the new sock to the monitored array
                for (i = 0; i < MAX_CLIENTS; i++) {
                    if (client_sockets[i] == -1) {
                        client_sockets[i] = new_socket_fd;
                        printf("Adding to list of sockets as %d\n", i);
                        break;
                    }
                }
                if (i == MAX_CLIENTS) {
                     printf("Too many clients, closing new connection.\n");
                     close(new_socket_fd);
                }
            }
        }

        // check activity on client sockets
        for (i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_sockets[i];
            if (sd > 0 && FD_ISSET(sd, &read_fds)) {
                memset(buffer, 0, BUFFER_SIZE);
                valread = read(sd, buffer, BUFFER_SIZE - 1);

                if (valread > 0) {
                    buffer[valread] = '\0';
                    printf("Client %d sent: %s\n", sd, buffer);
                    // send(sd, response_buffer, strlen(response_buffer), 0);
                } else if (valread == 0) {
                    getpeername(sd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
                    printf("Host disconnected, ip %s, port %d, socket_fd %d\n",
                           inet_ntoa(address.sin_addr), ntohs(address.sin_port), sd);
                    close(sd);
                    client_sockets[i] = 0; 
                } else { // valread < 0
                    if (errno == EWOULDBLOCK || errno == EAGAIN) {
                        // printf("No data right now for sd %d\n", sd);
                    } else {
                        perror("read error");
                        close(sd);
                        client_sockets[i] = 0; //
                    }
                }
            }
        }
    }
    return 0;
}
