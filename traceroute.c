#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/ip_icmp.h>
#include "traceroute.h" 

// Function to calculate the checksum for ICMP and IP headers
unsigned short checksum(void *b, int len) {
    unsigned short *buf = b;
    unsigned int sum = 0;
    unsigned short result;

    for (sum = 0; len > 1; len -= 2)
        sum += *buf++;
    
    if (len == 1)
        sum += *(unsigned char *)buf;

    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);

    result = ~sum;
    return result;
}

// Traceroute function that sends ICMP Echo Requests with increasing TTL (Time To Live)
void traceroute(const char *destination) {
    int sockfd;                  // Socket file descriptor
    struct sockaddr_in dest_addr; // Destination address structure

    if ((sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;

    if (inet_pton(AF_INET, destination, &dest_addr.sin_addr) <= 0) {
        perror("Invalid destination address");
        exit(EXIT_FAILURE);
    }

    printf("Traceroute to %s, 30 hops max\n", destination);

    for (int ttl = 1; ttl <= MAX_TTL; ttl++) {
        setsockopt(sockfd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));

        int received_replies = 0;
        char hop_ip[INET_ADDRSTRLEN] = {0};
        printf("%2d ", ttl);

        for (int i = 0; i < 3; i++) {
            struct icmp_packet packet;
            memset(&packet, 0, sizeof(packet));

            packet.hdr.type = ICMP_ECHO;
            packet.hdr.un.echo.id = getpid();
            packet.hdr.un.echo.sequence = ttl * 3 + i;
            packet.hdr.checksum = checksum(&packet, sizeof(packet));

            struct timeval start, end;
            gettimeofday(&start, NULL);

            if (sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) <= 0) {
                perror("sendto failed");
                printf("* ");
                continue;
            }

            fd_set fds;
            struct timeval timeout;
            FD_ZERO(&fds);
            FD_SET(sockfd, &fds);
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;

            int ready = select(sockfd + 1, &fds, NULL, NULL, &timeout);
            if (ready > 0) {
                char buffer[1024];
                struct sockaddr_in reply_addr;
                socklen_t reply_len = sizeof(reply_addr);

                if (recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&reply_addr, &reply_len) > 0) {
                    gettimeofday(&end, NULL);
                    double elapsed_time = (end.tv_sec - start.tv_sec) * 1000.0;
                    elapsed_time += (end.tv_usec - start.tv_usec) / 1000.0;

                    inet_ntop(AF_INET, &reply_addr.sin_addr, hop_ip, sizeof(hop_ip));

                    if (i == 0) {
                        printf("%s ", hop_ip);
                    }

                    printf("%.3fms ", elapsed_time);

                    received_replies++;

                    if (reply_addr.sin_addr.s_addr == dest_addr.sin_addr.s_addr && i == 2) {
                        printf("\nReached destination: %s\n", destination);
                        close(sockfd);
                        return;
                    }
                }
            } else {
                printf("* ");
            }
        }
        printf("\n");
    }

    close(sockfd);
    printf("Destination unreachable.\n");
}

int main(int argc, char *argv[]) {
    if (argc != 3 || strcmp(argv[1], "-a") != 0) {
        fprintf(stderr, "Usage: %s -a <destination>\n", argv[0]);
        return 1;
    }

    traceroute(argv[2]);
    
    return 0;
}