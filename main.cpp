#include "transport.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    struct Info info;
    memset(&info, 0, sizeof(info));

    read_configuration(argc, argv, &info);

    create_socket(&info);
    
    while (info.LAR < info.all_segments) {
        send_window(&info);

        receive_segments(&info);

        move_window(&info);
    }

    fclose(info.fp);
    close(info.sockfd);
}