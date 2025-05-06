#include "ping.h"

/**
 * Computes the checksum for the provided data.
 * This function is used to calculate the Internet Checksum for ICMP packets.
 *
 * @param b Pointer to the data buffer.
 * @param len Length of the data buffer.
 * @return The computed checksum.
 */
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

/**
 * Sends an ICMP/ICMPv6 echo request and waits for a reply.
 *
 * @param sockfd Socket file descriptor.
 * @param dest_addr IPv4 destination address (NULL for IPv6).
 * @param dest_addr6 IPv6 destination address (NULL for IPv4).
 * @param ttl Time-to-live for the packet.
 * @param seq_num Sequence number for the echo request.
 * @param total_time Accumulator for total round-trip time (RTT).
 * @param received Counter for received packets.
 * @param min_time Pointer to the minimum RTT.
 * @param max_time Pointer to the maximum RTT.
 * @param is_ipv6 Flag indicating whether the destination is IPv6.
 */
void send_ping(int sockfd, struct sockaddr_in *dest_addr, struct sockaddr_in6 *dest_addr6, int ttl, int seq_num,
               double *total_time, int *received, double *min_time, double *max_time, int is_ipv6) {
    struct timeval start, end; // To measure RTT
    struct icmp_packet packet; // ICMP packet for IPv4
    struct icmp6_packet packet6; // ICMPv6 packet for IPv6

    gettimeofday(&start, NULL); // Record the start time

    if (is_ipv6) {
        // Prepare the ICMPv6 packet
        packet6.icmp6_hdr.icmp6_type = ICMP6_ECHO_REQUEST;
        packet6.icmp6_hdr.icmp6_id = getpid();
        packet6.icmp6_hdr.icmp6_seq = seq_num;
        memset(packet6.data, 0, sizeof(packet6.data));
        packet6.icmp6_hdr.icmp6_cksum = 0;
        packet6.icmp6_hdr.icmp6_cksum = checksum(&packet6, sizeof(packet6));

        // Send the ICMPv6 packet
        if (sendto(sockfd, &packet6, sizeof(packet6), 0, (struct sockaddr *)dest_addr6, sizeof(*dest_addr6)) <= 0) {
            perror("sendto failed");
            return;
        }
    } else {
        // Prepare the ICMP packet
        packet.icmp_hdr.type = ICMP_ECHO;
        packet.icmp_hdr.code = 0;
        packet.icmp_hdr.un.echo.id = getpid();
        packet.icmp_hdr.un.echo.sequence = seq_num;
        memset(packet.data, 0, sizeof(packet.data));
        packet.icmp_hdr.checksum = 0;
        packet.icmp_hdr.checksum = checksum(&packet, sizeof(packet));

        // Send the ICMP packet
        if (sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)dest_addr, sizeof(*dest_addr)) <= 0) {
            perror("sendto failed");
            return;
        }
    }

    struct pollfd pfd = {
        .fd = sockfd,
        .events = POLLIN
    };

    // Wait for a reply with a timeout
    int poll_result = poll(&pfd, 1, PING_TIMEOUT * 1000);

    if (poll_result < 0) {
        perror("poll failed");
        return;
    } else if (poll_result == 0) {
        printf("Request timeout for icmp_seq=%d\n", seq_num);
        return;
    }

    // Receive the reply
    socklen_t addr_len = is_ipv6 ? sizeof(*dest_addr6) : sizeof(*dest_addr);
    char buffer[1024];
    int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)(is_ipv6 ? (struct sockaddr *)dest_addr6 : (struct sockaddr *)dest_addr), &addr_len);

    if (n > 0) {
        gettimeofday(&end, NULL); // Record the end time

        double elapsed_time = (end.tv_sec - start.tv_sec) * 1000.0;
        elapsed_time += (end.tv_usec - start.tv_usec) / 1000.0;

        // Update RTT statistics
        if (elapsed_time < *min_time) {
            *min_time = elapsed_time;
        }
        if (elapsed_time > *max_time) {
            *max_time = elapsed_time;
        }

        *total_time += elapsed_time;
        (*received)++;

        // Print reply information
        if (is_ipv6) {
            char ip6_str[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &dest_addr6->sin6_addr, ip6_str, INET6_ADDRSTRLEN);
            printf("%d bytes from %s: icmp_seq=%d time=%.3fms\n",
                   PACKET_SIZE,
                   ip6_str,
                   seq_num,
                   elapsed_time);

        } else {
            struct iphdr *ip_hdr = (struct iphdr *)buffer;
            printf("%d bytes from %s: icmp_seq=%d ttl=%d time=%.3fms\n",
                   PACKET_SIZE,
                   inet_ntoa(dest_addr->sin_addr),
                   seq_num,
                   ip_hdr->ttl,
                   elapsed_time);
        }
    }
}

/**
 * Handles the timeout signal.
 * This function is called when the program times out, and it exits the program gracefully.
 *
 * @param sig Signal number.
 */
void handle_timeout(int sig) {
    printf("Timeout reached. Exiting...\n");
    exit(0);
}

/**
 * Entry point for the ping program.
 * Parses arguments, sets up the socket, and sends ICMP/ICMPv6 echo requests.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit status.
 */
int main(int argc, char *argv[]) {
    if (argc < 5) {
        printf("Usage: %s -a <destination_ip> -t <type> [-c <count>] [-f]\n", argv[0]);
        exit(1);
    }

    double total_rtt = 0, min_time = DBL_MAX, max_time = 0;
    int transmitted = 0, received = 0;
    struct sockaddr_in dest_addr;
    struct sockaddr_in6 dest_addr6;
    int sockfd, count = 4, ttl = 64;
    int is_ipv6 = 0, flood = 0;

    // Set up a timeout handler
    signal(SIGALRM, handle_timeout);
    alarm(PING_TIMEOUT);

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
            if (strchr(argv[i + 1], ':')) {
                is_ipv6 = 1;
                inet_pton(AF_INET6, argv[i + 1], &dest_addr6.sin6_addr);
                dest_addr6.sin6_family = AF_INET6;
                i++;
            } else {
                inet_pton(AF_INET, argv[i + 1], &dest_addr.sin_addr);
                dest_addr.sin_family = AF_INET;
                i++;
            }
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            if (atoi(argv[i + 1]) == 6) {
                is_ipv6 = 1;
            }
            i++;
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            count = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-f") == 0) {
            flood = 1;
        }
    }

    // Create a raw socket
    sockfd = socket(is_ipv6 ? AF_INET6 : AF_INET, SOCK_RAW, is_ipv6 ? IPPROTO_ICMPV6 : IPPROTO_ICMP);
    if (sockfd < 0) {
        perror("socket");
        exit(1);
    }

    // Send ICMP/ICMPv6 packets
    for (int i = 0; i < count; i++) {
        send_ping(sockfd, &dest_addr, &dest_addr6, ttl, i, &total_rtt, &received, &min_time, &max_time, is_ipv6);
        transmitted++;
        if (!flood) {
            sleep(1);
        }
    }

    // Display statistics
    if (received > 0) {
        double mean = total_rtt / received;
        double mdev = 0.0;
        if (received > 1) {
            mdev = sqrt((total_rtt * total_rtt) / received - (mean * mean));
        }

        printf("--- ping statistics ---\n");
        printf("%d packets transmitted, %d received\n", transmitted, received);
        printf("rtt min/avg/max/mdev = %.3f/%.3f/%.3f/%.3fms\n", min_time, mean, max_time, mdev);
    } else {
        printf("No reply received\n");
    }

    close(sockfd);
    return 0;
}
