#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <fcntl.h>

#include <linux/if_link.h>
//#include <sys/socket.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#define PORT 8888
#define BUFF_SIZE 1024


int main(int argc, char **argv) {
	int server_fd, conn_sock_fd;
	struct sockaddr_in address;
	int opt = 1;
	int backlog = 4;
	ssize_t read_bytes, sent_bytes;
	char buffer[BUFF_SIZE] = {0};
	int flags;
	int ret;
	//const char *greeting = "Hello, hello !";
	socklen_t addr_len = sizeof(address);

	// non-blocking socket
	server_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (server_fd < 0) {
		fprintf(stderr, "Failed to open server socket: %s (%d)\n", strerror(errno), errno);
		return -errno;
	}

	/* SO_REUSEADDR bind on TW sockets and multiple IPs - same port (multi-homing),
	 * SO_REUSEPORT bind N listening sockets (balancing especially for UDP) + bind on already bound socket, zero down-time
	 */
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < -1) {
		fprintf(stderr, "Failed to set SO_REUSEADDR and SO_REUSEPORT on fd #%d: %s (%d)\n", server_fd, strerror(errno), errno);
		return -errno;
	}
	memset(&address, 0, sizeof(struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_port = htons(PORT);
	address.sin_addr.s_addr = INADDR_ANY;

	if (bind(server_fd, (struct sockaddr *)&address, addr_len) < -1) {
		fprintf(stderr, "Failed to bind fd #%d: %s (%d)\n", server_fd, strerror(errno), errno);
		return errno;
	}

	if (listen(server_fd, backlog) < 0) {
		fprintf(stderr, "listen() failed on fd #%d: %s (%d), SOMAXCONN=%d\n", server_fd,  strerror(errno), errno, SOMAXCONN);
		return errno;

	}

	fprintf(stdout, "Server has started and listening on port %d\n", PORT);


	while(1) {

		ret = accept(server_fd, (struct sockaddr *)&address, &addr_len);

		if ((ret < 0) && (errno != EAGAIN)) {
			fprintf(stderr, "accept() on port %d failed: %s (%d)\n", PORT, strerror(errno), errno);
			return errno;
		}

		if (ret > 0) {
			conn_sock_fd = ret;
			flags = fcntl(conn_sock_fd, F_GETFD);
			if (flags < 0) {
				fprintf(stderr, "fcntl failed to get flags: %s (%d)\n", strerror(errno), errno);
				return errno;
			}

			flags |= O_NONBLOCK;

			if (fcntl(conn_sock_fd, F_SETFD, flags) < 0) {
				fprintf(stderr, "fcntl failed to set: %s (%d)\n", strerror(errno), errno);
				return errno;
			}
		
		
			fprintf(stdout, "Server accepted new conn on port %d from %s:%d\n", PORT, inet_ntoa(address.sin_addr), ntohs(address.sin_port));

			read_bytes = recvfrom(conn_sock_fd, buffer, BUFF_SIZE, 0, (struct sockaddr *)&address, &addr_len);
			if ((read_bytes < 0) && (errno == EAGAIN))
				continue;
			if (read_bytes < 0) {
				fprintf(stderr, "recv from %s:%d failed: %s (%d)\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port), strerror(errno), errno);
				return errno;
			} else if (read_bytes == 0) {
				fprintf(stderr, "peer socket is closed at  %s:%d\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port));
				return errno;
			}

			fprintf(stderr, "Received %lu bytes %s\n", read_bytes, buffer);

			sent_bytes = sendto(conn_sock_fd, buffer, read_bytes, 0, (struct sockaddr *)&address, addr_len);
			if (sent_bytes < 0) {
				fprintf(stderr, "send failed: sending to  %s:%d: %s (%d)\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port), strerror(errno), errno);
				return errno;
			}

			fprintf(stderr, "Sent %lu bytes %s to %s:%d\n", sent_bytes, buffer, inet_ntoa(address.sin_addr), ntohs(address.sin_port));

			close(conn_sock_fd);
		}
		usleep(100000); // busy-wait
	}

	
	close(server_fd);

	return 0;

}
