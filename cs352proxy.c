/* cs352proxy.c
 * Project 2 - Internet Technology - Fall 2013
 * Jan Racoma
 * Objective: Maintaining link states with multiple peers.
 */

#include "cs352proxy.h"
/* Debug */
 int debug = 1;

/* FDs used for read/write to net and tap */
 int tap_fd, net_fd;

/* Socket FD */
 int sock_fd;

/* Local Parameters */
 int linkPeriod, linkTimeout, quitAfter;
 struct peerList *peers = NULL, *local_info;
 struct linkStateRecord *records = NULL;
 struct linkStatePacket *lsPacket;
 char *dev = "tap10";

/* Threads to handle socket and tap */
 pthread_t sleep_thread, listen_thread, connect_thread, socket_thread, flood_thread, server_thread, timeout_thread;
 pthread_mutex_t peer_mutex = PTHREAD_MUTEX_INITIALIZER, linkstate_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Open a tun/tap and return the fd to read/write back to caller */
 int allocate_tunnel(char *dev, int flags) {
 	int fd, error;
 	struct ifreq ifr;
 	char *device_name = "/dev/net/tun";
 	if((fd = open(device_name , O_RDWR)) < 0 ) {
 		perror("error opening /dev/net/tun");
 		return fd;
 	}
 	memset(&ifr, 0, sizeof(ifr));
 	ifr.ifr_flags = flags;

 	if (*dev) {
 		strncpy(ifr.ifr_name, dev, IFNAMSIZ);
 	}

 	if((error = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0 ) {
 		perror("ioctl on tap failed");
 		close(fd);
 		return error;
 	}

 	// strcpy(dev, ifr.ifr_name);

 	if (debug) printf("TUN FD: %d\n", fd);
 	return fd;
 }

/* Resolve hostname to a.b.c.d address */
 int getIP(char *host, char *ip) {
 	struct hostent *hp;
 	struct in_addr **addr_list;
 	int i;

 	if ((hp = gethostbyname(host)) == NULL) {
 		perror("gethostname");
 		return 1;
 	}

 	addr_list = (struct in_addr **)hp->h_addr_list;
 	for (i = 0; addr_list[i] != NULL; i++) {
 		strcpy(ip, inet_ntoa(*addr_list[i]));
 		return 0;
 	}

 	return 1;
 }

/* Initiliaze local parameters */
 int initLocalParams() {
 	struct ifreq ifr;
 	char buffer[MAXLINESIZE];
 	local_info = (struct peerList *)malloc(sizeof(struct peerList));
 	lsPacket = (struct linkStatePacket *)malloc(sizeof(struct linkStatePacket));
 	lsPacket->header = (struct packetHeader *)malloc(sizeof(struct packetHeader));

	/* Template for local linkStatePacket */
 	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
 	ifr.ifr_addr.sa_family = AF_INET;

  /* Obtain local IP address of eth0 */
 	strncpy(ifr.ifr_name, "eth0", IFNAMSIZ-1);
 	if (ioctl(sock_fd, SIOCGIFADDR, &ifr) < 0) {
 		perror("ioctl(SIOCGIADDR)");
 		return EXIT_FAILURE;
 	}
 	inet_aton((char *)inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr), &local_info->listenIP);

 	sprintf(buffer, "/sys/class/net/%s/address", dev);
 	FILE *f = fopen(buffer, "r");
 	fread(buffer, 1, MAXLINESIZE, f);
 	readMAC(buffer, local_info);
 	fclose(f);

 	lsPacket->header->type = htons(PACKET_LINKSTATE);
 	lsPacket->source = local_info;

 	return 0;
 }

/* Parse through input file */
 int parseInput(int argc, char *argv[]) {
 	FILE *input_file;
	/* Line number of current line being read from input_file */
 	int line_number = 0;
	/* ptr to next field extracted from current line */
 	char *next_field;
	/* ptr to current input  */
 	char line[MAXLINESIZE+1];
	/* Variables for peer information */
 	char *host, *tapDevice, ip[100];
 	struct peerList *current;

	/* Verifies proper syntax command line */
 	if (argc != 2) {
 		puts("Syntax: cs352proxy <input_file>");
 		return EXIT_FAILURE;
 	}

	/* Initialize local parameters */
 	initLocalParams();

	/* Open input file */
 	input_file = fopen(argv[1], "r");
 	if (input_file == NULL) {
 		perror("argv[1]");
 		return EXIT_FAILURE;
 	}

	/* Iterate through the input line one line at a time */
 	while (fgets(line, MAXLINESIZE, input_file)) {
 		line_number++;
 		next_field = strtok(line, " \n");

 		if (!next_field || !strcmp(next_field, "//")) continue;
 		else if (!strcmp(next_field, "listenPort")) local_info->listenPort = atoi(strtok(NULL, " \n"));
 		else if (!strcmp(next_field, "linkPeriod")) linkPeriod = atoi(strtok(NULL, " \n"));
 		else if (!strcmp(next_field, "linkTimeout")) linkTimeout = atoi(strtok(NULL, " \n"));
 		else if (!strcmp(next_field, "quitAfter")) {
 			quitAfter = atoi(strtok(NULL, " \n"));
		 	/* Set quitAfter sleeper */
 			if (pthread_create(&sleep_thread, NULL, sleeper, NULL)) {
 				perror("connect thread");
 				pthread_exit(NULL);
 			}
 		}
 		else if (!strcmp(next_field, "peer")) {
 			current = (struct peerList *)malloc(sizeof(struct peerList));
 			host = strtok(NULL, " \n");

			/* Checks for a.b.c.d address, otherwise resolve hostname */
 			if (inet_addr(host) == -1) {
 				getIP(host, ip);
 				host = ip;
 			}
 			inet_aton(host, &current->listenIP);
 			current->listenPort = atoi(strtok(NULL, " \n"));
 			fgets(line, MAXLINESIZE, input_file);
 			next_field = strtok(line, " \n");
 			tapDevice = strtok(NULL, " \n");
 			strcpy(current->tapDevice, tapDevice);
 			if (pthread_create(&connect_thread, NULL, connectToPeer, (void *)current) != 0) {
 				perror("connect_thread");
 				pthread_exit(NULL);
 			}
 			pthread_join(connect_thread, NULL);
 		}
 	}

 	if (debug) {
 		puts("\n\n\nLocal Information:");
 		print_linkState(local_info);
 		lsPacket->neighbors = HASH_COUNT(peers);
 		printf("Count: %d\n", lsPacket->neighbors);
 		printf("linkPeriod: %d | linkTimeout: %d | quitAfter: %d\n\n\n", linkPeriod, linkTimeout, quitAfter);
 		printf("\n\n\n---FINAL Linked List after parseInput:---\n");
 		pthread_mutex_lock(&peer_mutex);
 		print_peerList();
 		pthread_mutex_unlock(&peer_mutex);
 	}

	/* Close input file */
 	fclose(input_file);
 	return 0;
 }

/* Read from socket and write to tap */
 void *handle_listen(void *temp)
 {
 	struct peerList *peer = (struct peerList *)temp;
 	int size;
 	uint16_t type;
 	char buffer[MAXBUFFSIZE], buffer2[MAXBUFFSIZE];

 	/* Listen for client packets and parse accordingly */
 	printf("Client connected from %s:%d - %d.\n", inet_ntoa(peer->listenIP), peer->listenPort, peer->in_fd);
 	while (1) {
 		if (debug) printf("///Start while loop for %s\n", send_peerList(peer));
 		memset(buffer, 0, MAXBUFFSIZE);
 		size = recv(peer->in_fd, buffer, sizeof(buffer), 0);
 		if (debug) printf("\nSIZE from %s: %d\n", send_peerList(peer), size);
 		if (size > 0) {
 			strncpy(buffer2, buffer, 6);
 			type = (uint16_t)strtol(buffer2, (char **)&buffer2, 0);
 			if (debug) printf("TYPE from %s: %x\n", send_peerList(peer), type);
 			switch (type) {
 				case PACKET_LINKSTATE:
 				strncpy(buffer, buffer+7, sizeof(buffer));
 				decode_linkStatePacket(buffer, peer->in_fd);
 				break;
 				case PACKET_LEAVE:
 				strncpy(buffer, buffer+10, sizeof(buffer));
 				decode_leavePacket(buffer);
 				break;
 				case PACKET_QUIT:
 				send_quitPacket();
 				break;
 				default:
 				if (debug) printf("Negative.\n");
 			}
 		} else if (size < 0) {
 			printf("recv error from %s - %d | ERR: %d\n", send_peerList(peer), peer->in_fd, errno);
 			remove_peer(peer);
 			print_peerList();
 			print_linkStateRecords();
 			return NULL;
 			break;
 		} else if (size == 0) {
 			printf("PEER: Peer Removed %s:%d: Peer disconnected\n", inet_ntoa(peer->listenIP), peer->listenPort);
 			remove_peer(peer);
 			print_peerList();
 			print_linkStateRecords();
 			return NULL;
 		}

 		printf("///End while loop for %s\n", send_peerList(peer));
 	}
 	if (debug) puts("Leaving handle_listen");
 	return NULL;
 }

/* Server Mode */
 void *server()
 {
 	struct sockaddr_in local_addr, client_addr;
 	int optval = 1, new_fd;
 	socklen_t addrlen = sizeof(client_addr);
 	struct peerList *new_peer = (struct peerList *)malloc(sizeof(struct peerList));

	/* Allows reuse of socket if not closed properly */
 	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&optval, sizeof(optval)) < 0) {
 		perror("setsockopt");
 		exit(1);
 	}

	/* Bind socket */
 	memset((char *)&local_addr, 0, sizeof(local_addr));
 	local_addr.sin_family = AF_INET;
 	local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
 	local_addr.sin_port = htons(local_info->listenPort);
 	if (bind(sock_fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
 		perror("bind");
 		exit(1);
 	}

 	printf("Server Mode: Waiting for connections on %s:%d...\n", inet_ntoa(local_info->listenIP), local_info->listenPort);

	/* Listens for connection, backlog 10 */
 	if (listen(sock_fd, BACKLOG) < 0) {
 		perror("listen");
 		exit(1);
 	}

 	/* Wait for connections */
 	while (1) {
 		if ((new_fd = accept(sock_fd, (struct sockaddr *)&client_addr, &addrlen)) < 0) {
 			perror("accept");
 			exit(1);
 		}

 		new_peer->in_fd = new_fd;
 		new_peer->listenIP = client_addr.sin_addr;
 		new_peer->listenPort = htons(client_addr.sin_port);
 		if (pthread_create(&listen_thread, NULL, handle_listen, (void*)new_peer) != 0) {
 			perror("listen_thread");
 			exit(1);
 		}
 	}
 	return NULL;
 }

/* Thread to open and handle tap device, read from tap and send to socket */
 void *handle_tap()
 {
 	if (debug) puts("create thread for tap");
 	uint16_t type;
 	ssize_t size;
 	char *buffer = malloc(MAXBUFFSIZE);
 	union ethframe frame;

 	while (1) {
 		puts("in while tap");
 		size = read(tap_fd, &frame, MAXBUFFSIZE);
 		printf("tapSize: %d\n", size);
		// if(errno!=EINTR) {
		// 	perror("EINTR");
		// 	exit(1);
		// }
 		if (size < 0) {
 			perror("not connected tap");
 			close(net_fd);
 			close(tap_fd);
 			exit(1);
 		} else if (size == 0) {
 			printf("0 tap");
 			close(tap_fd);
 			close(net_fd);
 			exit(1);
 		}
 		type = htons(0xABCD);
 		buffer[size] = '\0';
 		printf("Tap Message: %s | Size: %d bytes.\n", buffer, size);
 		if (write(tap_fd, buffer, sizeof(buffer)) < 0) {
 			perror("write to tap");
 			close(tap_fd);
 			close(net_fd);
 			exit(1);
 		} else {
 			printf("Message sent to socket.\n");
 			close(tap_fd);
 			pthread_exit(NULL);
 		}
 	}
 	return NULL;
 }

/* Client Mode */
 void *connectToPeer(void *temp) {
 	struct sockaddr_in remote_addr;
 	int new_fd;
 	char *buffer = malloc(MAXBUFFSIZE), *buf1 = malloc(MAXBUFFSIZE), *buf2 = malloc(MAXBUFFSIZE);
 	struct peerList *peer = (struct peerList *)temp;

 	if (!add_peer(peer) && (peer->net_fd)) return NULL;

	/* Create TCP Socket */
 	if ((new_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
 		perror("could not create socket");
 		exit(1);
 	}

 	memset((char *)&remote_addr, 0, sizeof(remote_addr));
 	remote_addr.sin_family = AF_INET;
 	remote_addr.sin_port = htons(peer->listenPort);
 	inet_aton((char *)inet_ntoa(peer->listenIP), &remote_addr.sin_addr);

 	sprintf(buf1, "%s %d", inet_ntoa(local_info->listenIP), local_info->listenPort);
 	sprintf(buf2, "%s %d", inet_ntoa(peer->listenIP), peer->listenPort);

 	if (debug) printf("COMPARING %s --- %s\n", buf1, buf2);
 	if (!(strcmp(buf1, buf2))) return NULL;

 	puts("Client Mode:");
 	printf("NEW PEER: Connecting to %s:%d\n", inet_ntoa(remote_addr.sin_addr), ntohs(remote_addr.sin_port));

	/* Connect to server */
 	if ((connect(new_fd, (struct sockaddr *)&remote_addr, sizeof(remote_addr))) < 0) {
 		printf("NEW PEER: Peer Removed %s:%d: Failed to connect\n", inet_ntoa(peer->listenIP), peer->listenPort);
 		if (debug) printf("errno: %d\n", errno);
 		remove_peer(peer);
 	} else {
		/* Create single link state packet */
 		strcpy(buffer, peer->tapDevice);
 		peer->net_fd = new_fd;
 		printf("NEW PEER: Connected to server %s:%d - %d\n", inet_ntoa(peer->listenIP), peer->listenPort, peer->net_fd);
 		send_singleLinkStatePacket(peer);
 		lsPacket->neighbors = HASH_COUNT(peers);
 		if (debug) print_linkStatePacket();
 	}

 	if (debug) puts("Leaving connectToPeer");
 	return NULL;
 }

/* Send linkState */
 char *send_peerList(struct peerList *ls) {
 	char *buffer = malloc(MAXBUFFSIZE);

 	/* Serialize Data - listenIP | listenPort | ethMAC */
 	sprintf(buffer, "%s %d %02x:%02x:%02x:%02x:%02x:%02x ", inet_ntoa(ls->listenIP), ls->listenPort, (unsigned char)ls->ethMAC.sa_data[0], (unsigned char)ls->ethMAC.sa_data[1], (unsigned char)ls->ethMAC.sa_data[2], (unsigned char)ls->ethMAC.sa_data[3], (unsigned char)ls->ethMAC.sa_data[4], (unsigned char)ls->ethMAC.sa_data[5]);
 	return buffer;
 }

/* Flood linkStateRecords */
 void *flood_packets() {
 	struct peerList *s, *tmp;
 	char *buffer = malloc(MAXBUFFSIZE);

 	while (1) {
 		sleep(linkPeriod);
 			/* Serialize Data - Packet Type | Packet Length | Source IP | Source Port | Eth MAC | tapDevice | Neighbors | Records */
 		lsPacket->header->length = sizeof(lsPacket) + sizeof(lsPacket->header) + sizeof(lsPacket->source);
 		sprintf(buffer, "0x%x %d %s %d %02x:%02x:%02x:%02x:%02x:%02x %s %d %d ", ntohs(lsPacket->header->type), lsPacket->header->length, inet_ntoa(lsPacket->source->listenIP), lsPacket->source->listenPort, (unsigned char)lsPacket->source->ethMAC.sa_data[0], (unsigned char)lsPacket->source->ethMAC.sa_data[1], (unsigned char)lsPacket->source->ethMAC.sa_data[2], (unsigned char)lsPacket->source->ethMAC.sa_data[3], (unsigned char)lsPacket->source->ethMAC.sa_data[4], (unsigned char)lsPacket->source->ethMAC.sa_data[5], dev, HASH_COUNT(peers), HASH_COUNT(records));

 		printf("OUTGOING PAYLOAD: %s\n", buffer);
 		if (debug) puts("^^^^FLOODING^^^^");
 		print_peerList();

 		/* Lock peers and records, iterate through peers and send records */
 		HASH_ITER(hh, peers, s, tmp) {
 			send_linkStatePacket(s, buffer);
 		}
 	}
 	return NULL;
 }

/* Periodically check for timed out peers */
 void *check_timeout() {
 	struct timeval current_time;
 	struct peerList *s, *tmp;

 	while (1) {
 		sleep(linkTimeout);
 		gettimeofday(&current_time, NULL);
 		puts("Checking for timed out peers...\n");

 		HASH_ITER(hh, peers, s, tmp) {
 			pthread_mutex_lock(&peer_mutex);
 			printf("%ld -- %ld\n", current_time.tv_sec, s->lastLS);
 			if ((current_time.tv_sec - s->lastLS) > linkTimeout) {
 				printf("PEER: %shas timed out.\n", send_peerList(s));
 				pthread_mutex_unlock(&peer_mutex);
 				remove_peer(s);
 			}
 			pthread_mutex_unlock(&peer_mutex);
 		}
 	}
 }

/* Send single linkStatePacket */
 void send_singleLinkStatePacket(struct peerList *peer) {
 	struct linkStateRecord *new_record = create_linkStateRecord(local_info, peer);
 	char *buffer = malloc(MAXBUFFSIZE);
 	int size;

 	/* Serialize Data - Packet Type | Packet Length | Source IP | Source Port | Eth MAC | tapDevice | Neighbors | uniqueID | linkWeight */
 	lsPacket->header->length = sizeof(lsPacket) + sizeof(lsPacket->header) + sizeof(lsPacket->source);
 	sprintf(buffer, "0x%x %d %s %d %02x:%02x:%02x:%02x:%02x:%02x %s 0 0 %ld:%ld %d ", ntohs(lsPacket->header->type), lsPacket->header->length, inet_ntoa(lsPacket->source->listenIP), lsPacket->source->listenPort, (unsigned char)lsPacket->source->ethMAC.sa_data[0], (unsigned char)lsPacket->source->ethMAC.sa_data[1], (unsigned char)lsPacket->source->ethMAC.sa_data[2], (unsigned char)lsPacket->source->ethMAC.sa_data[3], (unsigned char)lsPacket->source->ethMAC.sa_data[4], (unsigned char)lsPacket->source->ethMAC.sa_data[5], dev, new_record->uniqueID.tv_sec, new_record->uniqueID.tv_usec, new_record->linkWeight);
 	strcat(buffer, send_peerList(local_info));
 	strcat(buffer, send_peerList(peer));

 	/* Send linkStatePacket */
 	send(peer->net_fd, buffer, strlen(buffer), 0);
 	if (debug) printf("\nPAYLOAD SENT: %s on %d\n", buffer, peer->net_fd);
 	memset(buffer, 0, MAXBUFFSIZE);
 	/* Receive MAC Address and tapDevice */
 	size = recv(peer->net_fd, buffer, MAXBUFFSIZE, 0);
 	if (size < 0) {
 		printf("recv error from %s - %d | ERR: %d\n", send_peerList(peer), peer->in_fd, errno);
 		remove_peer(peer);
 		free(buffer);
 		return;
 	} else if (size == 0) {
 		printf("PEER: Peer Removed %s:%d: Peer disconnected\n", inet_ntoa(peer->listenIP), peer->listenPort);
 		remove_peer(peer);
 		print_peerList();
 		print_linkStateRecords();
 		return;
 	}

 	if (debug) printf("Remote MAC: %s from %d\n", buffer, peer->net_fd);
 	puts("NEW PEER: Single link state record sent.");
 	sscanf(buffer ,"%hhX:%hhX:%hhX:%hhX:%hhX:%hhX %s", (unsigned char *)&peer->ethMAC.sa_data[0], (unsigned char *)&peer->ethMAC.sa_data[1], (unsigned char *)&peer->ethMAC.sa_data[2], (unsigned char *)&peer->ethMAC.sa_data[3], (unsigned char *)&peer->ethMAC.sa_data[4], (unsigned char *)&peer->ethMAC.sa_data[5], peer->tapDevice);

 	print_linkStateRecords();

 	free(buffer);
 }

/* Send linkStatePacket */
 void send_linkStatePacket(struct peerList *target, char *buffer) {
 	pthread_mutex_lock(&peer_mutex);
 	pthread_mutex_lock(&linkstate_mutex);
 	struct linkStateRecord *s, *tmp;
 	char *buf1 = malloc(MAXBUFFSIZE);
 	int size;

 	printf("\n^^FLOODING TO: %s", send_peerList(target));

 	HASH_ITER(hh, records, s, tmp) {
 		memset(buf1, 0, MAXBUFFSIZE);
 	/* Concat uniqueID | linkWeight */
 		sprintf(buf1, "%ld:%ld %d ", s->uniqueID.tv_sec, s->uniqueID.tv_usec, s->linkWeight);
 		strcat(buffer, buf1);
 		strcat(buffer, send_peerList(s->proxy1));
 		strcat(buffer, send_peerList(s->proxy2));
 		strcat(buffer, "!");
 	}

 	/* Send linkStatePacket */
 	size = send(target->net_fd, buffer, strlen(buffer), 0);
 	if (debug) printf("\n\n======FLOODING OUT: %s on %d\n", buffer, target->net_fd);
 	memset(buffer, 0, MAXBUFFSIZE);
 	if (size < 0) {
 		printf("send error from %s - %d | ERR: %d\n", send_peerList(target), target->in_fd, errno);
 		remove_peer(target);
 		free(buffer);
 		return;
 	}
 	pthread_mutex_unlock(&peer_mutex);
 	pthread_mutex_unlock(&linkstate_mutex);
 }

/* Create linkStateRecord */
 struct linkStateRecord *create_linkStateRecord(struct peerList *proxy1, struct peerList *proxy2) {
 	struct timeval current_time;
 	struct linkStateRecord *new_record = (struct linkStateRecord *)malloc(sizeof(struct linkStateRecord));
 	memset(new_record, 0, sizeof(struct linkStateRecord));

 	/* Ensure proxy1 != proxy2 */
 	if (!strcmp(send_peerList(proxy1), send_peerList(proxy2))) {
 		printf("NEW PEER: Peer Removed %s:%d: Duplicate proxies\n", inet_ntoa(proxy1->listenIP), proxy1->listenPort);
 		pthread_exit(NULL);
 	}

 	if (debug) printf("\nCreating new linkStateRecord: %s | %s\n", send_peerList(proxy1), send_peerList(proxy2));
 	gettimeofday(&current_time, NULL);
 	new_record->uniqueID = current_time;
 	new_record->linkWeight = 1;
 	new_record->proxy1 = proxy1;
 	new_record->proxy2 = proxy2;
 	/* Verify peer isn't in the list, connect if it ins't */
 	if (find_peer(proxy1) == NULL) {
 		if (debug) printf("Starting new thread for %s:%d\n", inet_ntoa(proxy1->listenIP), proxy1->listenPort);
 		if (pthread_create(&connect_thread, NULL, connectToPeer, (void *)proxy1) != 0) {
 			perror("connect_thread");
 			pthread_exit(NULL);
 		}
 	}
 	if (find_peer(proxy2) == NULL) {
 		if (debug) printf("Starting new thread for %s:%d\n", inet_ntoa(proxy2->listenIP), proxy2->listenPort);
 		if (pthread_create(&connect_thread, NULL, connectToPeer, (void *)proxy2) != 0) {
 			perror("connect_thread");
 			pthread_exit(NULL);
 		}
 	}
 	add_record(new_record);
 	return new_record;
 }

/* Sends leavePacket */
 void send_leavePacket(struct peerList *leaving, struct peerList *sendto) {
 	char *buffer = malloc(MAXBUFFSIZE);

 	sprintf(buffer, "0x%x 20 %s", PACKET_LEAVE, send_peerList(leaving));
 	if (debug) printf("LEAVING AND SENDING: %s - %d\n", buffer, sendto->net_fd);
 	send(sendto->net_fd, buffer, strlen(buffer), 0);
 }

/* Sends quitPacket */
 void send_quitPacket() {
 	struct peerList *s, *tmp;
 	char *buffer = malloc(MAXBUFFSIZE);

 	sprintf(buffer, "0x%x 20 %s", PACKET_QUIT, send_peerList(local_info));
 	if (debug) printf("QUIT PACKET GOGO: %s\n", buffer);
 	HASH_ITER(hh, peers, s, tmp) {
 		send(s->net_fd, buffer, strlen(buffer), 0);
 		close(s->net_fd);
 		close(s->in_fd);
 	}
 	exit(1);
 }

/* Print packetHeader information */
 void print_packetHeader(struct packetHeader *pkt) {
 	printf("---PACKETHEADER: Type: 0x%x | Length: %d\n", ntohs(pkt->type), pkt->length);
 }

/* Print peerList information */
 void print_peer(struct peerList *peer) {
 	print_linkState(peer);
 	printf("--Tap: %s | NET_FD: %d | IN_FD: %d | LASTLS: %ld\n", peer->tapDevice, peer->net_fd, peer->in_fd, peer->lastLS);
 }

/* Print peers hash table */
 void print_peerList() {
 	struct peerList *tmp;
 	unsigned int num = HASH_COUNT(peers), i;
 	printf("\nPEERS: %d\n", num);

 	for (tmp = peers, i = 1; tmp != NULL; tmp = tmp->hh.next, i++) {
 		printf("---PEER %d: ", i);
 		print_peer(tmp);
 	}
 }

/* Print linkState information */
 void print_linkState(struct peerList *ls) {
 	char *ethMAC = malloc(MAXBUFFSIZE);
 	sprintf(ethMAC, "%02x:%02x:%02x:%02x:%02x:%02x", (unsigned char)ls->ethMAC.sa_data[0], (unsigned char)ls->ethMAC.sa_data[1], (unsigned char)ls->ethMAC.sa_data[2], (unsigned char)ls->ethMAC.sa_data[3], (unsigned char)ls->ethMAC.sa_data[4], (unsigned char)ls->ethMAC.sa_data[5]);
 	printf("--LINKSTATE: listenIP: %s:%d | MAC: %s\n", inet_ntoa(ls->listenIP), ls->listenPort, ethMAC);
 	free(ethMAC);
 }

/* Print linkStatePacket information */
 void print_linkStatePacket() {
 	puts("\n\n---LINKSTATE PACKET INFORMATION---");
 	print_packetHeader(lsPacket->header);
 	print_linkState(lsPacket->source);
 	printf("----Neighbors: %d", lsPacket->neighbors);
 	print_peerList();
 }

/* Print linkStateRecord information */
 void print_linkStateRecord(struct linkStateRecord *record) {
 	printf("\n@@@linkStateRecord: %ld:%ld | linkWeight: %d\nProxy 1: ", record->uniqueID.tv_sec, record->uniqueID.tv_usec, record->linkWeight);
 	print_linkState(record->proxy1);
 	printf("Proxy 2: ");
 	print_linkState(record->proxy2);
 }

/* Print linkStateRecords */
 void print_linkStateRecords() {
 	struct linkStateRecord *tmp, *s;
 	printf("\n\n###Complete linkStateRecords: %d Records###\n", HASH_COUNT(records));

 	if (records == NULL) return;

 	HASH_ITER(hh, records, s, tmp) {
 		print_linkStateRecord(s);
 	}
 }

/* Add new member */
 int add_peer(struct peerList *peer) {
 	pthread_mutex_lock(&peer_mutex);
 	struct peerList *tmp;
 	struct timeval current_time;
 	char *buf1 = send_peerList(peer), *buf2;
 	gettimeofday(&current_time, NULL);

 	buf2 = send_peerList(local_info);
 	if (debug) printf("\n\nTOTAL PEERS: %d | ATTEMPTING TO ADD PEER: %s - %d/%d\n", HASH_COUNT(peers), buf1, peer->net_fd, peer->in_fd);
 	if (!strcmp(buf1, buf2)) {
 		if (debug) puts("LOCAL MACHINE INFO\n");
 		pthread_mutex_unlock(&peer_mutex);
 		return 0;
 	} else if (peers == NULL) {
 		if (debug) printf("EMPTY PEERLIST: ADDING %s\n", buf1);
 		peer->lastLS = current_time.tv_sec;
 		HASH_ADD(hh, peers, ethMAC, sizeof(struct sockaddr), peer);
 	} else {
 		if ((tmp = find_peer(peer)) == NULL) {
 			peer->lastLS = current_time.tv_sec;
 			HASH_ADD(hh, peers, ethMAC, sizeof(struct sockaddr), peer);
 		} else {
 			pthread_mutex_unlock(&peer_mutex);
 			return 0;
 		}
 	}
 	pthread_mutex_unlock(&peer_mutex);
 	print_peerList();
 	return 1;
 }

/* Remove peer member */
 int remove_peer(struct peerList *peer) {
 	pthread_mutex_lock(&peer_mutex);
 	struct peerList *tmp;
 	char *buf1 = send_peerList(peer);

 	if (debug) {
 		printf("TOTAL PEERS: %d | ATTEMPTING TO REMOVE PEER: %s | NET_FD: %d | IN_FD: %d\n", HASH_COUNT(peers), buf1, peer->net_fd, peer->in_fd);
 	}

 	if (peers == NULL) {
 		if (debug) puts("EMPTY PEERLIST");
 		pthread_mutex_unlock(&peer_mutex);
 		return 1;
 	} else {
 		if ((tmp = find_peer(peer)) != NULL) {
 			if (debug) puts("REMOVED PEER");
 			remove_record(tmp);
 			close(tmp->in_fd);
 			close(tmp->net_fd);
 			HASH_DEL(peers, tmp);
 			pthread_mutex_unlock(&peer_mutex);
 			return 1;
 		}
 	}

 	print_peerList();
 	pthread_mutex_unlock(&peer_mutex);
 	return 0;
 }

/* Find specified peer */
 struct peerList *find_peer(struct peerList *peer) {
 	struct peerList *tmp, *s;
 	char *buf1 = send_peerList(peer), *buf2;

 	if (peers == NULL) {
 		if (debug) printf("EMPTY PEERLIST\n");
 		return NULL;
 	}

 	if (debug) printf("LOOKING FOR: %s\n", buf1);
 	HASH_ITER(hh, peers, s, tmp) {
 		buf2 = send_peerList(s);
 		if (debug) printf("CHECK PEER: %s\n", buf2);
 		if (!strcmp(buf1, buf2) || (s->in_fd == peer->in_fd && !(s->in_fd))) {
 			if (!(s->in_fd) && (peer->in_fd)) s->in_fd = peer->in_fd;
 			if (debug) printf("PEER FOUND: %s\n", buf1);
 			return s;
 		}
 	}
 	if (debug) puts("PEER NOT FOUND");
 	return NULL;
 }

/* Add new record */
 int add_record(struct linkStateRecord *record) {
 	pthread_mutex_lock(&linkstate_mutex);
 	struct linkStateRecord *tmp, *s;
 	char *buf1 = send_peerList(record->proxy1), *buf2 = send_peerList(record->proxy2), *buf3, *buf4;

 	if (debug) {
 		printf("TOTAL RECORDS: %d | ATTEMPTING TO ADD RECORD:\n%s- %d/%d | %s- %d/%d\n", HASH_COUNT(records), buf1, record->proxy1->net_fd, record->proxy1->in_fd, buf2, record->proxy2->net_fd, record->proxy2->in_fd);
 		printf("\nChecking proxy1 membership...\n");
 	}
 	if (add_peer(record->proxy1)) {
 		if (debug) printf("Starting new thread for %s:%d\n", inet_ntoa(record->proxy1->listenIP), record->proxy1->listenPort);
 		if (pthread_create(&connect_thread, NULL, connectToPeer, (void *)record->proxy1) != 0) {
 			perror("connect_thread");
 			pthread_mutex_unlock(&linkstate_mutex);
 			pthread_exit(NULL);
 		}
 	}

 	if (debug) printf("\nChecking proxy2 membership...\n");
 	if (add_peer(record->proxy2)) {
 		printf("Starting new thread for %s:%d\n", inet_ntoa(record->proxy2->listenIP), record->proxy2->listenPort);
 		if (pthread_create(&connect_thread, NULL, connectToPeer, (void *)record->proxy2) != 0) {
 			perror("connect_thread");
 			pthread_mutex_unlock(&linkstate_mutex);
 			pthread_exit(NULL);
 		}
 	}

 	if (debug) puts("Now to the adding the record...");
 	if (records == NULL) {
 		if (debug) puts("EMPTY RECORDS");
 		HASH_ADD(hh, records, uniqueID, sizeof(struct timeval), record);
 	} else {
 		HASH_ITER(hh, records, s, tmp) {
 			buf3 = send_peerList(s->proxy1);
 			buf4 = send_peerList(s->proxy2);
 			printf("CHECKING:\n%s | %s\n", buf3, buf4);
 			if (!strcmp(buf1, buf3) && !strcmp(buf2, buf4)) {
 				if (debug) {
 					puts("RECORD EXISTS!");
 					printf("COMPARE: %d\n", compare_uniqueID(record->uniqueID, s->uniqueID));
 				}
 				if (compare_uniqueID(record->uniqueID, s->uniqueID)) {
 					HASH_REPLACE(hh, records, uniqueID, sizeof(struct timeval), record, s);
 				}
 				print_linkStateRecords();
 				pthread_mutex_unlock(&linkstate_mutex);
 				return 0;
 			} else if (s->hh.next == NULL) {
 				HASH_ADD(hh, records, uniqueID, sizeof(struct timeval), record);
 				if (debug) puts("RECORD ADDED");
 			}
 		}
 	}

 	print_linkStateRecords();
 	if (debug) print_peerList();
 	pthread_mutex_unlock(&linkstate_mutex);
 	return 1;
 }

/* Remove peer from records */
 int remove_record(struct peerList *peer) {
 	pthread_mutex_lock(&linkstate_mutex);
 	struct linkStateRecord *tmp, *s;
 	char *buf1 = send_peerList(peer), *buf2, *buf3;

 	if (debug) printf("Removing peer from records: %s\n", buf1);
 	HASH_ITER(hh, records, s, tmp) {
 		buf2 = send_peerList(s->proxy1);
 		buf3 = send_peerList(s->proxy2);
 		if (!strcmp(buf1, buf2) || !strcmp(buf1, buf3)) {
 			HASH_DEL(records, s);
 		}
 	}
 	print_linkStateRecords();
 	pthread_mutex_unlock(&linkstate_mutex);
 	return 1;
 }

/* Decode leavePacket */
 void decode_leavePacket(char *buffer) {
 	struct peerList *leaving = (struct peerList *)malloc(sizeof(struct peerList)), *s, *tmp;
 	char *next_field, ip[100];
 	if (debug) {
 		printf("\n!!LEAVE PACKET RECEIVED: %s\n", buffer);
 		printf("\nDECODING: %s\n", buffer);
 	}
 	next_field = strtok(buffer, " \n");
 	if (inet_addr(next_field) == -1) {
 		getIP(next_field, ip);
 		next_field = ip;
 	}
 	inet_aton(next_field, &leaving->listenIP);
 	leaving->listenPort = atoi(strtok(NULL, " \n"));
 	next_field = strtok(NULL, " \n");
 	readMAC(next_field, leaving);

 	printf("PEER LEAVING: %s\n", send_peerList(leaving));
 	remove_peer(leaving);
 	// remove_record(leaving);
 	HASH_ITER(hh, peers, s, tmp) {
 		send_leavePacket(leaving, s);
 	}
 }

/* Decode linkStatePacket information */
 void decode_linkStatePacket(char *buffer, int in_fd) {
 	struct peerList *new_peer = (struct peerList *)malloc(sizeof(struct peerList));
 	char *next_field, ip[100], *ethMAC = malloc(MAXBUFFSIZE);
 	int neighbors, numrecords, i;
 	if (debug)printf("Received: %s\n", buffer);

 	/* Parse through buffer */
 	next_field = strtok(buffer, " \n");
 	next_field = strtok(NULL, " \n");

	/* Checks for a.b.c.d address, otherwise resolve hostname */
 	if (inet_addr(next_field) == -1) {
 		getIP(next_field, ip);
 		next_field = ip;
 	}
 	inet_aton(next_field, &new_peer->listenIP);
 	new_peer->listenPort = atoi(strtok(NULL, " \n"));
 	next_field = strtok(NULL, " \n");
 	readMAC(next_field, new_peer);
 	next_field = strtok(NULL, " \n");
 	strcpy(new_peer->tapDevice, next_field);
 	neighbors = atoi(strtok(NULL, " \n"));
 	numrecords = atoi(strtok(NULL, " \n"));
 	next_field = strtok(NULL, "!\n");
 	if (debug) printf("Neighbors: %d ", neighbors);
 	if (!(neighbors)) {
 		sprintf(ethMAC, "%02x:%02x:%02x:%02x:%02x:%02x %s", (unsigned char)local_info->ethMAC.sa_data[0], (unsigned char)local_info->ethMAC.sa_data[1], (unsigned char)local_info->ethMAC.sa_data[2], (unsigned char)local_info->ethMAC.sa_data[3], (unsigned char)local_info->ethMAC.sa_data[4], (unsigned char)local_info->ethMAC.sa_data[5], dev);
 		if (debug) printf("\nSINGLE LINKSTATE: SENT MAC: %s\n", ethMAC);
 		send(in_fd, ethMAC, strlen(ethMAC), 0);
 		sleep(1);
 		decode_singleLinkStateRecord(next_field, in_fd);
 	} else {
 		if (debug) puts("NOT SINGLE!");
 		printf("INCOMING NUMBER OF RECORDS: %d\n", numrecords);
 		for (i = 0; i < numrecords; i++) {
 			printf("NEXT: %s\n", next_field);
 			decode_linkStateRecord(next_field);
 			next_field = strtok(NULL, "!\n");
 		}
 	}
 }

/* Decode non-single linkStateRecord information */
 void decode_linkStateRecord(char *buffer) {
 	struct linkStateRecord *new_record = (struct linkStateRecord *)malloc(sizeof(struct linkStateRecord));
 	struct peerList *new_peerList1 = (struct peerList *)malloc(sizeof(struct peerList)), *new_peerList2 = (struct peerList *)malloc(sizeof(struct peerList));
 	char *next_field, ip[100];
 	if (debug) printf("\nDECODING: %s\n", buffer);
 	new_record->uniqueID.tv_sec = atoi(strtok(buffer, ":\n"));
 	new_record->uniqueID.tv_usec = atoi(strtok(NULL, " \n"));
 	new_record->linkWeight = atoi(strtok(NULL, " \n"));
 	next_field = strtok(NULL, " \n");
 	if (inet_addr(next_field) == -1) {
 		getIP(next_field, ip);
 		next_field = ip;
 	}
 	inet_aton(next_field, &new_peerList1->listenIP);
 	new_peerList1->listenPort = atoi(strtok(NULL, " \n"));
 	next_field = strtok(NULL, " \n");
 	readMAC(next_field, new_peerList1);
 	new_record->proxy1 = new_peerList1;
 	next_field = strtok(NULL, " \n");
 	if (inet_addr(next_field) == -1) {
 		getIP(next_field, ip);
 		next_field = ip;
 	}
 	inet_aton(next_field, &new_peerList2->listenIP);
 	new_peerList2->listenPort = atoi(strtok(NULL, " \n"));
 	next_field = strtok(NULL, " \n");
 	readMAC(next_field, new_peerList2);
 	new_record->proxy2 = new_peerList2;

 	print_linkStateRecord(new_record);
 	add_record(new_record);
 }

/* Decode linkStateRecord information */
 void decode_singleLinkStateRecord(char *buffer, int in_fd) {
 	struct linkStateRecord *new_record = (struct linkStateRecord *)malloc(sizeof(struct linkStateRecord));
 	struct peerList *new_peerList = (struct peerList *)malloc(sizeof(struct peerList));
 	char *next_field, ip[100];
 	if (debug) printf("\nDECODING: %s\n", buffer);
 	new_peerList->in_fd = in_fd;
 	new_record->uniqueID.tv_sec = atoi(strtok(buffer, ":\n"));
 	new_record->uniqueID.tv_usec = atoi(strtok(NULL, " \n"));
 	new_record->linkWeight = atoi(strtok(NULL, " \n"));
 	next_field = strtok(NULL, " \n");
 	if (inet_addr(next_field) == -1) {
 		getIP(next_field, ip);
 		next_field = ip;
 	}
 	inet_aton(next_field, &new_peerList->listenIP);
 	new_peerList->listenPort = atoi(strtok(NULL, " \n"));
 	next_field = strtok(NULL, " \n");
 	readMAC(next_field, new_peerList);
 	new_record->proxy1 = new_peerList;
 	new_record->proxy2 = local_info;

 	print_linkStateRecord(new_record);
 	add_record(new_record);
 }

/* String to MAC Address */
 void readMAC(char *buffer, struct peerList *pl) {
 	sscanf(buffer ,"%hhX:%hhX:%hhX:%hhX:%hhX:%hhX", (unsigned char *)&pl->ethMAC.sa_data[0], (unsigned char *)&pl->ethMAC.sa_data[1], (unsigned char *)&pl->ethMAC.sa_data[2], (unsigned char *)&pl->ethMAC.sa_data[3], (unsigned char *)&pl->ethMAC.sa_data[4], (unsigned char *)&pl->ethMAC.sa_data[5]);
 }

/* Sleeper for quitAfter */
 void *sleeper() {
 	struct peerList *s, *tmp;

 	sleep(quitAfter);
 	printf("%d seconds have elapsed. Proxy terminating.\n", quitAfter);
 	print_peerList();
 	print_linkStateRecords();

 	// send_quitPacket();

 	HASH_ITER(hh, peers, s, tmp) {
 		send_leavePacket(local_info, s);
 		close(s->net_fd);
 		close(s->in_fd);
 	}
 	exit(1);
 }

/* Compare uniqueIDs */
 int compare_uniqueID(struct timeval a, struct timeval b) {
 	if (a.tv_sec > b.tv_sec) return 1;
 	else if (a.tv_sec < b.tv_sec) return 0;
 	else if (a.tv_usec > b.tv_usec) return 1;
 	else if (a.tv_usec < b.tv_usec) return 0;
 	return 0;
 }

/* Main */
 int main (int argc, char *argv[]) {
 	if (debug) puts("DEBUGGING MODE:");

 // 	int size;
 // 	char if_name[IFNAMSIZ] = "";
 // 	unsigned char dest[ETH_ALEN] = { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 };
 // 	unsigned short proto = 0x1234;
 // 	char *data = "hello world~!!!";
 // 	unsigned short data_len = strlen(data);

 // 	unsigned char source[ETH_ALEN] = { 0x61, 0x12, 0x34, 0x56, 0x78, 0x90 };

 // 	union ethframe frame;
 // 	memcpy(frame.field.header.h_dest, dest, ETH_ALEN);
 // 	memcpy(frame.field.header.h_source, source, ETH_ALEN);
 // 	frame.field.header.h_proto = htons(proto);
 // 	memcpy(frame.field.data, data, data_len);

 // 	unsigned int frame_len = data_len + ETH_HLEN;
 // 	strncpy(if_name, "tap10", IFNAMSIZ - 1);
 // 	printf("Attempting to open %s...\n", if_name);
	/* Open tap interface */
 	if ((tap_fd = allocate_tunnel(dev, IFF_TAP | IFF_NO_PI)) < 0) {
 		perror("Opening tap interface failed!");
 		return EXIT_FAILURE;
 	} else {
 		printf("Successfully opened %s interface...\n", dev);
 	}

 // 	if ((size = write(tap_fd, &frame.buffer, frame_len)) < 0) {
 // 		perror("write to tap");
 // 		close(tap_fd);
 // 		return EXIT_FAILURE;
 // 	} else {
 // 		printf("%d bytes sent to tap..\n", size);

 // 	}

 // 	if (pthread_create(&socket_thread, NULL, handle_tap, NULL) != 0 ) {
 // 		perror("socket_thread");
 // 		return EXIT_FAILURE;
 // 	}

 	/* Parse input file */
 	if (parseInput(argc, argv)) {
 		perror("parseInput");
 		close(tap_fd);
 		return EXIT_FAILURE;
 	}

 	/* Start server path */
 	if (pthread_create(&server_thread, NULL, server, NULL) != 0) {
 		perror("server_thread");
 		pthread_exit(NULL);
 	}

 	/* Start flooding thread */
 	if (pthread_create(&flood_thread, NULL, flood_packets, NULL) != 0) {
 		perror("flood_thread");
 		pthread_exit(NULL);
 	}

 	/* Start timeout thread */
 	if (pthread_create(&timeout_thread, NULL, check_timeout, NULL) != 0) {
 		perror("timeout_thread");
 		pthread_exit(NULL);
 	}

 	close(tap_fd);
 	pthread_exit(NULL);

 	return 0;
 }
