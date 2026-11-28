#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <netinet/in.h>

void set_port(struct sockaddr_in *address, unsigned short port)
{
    address->sin_port = port;
}
