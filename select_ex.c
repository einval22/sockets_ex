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

#define PORT 8888
#define MAX_CLIENTS 32
#define BUFFER_SIZE 1024


void set_sock_non_blocking(int fd) {
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
	int listener_fd, conn_sock_fd, conn_fd;
	int client_sockets[MAX_CLIENTS] = {-1}; // init all clients sockets
	int max_fd, activity, i, read_bytes;
	struct sockaddr_in address;
	socklen_t addrlen = sizeof(address);
	char buffer[BUFFER_SIZE];
	int opt = 1;

	memset(client_sockets, -1, sizeof(int)*MAX_CLIENTS);

	listener_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listener_fd < 0) {
		perror("socket() failed");
		exit(EXIT_FAILURE);
	}
	set_sock_non_blocking(listener_fd);

	if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
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
	

	if (listen(listener_fd, 3) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

	printf("Listening on port %d \n", PORT);

	// set of fds for select
	fd_set read_fds;

	printf("Waiting for connections ...\n");

	while (1) {
		// clear set
		FD_ZERO(&read_fds);

		// add listener to monitored set
		FD_SET(listener_fd, &read_fds);
		max_fd = listener_fd;

		// add connection socks
		for (i = 0; i < MAX_CLIENTS; i++) {
			conn_fd = client_sockets[i];
			if (conn_fd > 0) {
				FD_SET(conn_fd, &read_fds);
			}
			if (conn_fd > max_fd) {
				max_fd = conn_fd;
			}
		}

		// wait for the event on socket (timeout NULL = blocking)
		// add tval for timeout
		activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

		if ((activity < 0) && (errno != EINTR)) {
		    perror("select error");
		}

		// get event on listener, new client !
		if (FD_ISSET(listener_fd, &read_fds)) {
			// accept will not block as listener is non blocking and we know that there is a new client
			conn_sock_fd = accept(listener_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
			if (conn_sock_fd < 0) {
				if (errno != EWOULDBLOCK || errno != EAGAIN) {
				    perror("accept is failed\n");
				}
			} else {
				printf("New connection, socket fd is %d, ip is : %s, port : %d\n",
				       conn_sock_fd, inet_ntoa(address.sin_addr), ntohs(address.sin_port));

				set_sock_non_blocking(conn_sock_fd);

				// add the new sock to the monitored array
				for (i = 0; i < MAX_CLIENTS; i++) {
				    if (client_sockets[i] == -1) {
					client_sockets[i] = conn_sock_fd;
					printf("Adding to list of sockets as %d\n", i);
					break;
				    }
				}
				if (i == MAX_CLIENTS) {
				     printf("Too many clients, closing new connection.\n");
				     close(conn_sock_fd);
				}
			}
		}

		// check activity on client sockets
		for (i = 0; i < MAX_CLIENTS; i++) {
			conn_fd = client_sockets[i];
			if (conn_fd > 0 && FD_ISSET(conn_fd, &read_fds)) {
				memset(buffer, 0, BUFFER_SIZE);
				read_bytes = recvfrom(conn_fd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)&address, &addrlen);

				if (read_bytes > 0) {
					buffer[read_bytes] = '\0';
					printf("Client (ip %s, port %d, socket_fd %d) sent: %s\n",
						inet_ntoa(address.sin_addr), ntohs(address.sin_port), conn_fd, buffer);
					// send(sd, response_buffer, strlen(response_buffer), 0);
				} else if (read_bytes == 0) {
					printf("Host disconnected, ip %s, port %d, socket_fd %d\n",
						inet_ntoa(address.sin_addr), ntohs(address.sin_port), conn_fd);
					close(conn_fd);
					client_sockets[i] = -1;
				} else { // read_bytes < 0
					if (errno == EWOULDBLOCK || errno == EAGAIN) {
						printf("No data right now for conn_fd %d\n", conn_fd);
					} else {
						perror("read error");
						close(conn_fd);
						client_sockets[i] = -1; //
					}
				}
			}
		}
	}

	return 0;
}
