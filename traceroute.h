#ifndef TRACEROUTE_H
#define TRACEROUTE_H

#include <netinet/ip_icmp.h>

#define MAX_TTL 30            // Maximum TTL value (hops) for traceroute
#define PACKET_SIZE 64        // Packet size (ICMP Echo request size)

// ICMP packet structure, which includes the ICMP header and additional data
struct icmp_packet {
    struct icmphdr hdr;      // ICMP header
    char data[PACKET_SIZE - sizeof(struct icmphdr)]; // ICMP data field (additional space for data)
};

// Function declarations
unsigned short checksum(void *b, int len);
void traceroute(const char *destination);

#endif // TRACEROUTE_H
