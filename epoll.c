#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_EVENTS 1024
#define LISTEN_PORT 8080
#define BUFFER_SIZE 1024

int make_socket_non_blocking(int sfd) {
	int flags = fcntl(sfd, F_GETFL, 0);

	if (flags == -1) {
		perror("fcntl(F_GETFL)");
		return -1;
	}
	flags |= O_NONBLOCK;

	if (fcntl(sfd, F_SETFL, flags) == -1) {
		perror("fcntl(F_SETFL)");
		return -1;
	}

	return 0;
}

int main() {
	int listen_sock, conn_sock, epoll_fd;
	struct sockaddr_in server_addr = {0};
	struct sockaddr_in client_addr = {0};
	socklen_t client_addr_len = sizeof(client_addr);
	struct epoll_event event;
	struct epoll_event events[MAX_EVENTS]; // ready list
	char buffer[BUFFER_SIZE];
	int opt = 1;

	// Create listener
	listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock == -1) {
		perror("socket");
		exit(EXIT_FAILURE);
	}
	
	if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
		perror("setsockopt");
		close(listen_sock);
		exit(EXIT_FAILURE);
	}

	if (make_socket_non_blocking(listen_sock) < 0) {
		close(listen_sock);
		exit(EXIT_FAILURE);
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(LISTEN_PORT);

	if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		perror("bind");
		close(listen_sock);
		exit(EXIT_FAILURE);
	}

	if (listen(listen_sock, SOMAXCONN) < 0) {
		perror("listen");
		close(listen_sock);
		exit(EXIT_FAILURE);
	}

	printf("Server is listening on port %d...\n", LISTEN_PORT);

	epoll_fd = epoll_create1(0); // 0 - no flags
	if (epoll_fd < 0) {
		perror("epoll_create1");
		close(listen_sock);
		exit(EXIT_FAILURE);
	}

	// add listener to epoll
	event.data.fd = listen_sock;
	event.events = EPOLLIN; // accepted clients

	// listen_sock + event.data.fd form the key to find fd in interested list	    
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &event) < 0) {
		perror("epoll_ctl: listen_sock");
		close(listen_sock);
		close(epoll_fd);
		exit(EXIT_FAILURE);
	}

	while (1) {
		int n_events;
		int i;

		n_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1); // put to sleep until event as we do not perform smth usefull except receiving and echoing data
		if (n_events == -1) {
			if (errno == EINTR) { // interrupted by signal 
				continue; // TODO: add some handling or epoll_pwait()
			}
			perror("epoll_wait");
			break;
		}

		for (i = 0; i < n_events; ++i) {
			// check listen sock, non block + LT
			if (events[i].data.fd == listen_sock) {
				while (1) {
					conn_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_addr_len);
					if (conn_sock < 0) {
						if (errno == EAGAIN || errno == EWOULDBLOCK) {
							break;
						} else {
							perror("accept");
							break;
						}
					}

					printf("New connect from %s:%d (created fd: %d)\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), conn_sock);

					// set as non-blocking client sock
					if (make_socket_non_blocking(conn_sock) <0 -1) {
						close(conn_sock);
						continue;
					}

					// add client socket into epoll
					event.data.fd = conn_sock;
					event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
					if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_sock, &event) == -1) {
						perror("epoll_ctl: conn_sock");
						close(conn_sock);
					}
				}
			} else {
				
				int client_fd = events[i].data.fd;

				/* first read EPOLLIN, even if EPOLLHUP/EPOLLRDHUP we still can have data in recv buffer and we need to write something (EPOLLRDHUP case) */
				if (events[i].events & EPOLLIN) {
					ssize_t read_bytes;
					ssize_t write_bytes;

					read_bytes = read(client_fd, buffer, sizeof(buffer) -1);
					if (read_bytes > 0) {
						buffer[read_bytes] = '\0';
						printf("Read from client (fd: %d): %s\n", client_fd, buffer);
						if (events[i].events & EPOLLOUT) {
							write_bytes = write(client_fd, buffer, read_bytes);
							if ((write_bytes > 0) && (write_bytes < read_bytes)) {
								write_bytes = write(client_fd, buffer + write_bytes, read_bytes - write_bytes);
							}
							if (write_bytes < 0) {
								if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
									perror("write");
									epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
									close(client_fd);
									break;
								}
							}
						}
					} else if (read_bytes == 0) {
						// (EOF) EPOLLHUP will be set as well
						printf("Client closed his end (fd: %d)\n", client_fd);
						//epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
						//close(client_fd);
					} else if (read_bytes < 0) {
						/* fatal error */
						if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
						    perror("read");
						    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
						    close(client_fd);
						}
						printf("Wait for client %d\n", client_fd);
					}
				}
				// check client sockets for read or closing
				if (events[i].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
					// err or closed by client
					printf("Client (fd: %d) has disconnected.\n", client_fd);
					epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
					close(client_fd);
				}
			}
		}
	}

	
	printf("Finishing...\n");
	close(listen_sock);
	close(epoll_fd);

    return EXIT_SUCCESS;
}
