#include "transport.hpp"

#include <netinet/ip.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>

// Code from THE GNU C LIBRARY: 
// https://www.gnu.org/software/libc/manual/html_node/Calculating-Elapsed-Time.html
int timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y) {
    /* Perform the carry for the later subtraction by updating y. */
    if (x->tv_usec < y->tv_usec) {
        int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
        y->tv_usec -= 1000000 * nsec;
        y->tv_sec += nsec;
    }
    if (x->tv_usec - y->tv_usec > 1000000) {
        int nsec = (x->tv_usec - y->tv_usec) / 1000000;
        y->tv_usec += 1000000 * nsec;
        y->tv_sec -= nsec;
    }

    /* Compute the time remaining to wait.
        tv_usec is certainly positive. */
    result->tv_sec = x->tv_sec - y->tv_sec;
    result->tv_usec = x->tv_usec - y->tv_usec;

    /* Return 1 if result is negative. */
    return x->tv_sec < y->tv_sec;
}


void read_configuration(int argc, char * argv[], struct Info * info) {
    if (argc != 5) {
        fprintf(stderr, "invalid input\n");
        exit(EXIT_FAILURE);
    }
    if (inet_pton(AF_INET, argv[1], &(info->ip)) != 1) {
        fprintf(stderr, "invalid IP address\n");
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[2]);
    if (port < 1 || port > 65535) {
        fprintf(stderr, "invalid port number\n");
        exit(EXIT_FAILURE);
    }
    info->port = port;

    if ((info->fp = fopen(argv[3], "ab")) == NULL) {
        fprintf(stderr, "invalid file name\n");
        exit(EXIT_FAILURE);
    }

    info->size = atoi(argv[4]);
    info->all_segments = (info->size + BUFFER_LENGTH - 1) / BUFFER_LENGTH;
}

void create_socket(struct Info * info) {
    info->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (info->sockfd < 0) {
		fprintf(stderr, "socket error: %s\n", strerror(errno)); 
		exit(EXIT_FAILURE);
	}
    struct sockaddr_in server_address;
	bzero (&server_address, sizeof(server_address));
	server_address.sin_family      = AF_INET;
	server_address.sin_port        = htons(32345);
	server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(info->sockfd,  (struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
        fprintf(stderr, "bind error: %s\n", strerror(errno)); 
		exit(EXIT_FAILURE);
    }
}

int send_datagram(int start, int length, struct Info * info) {
    char message[20];
    sprintf(message, "GET %u %u\n", start, length);

    ssize_t message_len = strlen(message);

    struct sockaddr_in server_address;
	bzero (&server_address, sizeof(server_address));
	server_address.sin_family      = AF_INET;
	server_address.sin_port        = htons(info->port);
	server_address.sin_addr = info->ip;

    if (sendto(info->sockfd, message, message_len, 0, (struct sockaddr*) &server_address, 
        sizeof(server_address)) != message_len) {
		fprintf(stderr, "sendto error: %s\n", strerror(errno)); 
        return EXIT_FAILURE;
	}
    return EXIT_SUCCESS;
}

void send_window(struct Info * info) {
    for (int i = 0; i < SWS; i++) {
        int pos_window = (info->first_segment + i) % SWS;
        int number_segment = info->LAR + i + 1;

        if (number_segment > info->all_segments) {
            break;
        }

        int length = BUFFER_LENGTH;
        if (number_segment == info->all_segments) {
            length = info->size % BUFFER_LENGTH;
            if (length == 0) {
                length = BUFFER_LENGTH;
            }
        }
        info->Window[pos_window].buffer_size = length;
        info->Window[pos_window].segment_number = number_segment;
        int start = (number_segment - 1) * BUFFER_LENGTH;


        if (info->Window[pos_window].status != ACK) {                
            if (send_datagram(start, length, info) == EXIT_SUCCESS) {
                info->Window[pos_window].status = SENT;
            }
        }
    }
}

void analyse_datagram(uint8_t * buffer, struct Info * info) {
    int start, length;

    if (buffer[0] != 'D' || buffer[1] != 'A' || buffer[2] != 'T' || buffer[3] != 'A' || buffer[4] != ' ') {
        return;
    }

    char str[5];
    sscanf((char *)buffer, "%s %d %d", str, &start, &length);
    
    int i_pos = 0;
    while (buffer[i_pos] != '\n') {
        i_pos++;
        if (i_pos > 21) { // error
            return;
        }
    }

    i_pos++;
    
    int segment_number = start / BUFFER_LENGTH + 1;
    if (segment_number <= info->LAR || segment_number > info->LAR + SWS) {
        return;
    }
    int windows_pos = ((segment_number - info->LAR - 1) + info->first_segment) % SWS;
    if (info->Window[windows_pos].status == ACK) {
        return;
    }
    info->Window[windows_pos].status = ACK;
    for (int i = 0; i < info->Window[windows_pos].buffer_size; i++) {
        info->Window[windows_pos].buffer[i] = buffer[i + i_pos];
    }
}

void receive_segments(struct Info * info) {
    fd_set descriptors;
    FD_ZERO(&descriptors);
    FD_SET(info->sockfd, &descriptors);
    struct timeval tv; tv.tv_sec = RECEIVE_TIME_S; tv.tv_usec = RECEIVE_TIME_US;
    struct timeval begin, end;
    int ready = 1;
    gettimeofday(&begin, NULL);
    for (;;) {
        ready = select(info->sockfd + 1, &descriptors, NULL, NULL, &tv);
        if (ready < 0) {
            fprintf(stderr, "select error: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (ready == 0) {
            break;
        }
        for (;;) {
            struct sockaddr_in sender;
            socklen_t sender_len = sizeof(sender);
            uint8_t buffer[IP_MAXPACKET+1];
            ssize_t datagram_len = 
                recvfrom (info->sockfd, buffer, IP_MAXPACKET, 
                    MSG_DONTWAIT, (struct sockaddr*)&sender, &sender_len);
            if (datagram_len == -1) {
                if (errno == EWOULDBLOCK) {
                    break;
                }
                else {
                    fprintf(stderr, "recvfrom error: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
            }
            else if (datagram_len == 0) {
                fprintf(stderr, "recvfrom: connection closed\n");
                exit(EXIT_FAILURE);
            }
            if (sender.sin_addr.s_addr == info->ip.s_addr) {
                analyse_datagram(buffer, info);
            }
        }  
        gettimeofday(&end, 0);
        struct timeval result;
        timeval_subtract(&result, &end, &begin);
        struct timeval tv_new;
        if (timeval_subtract(&tv_new, &tv, &result) == 1) {
            break;
        }
        tv = tv_new;
    }
}

void move_window(struct Info * info) {
    int windows_first = info->first_segment;
    for (int i = 0; i < SWS; i++) {
        int pos = (windows_first + i) % SWS;

        if (info->Window[pos].status != ACK || info->LAR == info->all_segments) {
            break;
        }

        if (fwrite(info->Window[pos].buffer, 1, info->Window[pos].buffer_size, info->fp) != info->Window[pos].buffer_size) {
            fprintf(stderr, "fwrite error: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        info->Window[pos].status = NOT_SENT;
        info->first_segment = (info->first_segment + 1) % SWS;
        info->LAR++;

        if (info->LAR != info->Window[pos].segment_number) {
            printf("ERROR: %d %d\n", info->LAR, info->Window[pos].segment_number);
            exit(EXIT_FAILURE);
        }

        double print_status = ((double)info->LAR / (double)info->all_segments) * 100.0;
        printf("%.3lf%% done\n", print_status);
    }
}