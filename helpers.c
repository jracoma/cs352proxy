/* helpers.c
 * Project 1 - Internet Technology - Fall 2013
 * Jan Racoma
 * Objective:
 *        To create a VLAN between two nodes, thus creating a tunnel.
 *         Proxy will use two Ethernet devices to implement VLAN; client/server.
 */

#include "helpers.h"

/* Print linkState information */
 void print_linkState(struct linkState *ls) {
 	char ethMAC[19];
 	sprintf(ethMAC, "%02x:%02x:%02x:%02x:%02x:%02x", (unsigned char)ls->ethMAC.sa_data[0], (unsigned char)ls->ethMAC.sa_data[1], (unsigned char)ls->ethMAC.sa_data[2], (unsigned char)ls->ethMAC.sa_data[3], (unsigned char)ls->ethMAC.sa_data[4], (unsigned char)ls->ethMAC.sa_data[5]);
 	printf("---LINKSTATE: listenIP: %s:%d | MAC: %s\n", inet_ntoa(ls->listenIP), ntohs(ls->listenPort), ethMAC);
 }
