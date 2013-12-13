/* cs352proxy.h
 * Project 1 - Internet Technology - Fall 2013
 * Jan Racoma
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
#include <net/if_arp.h>
#include "utlist.h"
#include "uthash.h"

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
#define PACKET_ETHADDR 0xFFFF

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

/* Struct for peer list */
struct peerList {
  char tapDevice[MAXBUFFSIZE];
  int net_fd;
  struct in_addr listenIP;
  uint16_t listenPort;
  struct sockaddr ethMAC;
  UT_hash_handle hh;
}__attribute__((packed));

/* Struct for data packet information */
struct dataPacket {
  struct packetHeader *header;
  char data[MAXBUFFSIZE];
}__attribute__((packed));

/* Struct for link state */
struct linkState {
  struct in_addr listenIP;
  uint16_t listenPort;
  struct sockaddr ethMAC;
}__attribute__((packed));

/* Struct for link state packet information */
struct linkStatePacket {
  struct packetHeader *header;
  struct peerList *source;
  uint16_t neighbors;
}__attribute__((packed));

/* Struct for link state record information */
struct linkStateRecord {
  struct timeval uniqueID;
  struct peerList *proxy1;
  struct peerList *proxy2;
  uint32_t linkWeight;
  UT_hash_handle hh;
}__attribute__((packed));

int allocate_tunnel(char *dev, int flags);
int getIP(char *host, char *ip);
int initLocalParams();
int parseInput(int argc, char *argv[]);
void *handle_listen(void *temp);
void server(int port);
void *handle_tap();
void *connectToPeer(void *temp);
char *send_peerList(struct peerList *ls);
void send_singleLinkStatePacket(int new_fd, struct peerList *peer);
void send_linkStatePacket(struct linkStatePacket *lsp);
struct linkStateRecord *create_linkStateRecord(struct peerList *proxy1, struct peerList *proxy2);
void print_packetHeader(struct packetHeader *pkt);
void print_peer(struct peerList *peer);
void print_peerList();
void print_linkState(struct peerList *ls);
void print_linkStatePacket();
void print_linkStateRecord(struct linkStateRecord *record);
void print_linkStateRecords();
int add_peer(struct peerList *peer);
int add_record(struct linkStateRecord *record);
void decode_linkStatePacket(char *buffer, int in_fd);
void decode_linkStateRecord(char *buffer);
void readMAC(char *buffer, struct peerList *pl);
void *sleeper();
