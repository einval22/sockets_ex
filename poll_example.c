#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h> 

#define NUM_PIPES 2
#define TIMEOUT_MS 5000

int main() {
	int pipe_fds[NUM_PIPES][2]; // [0] - for read, [1] - for write
	struct pollfd poll_fds[NUM_PIPES];
	char buffer[256] = {0};
	int i, j;
	ssize_t read_bytes;

	// 1. Create two pipes
	for (i = 0; i < NUM_PIPES; ++i) {
		if (pipe(pipe_fds[i]) < 0) {
			perror("pipe");
			return EXIT_FAILURE;
		}
		/* Set as non-blocking, because if read will happen between
		 * poll() and read() by another thread, read() called here can
		 * block.
		 */
		
		int flags = fcntl(pipe_fds[i][0], F_GETFL, 0);
		if (flags == -1) {
			perror("fcntl F_GETFL");
			for (j = 0; j <= i; ++j) {
				close(pipe_fds[j][0]);
				close(pipe_fds[j][1]);
			}
			return EXIT_FAILURE;
		}
		if (fcntl(pipe_fds[i][0], F_SETFL, flags | O_NONBLOCK) == -1) {
			perror("fcntl F_SETFL O_NONBLOCK");
			for (j = 0; j <= i; ++j) {
				close(pipe_fds[j][0]);
				close(pipe_fds[j][1]);
			}
			return EXIT_FAILURE;
		}
	}

	printf("Created two pipes, read fds: %d, %d\n", pipe_fds[0][0], pipe_fds[1][0]);
	printf("Write fds: %d, %d\n", pipe_fds[0][1], pipe_fds[1][1]);
	printf("In another terminal input text 'echo \"my text\" > /proc/%d/fd/%d'\n", getpid(), pipe_fds[0][1]);
	printf("Or 'echo \"my text\" > /proc/%d/fd/%d'\n", getpid(), pipe_fds[1][1]);


	// 2. Fill pollfd for poll() put only read "ends"
	for (i = 0; i < NUM_PIPES; ++i) {
		poll_fds[i].fd = pipe_fds[i][0]; // set read fds
		poll_fds[i].events = POLLIN;     // notify about fds ready to be read
		poll_fds[i].revents = 0;         // init revents, that kernel can write new events
	}

	while (1) {
		printf("Wait for events on pipe for %d\n", TIMEOUT_MS);

		int ret = poll(poll_fds, NUM_PIPES, TIMEOUT_MS);

		if (ret == -1) {
			perror("poll");
			break;
		} else if (ret == 0) {
		    printf("Timeout %d has expired, no events, let's write something\n", TIMEOUT_MS);
		    /* try to read in channel */
		    //if (loop_count == 1) {
		    char* msg1 = "Hello channel 1!";
		    printf("Write to channel 0 ('%s')...\n", msg1);
		    write(pipe_fds[0][1], msg1, strlen(msg1));
		    char* msg2 = "Hello to channel 2!";
		    printf("Write to channel 1 ('%s')...\n", msg2);
		    write(pipe_fds[1][1], msg2, strlen(msg2));
		    

		} else {
			printf("Got event, ret=%d\n", ret);
			// check descriptors for read
			for (i = 0; i < NUM_PIPES; ++i) {
				if (poll_fds[i].revents & POLLIN) {
					printf("There is data to read on fd: %d\n", poll_fds[i].fd);
					read_bytes = read(poll_fds[i].fd, buffer, sizeof(buffer) - 1);
					if ((read_bytes < 0) && ((errno != EAGAIN) || (errno != EWOULDBLOCK))) {
						perror("read() failed");
					} else if (read_bytes == 0) {
						printf("Write end was closed (fd=%d)\n", pipe_fds[i][1]);
						if (poll_fds[i].fd != -1) {
							if (close(poll_fds[i].fd) < 0) { // Close read "end"
								perror("read descriptor is still opened, close() on it failed ");
							}
							poll_fds[i].fd = -1; // set as non-used
						}
						if (pipe_fds[i][1] != -1) {
							if (close(pipe_fds[i][1] < 0)) {
								perror("write descriptor is still opened, close() on it failed ");
							}
							pipe_fds[i][1] = -1; // set as non-used
						}
						
					} else {
						buffer[read_bytes] = '\0';
						printf("Read from channel %d: \"%s\"\n", i, buffer);
					}
				}
			
				if (poll_fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
					printf("Error or close on fd %d. revents: %x\n", poll_fds[i].fd, poll_fds[i].revents);
					if (poll_fds[i].fd != -1) {
						close(poll_fds[i].fd);
						close(pipe_fds[i][1]); 
						poll_fds[i].fd = -1; // set as non-used
						pipe_fds[i][1] = -1;
					}
				}
				poll_fds[i].revents = 0; // reinit revents
			}
		}
	}

	// Close all fds
	printf("Close fds.\n");
	for (i = 0; i < NUM_PIPES; ++i) {
		if(pipe_fds[i][0] != -1)
			close(pipe_fds[i][0]); // Close read end
		if(pipe_fds[i][1] != -1)
			close(pipe_fds[i][1]); // Close write end
	}

	return EXIT_SUCCESS;
}
