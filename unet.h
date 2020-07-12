// unet.h - single-header BSD sockets wrapper with Windows support
//
// Mostly just renamed and reformatted https://github.com/BareRose/swrap ; also see
// https://github.com/Smilex/zed_net and https://github.com/kieselsteini/sts

/*
To use, include whereever needed and, in exactly one source file, place following before the #include:
#define UNET_IMPLEMENTATION

Client usage (error checking not shown):
  const char* msg = "hello";
  char buff[LEN];
  unet_init();
  int sock = unet_socket(UNET_TCP, UNET_CONNECT, UNET_DEFAULT, "example.com", "80");
  unet_send(sock, msg, strlen(msg));
  int n = unet_recv(sock, buff, LEN);
  if(n > 0) printf("%.*s", n, buff);
  unet_close(sock);
  unet_terminate();

*/

// header section
#ifndef UNET_H
#define UNET_H

//constants
#define UNET_TCP 0
#define UNET_UDP 1
#define UNET_BIND 0
#define UNET_CONNECT 1
#define UNET_DEFAULT 0x00
#define UNET_NOBLOCK 0x01
#define UNET_NODELAY 0x02
#define UNET_SHUT_RD 0
#define UNET_SHUT_WR 1
#define UNET_SHUT_RDWR 2
#define UNET_RDY_RD 1
#define UNET_RDY_WR 2

//structs
struct unet_addr {
  char data[128]; //enough space to hold any kind of address
};

// initializes socket functionality, returns 0 on success
int unet_init();

// terminates socket functionality
void unet_terminate();

// protocol-agnostically creates a new socket configured according to the given parameters
// sockets have to be created and bound/connected all at once to allow for protocol-agnosticity
// int: Protocol of the socket, either UNET_TCP or UNET_UDP for TCP or UDP respectively
//  UNET_TCP: TCP protocol connection-oriented reliable delivery, see unetListen/Accept
//  UNET_UDP: UDP protocol connectionless unreliable, UNET_CONNECT just assigns correspondent
// int: Mode of the socket
//  UNET_BIND: Bind to given address (or all interfaces if NULL) and port, e.g. for a server
//  UNET_CONNECT: Connect to given address (localhost if NULL) and port, e.g. for a client
// char: Configuration flags, either UNET_DEFAULT or a bitwise combination of flags
//  UNET_NOBLOCK: Sets the socket to be non-blocking, default is blocking
//  UNET_NODELAY: Disables Nagle's for TCP sockets, default is enabled
// char*: Host/address as a string, can be IPv4, IPv6, etc...
// char*: Service/port as a string, e.g. "1728" or "http"
// returns socket handle, or -1 on failure
int unet_socket(int prot, int mode, char flags, const char* host, const char* serv);

// closes the given socket
void unet_close(int sock);

// shutdown the socket - can be called from a different thread to cancel I/O waits w/o closing
int unet_shutdown(int sock, int how);

// SO_LINGER: wait for all data to be sent (or timeout) before closing (timeout > 0) or close immediately w/
//  RST (timeout == 0); pass timeout < 0 to disable ... only works for blocking socket! (?)
int unet_linger(int sock, int timeout);

// configures the given socket (must be UNET_TCP + UNET_BIND) to listen for new connections with given backlog
// returns 0 on success, non-zero on failure
int unet_listen(int sock, int blog);

// uses the given socket (must be unetListen) to accept a new incoming connection, optionally returning its address
// returns a socket handle for the new connection, or -1 on failure (e.g. if there are no new connections)
int unet_accept(int sock, struct unet_addr* addr);

// writes the address the given socket is bound to into given address pointer, useful when automatically assigning port
// returns 0 on success, non-zero on failure
int unet_address(int sock, struct unet_addr* addr);

// writes the host/address and service/port of given address into given buffers (pointer + size), one buffer may be NULL
// returns 0 on success, non-zero on failure
int unet_address_info(struct unet_addr* addr, char* host, int host_size, char* serv, int serv_size);

// uses the given socket (either UNET_CONNECT or returned by unetAccept) to send `len` bytes from `data`
// returns how much data was actually sent (may be less than data size), or -1 on failure
int unet_send(int sock, const char* data, int len);

// receive up to `len` bytes into buffer `data` using given socket (either UNET_CONNECT or returned by unetAccept)
// returns the number of bytes received, 0 on orderly shutdown, or -1 on failure (e.g. no data to receive)
int unet_recv(int sock, char* data, int len);

// uses the given socket to send given data (pointer + size) to the given unet_addr (e.g. from unetReceiveFrom)
// returns how much data was actually sent (may be less than data size), or -1 on failure
int unet_send_to(int sock, struct unet_addr* addr, const char* data, int len);

// receives data using given socket into given buffer (pointer + size), optionally returning sender's address
// returns the number of bytes received, 0 on orderly shutdown, or -1 on failure (e.g. no data to receive)
int unet_receive_from(int sock, struct unet_addr* addr, char* data, int len);

// get the number of bytes avail to read from the socket - handy for preallocating a buffer
int unet_bytes_avail(int sock);

// waits until timeout elapses (returning 0) or rdsock is ready for read (returning UNET_RDY_RD) and/or
//  wrsock (typically but not necessarily the same socket) is ready for write (returning UNET_RDY_WR)
// rdsock or wrsock are ignored if < 0; returns -1 on error; timeout < 0 blocks indefinitely
int unet_select(int rdsock, int wrsock, double timeout);

// waits either until a socket in given list has new data to receive or given time (in seconds) has passed
// if the given list length is 0 an empty select will be performed, which is just a sub-second sleep
// returns number of sockets with new data, 0 if timeout was reached, and -1 on error
// if ready is not NULL, ready[i] is set if socks[i] has new data
int unet_multi_select(int* socks, int* ready, int socks_size, double timeout);

#endif //UNET_H

#ifdef UNET_IMPLEMENTATION
#undef UNET_IMPLEMENTATION

#ifdef _WIN32
#include <ws2tcpip.h>
#undef errno
#define errno WSAGetLastError()
static int unet_would_block(int err) { return err == WSAEWOULDBLOCK; }
#else //unix
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <netinet/tcp.h>  // only for TCP_NODELAY
#include <netinet/in.h>  // only needed on Android
static int unet_would_block(int err) { return err == EAGAIN || err == EINPROGRESS; }
#endif
#include <stddef.h> //NULL

int unet_init()
{
#ifdef _WIN32
  WSADATA WsaData;
  return (WSAStartup(MAKEWORD(2,2), &WsaData) != NO_ERROR);
#else
  // prevent error in send() from crashing entire application (!)
  signal(SIGPIPE, SIG_IGN);  // or we could pass MSG_NOSIGNAL to send() on Linux / set SO_NOSIGPIPE on BSD
  return 0;
#endif
}

int unet_socket(int prot, int mode, char flags, const char* host, const char* serv)
{
  int sock = -1;
  struct addrinfo* ai_top = NULL;
  struct addrinfo* ai = NULL;
  struct addrinfo hint = {
    (mode == UNET_BIND) ? AI_PASSIVE : 0, //ai_flags
    AF_UNSPEC, //ai_family
    (prot == UNET_TCP) ? SOCK_STREAM : SOCK_DGRAM, //ai_socktype
    0, 0, NULL, NULL, NULL};
  // DNS lookup (blocking)
  if(getaddrinfo(host, serv, &hint, &ai_top)) return -1;
  // prefer IPv4 if available
  ai = ai_top;
  if(ai->ai_family == AF_INET6 && ai->ai_next && ai->ai_next->ai_family == AF_INET)
    ai = ai->ai_next;
  // create socket
  #ifdef _WIN32
    SOCKET wsck = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if(wsck == INVALID_SOCKET) return -1;
    // reject socket handle outside int range
    if(wsck > INT_MAX) {
      closesocket(wsck);
      goto error;
    }
    // convert to int
    sock = wsck;
  #else
    sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if(sock == -1) goto error;
  #endif
  // Apparently TCP_NODELAY can only be set after connect on Windows?
#ifndef _WIN32
  //make sure IPV6_ONLY is disabled
  if(ai->ai_family == AF_INET6) {
    int no = 0;
    setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (void*)&no, sizeof(no));
  }
  //set TCP_NODELAY if applicable
  if(prot == UNET_TCP) {
    int nodelay = (flags&UNET_NODELAY);
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (void*)&nodelay, sizeof(nodelay));
  }
#endif
  //bind if applicable
  if(mode == UNET_BIND && bind(sock, ai->ai_addr, ai->ai_addrlen))
    goto error;
  //set non-blocking if needed
  if(flags & UNET_NOBLOCK) {
#ifdef _WIN32
    DWORD no_block = 1;
    if(ioctlsocket(sock, FIONBIO, &no_block))
      goto error;
#else
    if(fcntl(sock, F_SETFL, O_NONBLOCK, 1) == -1)
      goto error;
#endif
  }
  if(mode == UNET_CONNECT && connect(sock, ai->ai_addr, ai->ai_addrlen) != 0 && !unet_would_block(errno))
    goto error;
  freeaddrinfo(ai_top);
  return sock;
error:
  freeaddrinfo(ai_top);
  if(sock != -1) unet_close(sock);
  return -1;
}

void unet_close(int sock)
{
#ifdef _WIN32
  closesocket(sock);
#else
  close(sock);
#endif
}

int unet_shutdown(int sock, int how)
{
  return shutdown(sock, how);
}

void unet_terminate()
{
#ifdef _WIN32
  WSACleanup();
#endif
}

int unet_linger(int sock, int timeout)
{
  struct linger sl;
  sl.l_onoff = timeout >= 0 ? 1 : 0;  // 1 = enable linger
  sl.l_linger = timeout;
#ifdef _WIN32
  return setsockopt(sock, SOL_SOCKET, SO_LINGER, (char*)&sl, sizeof(sl));
#else
  return setsockopt(sock, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl));
#endif
}

// connection functions
int unet_listen(int sock, int blog)
{
  return listen(sock, blog);
}

int unet_accept(int sock, struct unet_addr* addr)
{
  #ifdef _WIN32
    int addr_size = sizeof(struct unet_addr);
    SOCKET wsck = accept(sock, (struct sockaddr*)addr, (addr) ? &addr_size : NULL);
    if(wsck == INVALID_SOCKET) return -1;
    //reject socket handle outside int range
    if(wsck > INT_MAX) {
      closesocket(wsck);
      return -1;
    }
    //return new socket
    return wsck;
  #else
    socklen_t addr_size = sizeof(struct unet_addr);
    return accept(sock, (struct sockaddr*)addr, (addr) ? &addr_size : NULL);
  #endif
}

//address functions
int unet_address(int sock, struct unet_addr* addr)
{
  #ifdef _WIN32
    int addr_size = sizeof(struct unet_addr);
  #else
    socklen_t addr_size = sizeof(struct unet_addr);
  #endif
  return getsockname(sock, (struct sockaddr*)addr, &addr_size);
}

int unet_address_info(struct unet_addr* addr, char* host, int host_size, char* serv, int serv_size)
{
  return getnameinfo((struct sockaddr*)addr, sizeof(struct unet_addr), host, host_size, serv, serv_size, 0);
}

//send/receive functions
int unet_send(int sock, const char* data, int len)
{
  return send(sock, data, len, 0);
}

int unet_recv(int sock, char* data, int len)
{
  // last arg is flags; possibly useful values are MSG_DONTWAIT (overriding blocking socket) and MSG_PEEK
  // with MSG_DONTWAIT and no data available, EAGAIN or EWOULDBLOCK is returned
  return recv(sock, data, len, 0);
}

int unet_send_to(int sock, struct unet_addr* addr, const char* data, int len)
{
  return sendto(sock, data, len, 0, (struct sockaddr*)addr, sizeof(struct unet_addr));
}

int unet_receive_from(int sock, struct unet_addr* addr, char* data, int len)
{
  #ifdef _WIN32
    int addr_size = sizeof(struct unet_addr);
  #else
    socklen_t addr_size = sizeof(struct unet_addr);
  #endif
  return recvfrom(sock, data, len, 0, (struct sockaddr*)addr, &addr_size);
}

int unet_bytes_avail(int sock)
{
#ifdef _WIN32
  unsigned long n = 0;
  ioctlsocket(sock, FIONREAD, &n);
#else
  int n = 0;
  ioctl(sock, FIONREAD, &n);
#endif
  return n;
}

// seems it might not be safe to reuse a fd_set, since it can be modified by select() ... so there is no
//  advantage to providing a socket set abstraction for the single socket case
int unet_select(int rdsock, int wrsock, double timeout)
{
  fd_set rdset;
  fd_set wrset;
  struct timeval time;
  int res = -1, max_sock = rdsock > wrsock ? rdsock : wrsock;
  if(rdsock >= 0) {
    FD_ZERO(&rdset);
    FD_SET(rdsock, &rdset);
  }
  if(wrsock >= 0) {
    FD_ZERO(&wrset);
    FD_SET(wrsock, &wrset);
  }
  time.tv_sec = timeout;
  time.tv_usec = (timeout - time.tv_sec)*1000000.0;
  res = select(max_sock+1,
      (rdsock >= 0 ? &rdset : NULL), (wrsock >= 0 ? &wrset : NULL), NULL, (timeout < 0 ? NULL : &time));
  if(res <= 0)
    return res;
  return (rdsock >= 0 && FD_ISSET(rdsock, &rdset) ? UNET_RDY_RD : 0)
      | (wrsock >= 0 && FD_ISSET(wrsock, &wrset) ? UNET_RDY_WR : 0);
}

int unet_multi_select(int* socks, int* ready, int socks_size, double timeout)
{
  fd_set set;
  struct timeval time;
  int sock_max = -1;
  int res;
  //fd set
  FD_ZERO(&set);
  for(int i = 0; i < socks_size; i++) {
    if(socks[i] > sock_max) sock_max = socks[i];
    if(socks[i] > -1) FD_SET(socks[i], &set);
  }
  //timeout
  time.tv_sec = timeout;
  time.tv_usec = (timeout - time.tv_sec)*1000000.0;
  // select
  res = select(sock_max+1, &set, NULL, NULL, timeout < 0 ? NULL : &time);
  if(res > 0 && ready) {
    for(int i = 0; i < socks_size; i++)
      ready[i] = FD_ISSET(socks[i], &set); // ? 1 : 0;
  }
  return res;
}

#endif //UNET_IMPLEMENTATION
