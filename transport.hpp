#ifndef TRANSPORT_HPP
#define TRANSPORT_HPP

#include <netinet/in.h>
#include <stdio.h>

#define BUFFER_LENGTH 1000
#define SWS 1000

/* status values */
#define NOT_SENT 0
#define SENT 1
#define ACK 2

/* time values */
#define RECEIVE_TIME_S 0
#define RECEIVE_TIME_US 500000

struct Segment {
    uint8_t buffer[BUFFER_LENGTH];
    ssize_t buffer_size;
    int status;
    uint32_t segment_number;
};

struct Info {
    int sockfd;
    struct in_addr ip;
    uint16_t port;
    FILE * fp;
    uint32_t size;
    struct Segment Window[SWS + 1];
    uint16_t first_segment;
    uint32_t LAR;
    uint32_t all_segments;
};

void read_configuration(int argc, char * argv[], struct Info * info);
void create_socket(struct Info * info);
void send_window(struct Info * info);
void receive_segments(struct Info * info);
void move_window(struct Info * info);
#endif // TRANSPORT_HPP