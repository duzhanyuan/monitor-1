/*
 * pp.c
 * push/pull data across a unix/tcp stream to stdout/from stdin.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/time.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

/*
 * get ip address given a hostname or an ip addess in ascii xxx.xxx.xxx.xxx
 */
unsigned long
getip(hostname)
    char *hostname;
{
    struct hostent *hp;
    unsigned long res;
    unsigned char byte;

    if (isalpha(*hostname)) {
	/* get the ip addr given the hostname */
	if ((hp = gethostbyname(hostname)) == 0) {
	    fprintf(stderr, "%s: no such host\n", hostname);
	    exit(1);
	}
	bcopy((char*) hp->h_addr, (char*) &res, sizeof(res));
	return(res); /* should already be in network byte order */
    }
    else {
	/* convert from ascii ip addr. in dot notation to a 32 bit value */
	res = byte = 0;
	while(*hostname) {
	    if (*hostname == '.') {
		res = (res << 8) | byte;
		byte = 0;
	    }
	    else {
		byte = byte * 10 + (*hostname - '0');
	    }
	    hostname++;
	}
	res = (res << 8) | byte;
	
	return(htonl(res));
    }
}

/*
 * funnel data from the socket to stdout, and from stdin to the socket
 * until either the socket or stdin shows an EOF on a read
 */
void
talk(int sock)
{
   fd_set fdset;
   int size;
   int bytes_received = 0;
   struct timeval starttime, endtime;
   char buf[102400];

   if (gettimeofday(&starttime, 0) < 0) {
      perror("gettimeofday");
      exit(1);
   }

   while(1) {
      FD_ZERO(&fdset);
      FD_SET(0, &fdset);
      FD_SET(sock, &fdset);
      if (select(sock+1, &fdset, 0, 0, 0) < 0) {
         perror("select");
         exit(1);
      }
      if (FD_ISSET(sock, &fdset)) {
         if ((size = read(sock, buf, sizeof(buf))) == 0)
            goto done;
         if (size < 0) {
            perror("read");
            exit(1);
         }

         if (write(1, buf, size) < size) {
            perror("write");
            exit(1);
         }
         bytes_received += size;
      }
      if (FD_ISSET(0, &fdset)) {
         if ((size = read(0, buf, sizeof(buf))) == 0) /* EOF */
            goto done;
         if (size < 0) {
            perror("read");
            exit(1);
         }

         if (write(sock, buf, size) < size) {
            perror("write");
            exit(1);
         }
      }
   }
done:
   if (bytes_received) {
      float fsec;

      if (gettimeofday(&endtime, 0) < 0) {
         perror("gettimeofday");
         exit(1);
      }
      fsec = (float)(endtime.tv_sec - starttime.tv_sec) +
         (float)(endtime.tv_usec - starttime.tv_usec) / 1000000.0;
      fprintf(stderr, "%d bytes received in %.1f secs: %.1f cps\n",
            bytes_received, fsec, (float)bytes_received / fsec);
   }
   exit(0);
}


/*
 * Open a tcp connection to the given hostname on the given port.
 */
int
client(char *hostname, int port)
{
   int sock, sz;
   int sock_size;
   union {
      struct sockaddr_in in_sock;
      struct sockaddr_un un_sock;
   } name;
   u_short sa_family;

   if (port) {
      sa_family = AF_INET;
   } else {
      sa_family = AF_UNIX;
   }

   if ((sock = socket(sa_family, SOCK_STREAM, 0)) < 0) {
      perror("socket");
      exit(1);
   }

   sz = 1024;
   if (0) if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz)) < 0) {
      perror("getsockopt");
      exit(1);
   }

   if (sa_family == AF_INET) {
      name.in_sock.sin_family = AF_INET;
      name.in_sock.sin_addr.s_addr = getip(hostname);
      name.in_sock.sin_port = htons((u_short)port);
      sock_size = sizeof name.in_sock;
      printf ("Connecting client to %s:%hu\n", hostname, port);
   } else {
      name.un_sock.sun_family = AF_UNIX;
      name.un_sock.sun_path[(sizeof name.un_sock.sun_path)-1]='\0';
      strncpy(name.un_sock.sun_path,hostname,(sizeof name.un_sock.sun_path)-1);
      sock_size = sizeof name.un_sock;
      printf ("Connecting client to %s\n", name.un_sock.sun_path);
   }

   if (connect(sock, (struct sockaddr*) &name, sock_size) < 0) {
      perror("connect");
      exit(1);
   }
   printf("Client connected.\n");
   return(sock);
}

char const *
sockaddr_to_string(struct sockaddr *addr, char *buf, int size)
{
   struct sockaddr_in *addr_in;
   char *ptr = buf, *end = buf + size;

   switch (addr->sa_family) {
      case AF_INET:
         addr_in = (struct sockaddr_in *)addr;
         ptr += snprintf(ptr, end - ptr, "%u:%u:%u:%u : %hu",
               addr_in->sin_addr.s_addr & 0xff,
               (addr_in->sin_addr.s_addr >> 8) & 0xff,
               (addr_in->sin_addr.s_addr >> 16) & 0xff,
               (addr_in->sin_addr.s_addr >> 24) & 0xff,
               ntohs(addr_in->sin_port));
         break;
      default:
         ptr += snprintf(ptr, end-ptr, "(unknown)");
         break;
   }
   return buf;
}

/*
 * Open a generic socket to listen for incoming connections on the
 * given port.
 */
int
server(char *port)
{
   int sock, newsock;
   short sa_family;
   int sock_size;
   union {
      struct sockaddr_in in_sock;
      struct sockaddr_un un_sock;
   } name;
   struct sockaddr addr;
   socklen_t addrlen;
   char buf[128];

   if (atoi(port) == 0) {
      sa_family = AF_UNIX;
   } else {
      sa_family = AF_INET;
   }

   if ((sock = socket(sa_family, SOCK_STREAM, 0)) < 0) {
      perror("socket");
      exit(1);
   }

   if (sa_family == AF_INET) {
      name.in_sock.sin_family = AF_INET;
      name.in_sock.sin_addr.s_addr = INADDR_ANY;
      name.in_sock.sin_port = htons((u_short)atoi(port));
      sock_size = sizeof name.in_sock;
      printf ("Starting server at localhost:%d\n", atoi(port));
   } else {
      name.un_sock.sun_family = AF_UNIX;
      name.un_sock.sun_path[(sizeof name.un_sock.sun_path)-1]='\0';
      strncpy(name.un_sock.sun_path, port, (sizeof name.un_sock.sun_path)-1);
      sock_size = sizeof name.un_sock;
      printf ("Starting server at %s\n", name.un_sock.sun_path);
   }

   if (bind(sock, (struct sockaddr *) &name, sock_size) < 0) {
      perror("bind");
      exit(1);
   }

   listen(sock, 5);

   if ((newsock = accept(sock, &addr, &addrlen)) < 0) {
      perror("accept");
      exit(1);
   }

   printf("Connection accepted. Remote addr = %s\n",
          sockaddr_to_string(&addr, buf, sizeof buf));

   close(sock);
   return(newsock);
}

int
main(argc, argv)
    int argc;
    char **argv;
{
   int sock;

   if (argc < 3 || argc > 4) {
      printf("usage: %s client|server [host] <port>\n", argv[0]);
      exit(1);
   }

   if (!strcmp(argv[1], "client")) {
      if (argc == 4) {
         sock = client(argv[2], atoi(argv[3]));
      } else {
         sock = client(argv[2], 0);
      }
      talk(sock);
   } else if (!strcmp(argv[1], "server")) {
      if (argc == 4) {
         /* the user has given <host> <port> combination */
         sock = server(argv[3]);
      } else {
         sock = server(argv[2]);
      }
      talk(sock);
   }
   return 0;
}
