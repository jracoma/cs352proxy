/* cs352proxy.h
 * Project 1 - Internet Technology - Fall 2013
 * Jan Racoma, Jonathan Forscher
 * Objective:
 *        To create a VLAN between two nodes, thus creating a tunnel.
 *         Proxy will use two Ethernet devices to implement VLAN; client/server.
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/if_tun.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netdb.h>
#include <linux/if_ether.h>
#include "utlist.h"

/* Macro Definitions */
#define eprintf(...) fprintf(stderr, __VA_ARGS__);
#define MAXBUFFSIZE 2048
#define BACKLOG 10
#define MAXLINESIZE 256
/* Packet Definitions */
#define PACKET_DATA 0xABCD
#define PACKET_LEAVE 0xAB01
#define PACKET_QUIT 0xAB12
#define PACKET_LINKSTATE 0xABAC

/* Ethernet Frame Header */
union ethframe
{
  struct
  {
    struct ethhdr    header;
    unsigned char    data[ETH_DATA_LEN];
  } field;
  unsigned char    buffer[ETH_FRAME_LEN];
};

/* Struct for packet headers */
struct packetHeader {
  uint16_t type;
  uint16_t length;
}__attribute__((packed));

/* Struct for initial peer list */
struct peerList {
  struct in_addr peerIP;
  uint16_t peerPort;
  char *tapDevice;
  struct timeval uniqueID;
  int net_fd;
  pthread_t pid;
  struct peerList *next;
}__attribute__((packed));

/* Struct for data packet information */
struct dataPacket {
  struct packetHeader header;
  char data[MAXBUFFSIZE];
}__attribute__((packed));

/* Struct for link state */
struct linkState {
  struct in_addr listenIP;
  uint16_t listenPort;
  struct sockaddr tapMAC;
  struct sockaddr ethMAC;
}__attribute__((packed));

/* Struct for link state source */
struct linkStateSource {
  struct linkState *ls;
  uint16_t neighbors;
}__attribute__((packed));

/* Struct for link state packet information */
struct linkStatePacket {
  struct packetHeader header;
  struct timeval uniqueID;
  struct linkState proxy1;
  struct linkState proxy2;
  uint32_t linkWeight;
  struct linkStatePacket *next;
}__attribute__((packed));

int allocate_tunnel(char *dev, int flags);
int getIP(char *host, char *ip);
int initLocalParams();
int parseInput(int argc, char *argv[]);
void *handle_listen();
void server(int port);
void *handle_tap();
void *connectToPeer(void *temp);
uint16_t getHeaderInfo(uint16_t *header);
void *sleeper();
