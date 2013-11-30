/* cs352proxy.c
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

/* Printing Macros */
#define eprintf(...) fprintf(stderr, __VA_ARGS__);
#define MAXBUFFSIZE 1500
#define BACKLOG 10
#define MAX_DEV_LINE 256

/* FDs used for read/write to net and tap */
int tap_fd, net_fd, sock_fd;

/* Threads to handle socket and tap */
pthread_t tap_thread, socket_thread;

/* Variable to determine mode */
char *host;

/* Struct for data packet information */
struct dataPacket {
    uint16_t type;
    uint16_t length;
    char data[MAXBUFFSIZE];
};

union ethframe
{
  struct
  {
    struct ethhdr    header;
    unsigned char    data[ETH_DATA_LEN];
  } field;
  unsigned char    buffer[ETH_FRAME_LEN];
};

/* Prints the command-line usage syntax */
void print_usage(void)
{
    eprintf("Syntax:\n");
    eprintf("  Server Mode: cs352proxy <port> <local interface>\n");
    eprintf("  Client Mode: cs352proxy <remote host> <remote port> <local interface>\n");
}

/* Open a tun/tap and return the fd to read/write back to caller */
int allocate_tunnel(char *dev, int flags) {
    int fd, error;
    struct ifreq ifr;
    char *device_name = "/dev/net/tun";

    if ( (fd = open(device_name , O_RDWR)) < 0 ) {
        perror("error opening /dev/net/tun");
        return fd;
    }
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = flags;

    if (*dev) {
        strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    }

    if( (error = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0 ) {
        perror("ioctl on tap failed");
        close(fd);
        return error;
    }

    strcpy(dev, ifr.ifr_name);
    return fd;
}

/* Thread to open and handle tap device, read from tap and send to socket */
void *handle_tap()
{


    puts("create thread for tap");
    ssize_t size;
    uint16_t type, length;
    char buffer[MAXBUFFSIZE];





    memset(buffer, 0, MAXBUFFSIZE);
    while (1) {
      puts("in while tap");
      size = read(tap_fd, buffer, MAXBUFFSIZE);
      printf("tapSize: %d\n", size);
      if(errno!=EINTR) {
        perror("EINTR");
        exit(1);
      }
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
      perror("write to tap failed");
      close(tap_fd);
      close(net_fd);
      exit(1);
      } else {
      printf("Message sent to socket.\n");
      close(tap_fd);
pthread_exit(NULL);
      }
      return NULL;
    }
}

/* Read from socket and write to tap */
void *handle_socket()
{
    puts("create thread for socket");
    ssize_t size;
    uint16_t type, length;
    char buffer[MAXBUFFSIZE];

    memset(buffer, 0, MAXBUFFSIZE);
    while (1) {
      puts("in while socket");
      if ((size = recv(net_fd, buffer, MAXBUFFSIZE-1, 0)) < 0) {
        perror("not connected socket\n");
        close(net_fd);
        close(tap_fd);
        exit(1);
      } else if (size == 0) {
        printf("0 socket\n");
        close(tap_fd);
        close(net_fd);
        exit(1);
      }
      type = htons(0xABCD);
      buffer[size] = '\0';
      printf("Socket Message: %s | Size: %d bytes.\n", buffer, size);
      if ((size = write(tap_fd, buffer, size)) < 0) {
        perror("write to tap failed");
        close(tap_fd);
        close(net_fd);
        exit(1);
      } else {
        printf("%d bytes sent to tap..\n", size);
        pthread_exit(NULL);
      }
      return NULL;
    }
}

/* Server Mode */
void server(int port, int sock_fd, char *if_name)
{
    struct sockaddr_in local_addr, client_addr;
    socklen_t addrlen = sizeof(client_addr);
    char buffer[MAXBUFFSIZE];

    puts("Server Mode:");

    int optval = 1;
    /* Allows reuse of socket if not closed properly */
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) < 0) {
      perror("setsockopt failure");
      close(sock_fd);
    exit(1);
    }

    /* Bind socket */
    memset((char *)&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(port);
    if (bind(sock_fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("failed to bind");
        close(sock_fd);
        exit(1);
    }

    /* Listens for connection, backlog 5 */
    if (listen(sock_fd, BACKLOG) < 0) {
        perror("failed to listen");
        close(sock_fd);
        exit(1);
    }

    if ((net_fd = accept(sock_fd, (struct sockaddr *)&client_addr, &addrlen)) < 0) {
     perror("accept failed");
     exit(1);
    }

    printf("Client connected from %s:%d.\n", inet_ntoa(client_addr.sin_addr), htons(client_addr.sin_port));

    if (pthread_create(&tap_thread, NULL, handle_tap, NULL) != 0) {
      perror("handle_tap");
      exit(1);
    }
    if (pthread_create(&socket_thread, NULL, handle_socket, NULL) != 0) {
      perror("handle_socket");
      exit(1);
    }
    pthread_join(socket_thread, NULL);
    pthread_join(tap_thread, NULL);
    pthread_exit(NULL);
}

/* Client Mode */
void client(int port, int sock_fd, char *host, char *if_name)
{
    struct sockaddr_in remote_addr;
    char buffer[MAXBUFFSIZE];

    puts("Client Mode:");

    memset((char *)&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(port);

    memcpy((void *)&remote_addr.sin_addr, getHostName(host), hp->h_length);
    // remote_addr.sin_addr.s_addr = inet_addr(host);
    // if (remote_addr.sin_addr.s_addr == -1) {
    //     hp = gethostbyname(host);
    //     if (hp == NULL) {
    //         perror("could not resolve hostname");
    //         exit(1);
    //     }
    //     memcpy((void *)&remote_addr.sin_addr, hp->h_addr_list[0], hp->h_length);
    // }

    /* Connect to server */
    if ((net_fd = connect(sock_fd, (struct sockaddr *)&remote_addr, sizeof(remote_addr))) < 0) {
        perror("connection failed");
        exit(1);
    }
    net_fd = sock_fd;
    printf("Connected to server %s:%d\n", inet_ntoa(remote_addr.sin_addr), port);

    strcpy(buffer, "teskdkak3dakdkakd123t");
    if (send(net_fd, (void *)&buffer, strlen(buffer), 0) < 0) {
      perror("write failed");
      exit(1);
    } else {
      printf("%s sent, %d bytes.\n", buffer, strlen(buffer));
    }
}

int main (int argc, char *argv[])
{
  char buffer[MAXBUFFSIZE];
    char if_name[IFNAMSIZ] = "";
    // int port;
char buffer2[MAXBUFFSIZE];

int size;
pthread_t thread;

    // switch(argc) {
    //     case 3: /* Server Mode */
    //         host = NULL;
    //         port = atoi(argv[1]);
    //         if (port < 1024 || port > 65535) {
    //             puts("Port number must be in the range of 1024 to 65535.");
    //             return EXIT_FAILURE;
    //         }
    //         strncpy(if_name, argv[2], IFNAMSIZ - 1);
    //         break;
    //     case 4: /* Client Mode */
    //         host = argv[1];
    //         port = atoi(argv[2]);;
    //         if (port < 1024 || port > 65535) {
    //             puts("Port number must be in the range of 1024 to 65535.");
    //             return EXIT_FAILURE;
    //         }
    //         strncpy(if_name, argv[3], IFNAMSIZ - 1);
    //         break;
    //     default:  Syntax Error
    //         print_usage();
    //         return EXIT_FAILURE;
    // }
unsigned char dest[ETH_ALEN] = { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 };
  unsigned short proto = 0x1234;
  unsigned char *data = "hello world~!!!";
  unsigned short data_len = strlen(data);

  unsigned char source[ETH_ALEN] = { 0x61, 0x12, 0x34, 0x56, 0x78, 0x90 };

  union ethframe frame;
  memcpy(frame.field.header.h_dest, dest, ETH_ALEN);
  memcpy(frame.field.header.h_source, source, ETH_ALEN);
  frame.field.header.h_proto = htons(proto);
  memcpy(frame.field.data, data, data_len);

  unsigned int frame_len = data_len + ETH_HLEN;
strncpy(if_name, argv[1], IFNAMSIZ - 1);
strcpy(buffer, "teskdkak3dakdkTEATTESTWHOO");
    printf("Attempting to open %s...\n", if_name);
    /* Open tap interface */
    if ((tap_fd = allocate_tunnel(if_name, IFF_TAP | IFF_NO_PI)) < 0) {
        perror("Opening tap interface failed!");
        return EXIT_FAILURE;
    } else {
        printf("Successfully opened %s interface...\n", if_name);
    }

      if ((size = write(tap_fd, frame.buffer, frame_len)) < 0) {
        perror("write to tap failed");
        close(tap_fd);
        exit(1);
      } else {
        printf("%d bytes sent to tap..\n", size);

      }


    if (pthread_create(&socket_thread, NULL, handle_tap, NULL) != 0 ) {
        perror("pthread_create failed for tap thread");
        exit(1);
    }

pthread_exit(NULL);


    // /* Create TCP Socket */
    // if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    //     perror("could not create socket");
    //     return EXIT_FAILURE;
    // }

    // /* Check for server or client mode */
    // if (host == NULL) {
    //     server(port, sock_fd, if_name);
    // } else {
    //     client(port, sock_fd, host, if_name);
    // }

    // pthread_exit(NULL);
    // close(net_fd); NU
    // close(tap_fd);
pthread_join(thread, NULL);

    close(tap_fd);
    return 0;
}
