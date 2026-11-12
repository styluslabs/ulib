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
#define UNET_POLLIN 0x01
#define UNET_POLLOUT 0x02
#define UNET_POLLERR 0x04
#define UNET_POLLHUP 0x08

//structs
struct unet_addr {
  char data[128]; //enough space to hold any kind of address
};

// for unet_poll() - own struct (not struct pollfd) so that this header doesn't need system socket headers
struct unet_pollfd {
  int sock;  // ignored if < 0
  short events;  // UNET_POLLIN and/or UNET_POLLOUT
  short revents;  // set by unet_poll(): UNET_POLLIN, UNET_POLLOUT, UNET_POLLERR, UNET_POLLHUP
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
// char*: Service/port as a string, e.g. "1728" or "http"; "0" w/ UNET_BIND to choose free port (see unet_address)
// UNET_CONNECT prefers IPv4 if host has IPv4 and IPv6 addresses; UNET_BIND w/ host NULL binds to all interfaces,
//  preferring dual stack IPv6 (accepts IPv4 connections too), falling back to IPv4; UNET_BIND sets SO_REUSEADDR
//  (except on Windows, where it allows stealing the port) so that server can be restarted immediately
// returns socket handle, or -1 on failure
int unet_socket(int prot, int mode, char flags, const char* host, const char* serv);

// applies UNET_NOBLOCK and/or UNET_NODELAY flags (see unet_socket) to existing socket, e.g. from unet_accept
// returns 0 on success, non-zero on failure
int unet_set_flags(int sock, char flags);

// returns last socket error code (errno or WSAGetLastError())
int unet_errno();

// returns non-zero if error code (from unet_errno) means operation on a non-blocking socket would block (or
//  was interrupted by a signal) and should be retried later
int unet_would_block(int err);

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

// writes numeric "host:port" for address into buf (IPv4-mapped IPv6 addresses written as IPv4; other IPv6 as
//  [host]:port); no DNS lookup, unlike unet_address_info; returns 0 on success, non-zero on failure
int unet_address_str(struct unet_addr* addr, char* buf, int size);

// returns port of address (IPv4 or IPv6), or -1 on failure
int unet_address_port(struct unet_addr* addr);

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

// waits until timeout (seconds; < 0 blocks indefinitely) elapses or any socket in fds has requested event (or
//  error/hangup, which are always reported); unlike select(), no limit on socket values or count
// returns number of entries w/ revents set, 0 on timeout or if interrupted by signal, or -1 on error
int unet_poll(struct unet_pollfd* fds, int nfds, double timeout);

// waits until timeout elapses (returning 0) or rdsock is ready for read (returning UNET_RDY_RD) and/or
//  wrsock (typically but not necessarily the same socket) is ready for write (returning UNET_RDY_WR); error or
//  hangup is reported as ready (as select() does), e.g., failed non-blocking connect is reported as UNET_RDY_WR
// rdsock or wrsock are ignored if < 0; returns -1 on error; timeout < 0 blocks indefinitely
int unet_select(int rdsock, int wrsock, double timeout);

// waits either until a socket in given list has new data to receive or given time (in seconds) has passed
// if the given list length is 0, this just sleeps for timeout
// returns number of sockets with new data, 0 if timeout was reached, and -1 on error
// if ready is not NULL, ready[i] is set if socks[i] has new data (or error/hangup)
int unet_multi_select(int* socks, int* ready, int socks_size, double timeout);

#endif //UNET_H

#ifdef UNET_IMPLEMENTATION
#undef UNET_IMPLEMENTATION

#ifdef _WIN32
#include <ws2tcpip.h>
#undef errno
#define errno WSAGetLastError()
int unet_would_block(int err) { return err == WSAEWOULDBLOCK || err == WSAEINTR; }
#else //unix
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <netinet/tcp.h>  // only for TCP_NODELAY
#include <netinet/in.h>  // only needed on Android
int unet_would_block(int err) { return err == EAGAIN || err == EWOULDBLOCK || err == EINPROGRESS || err == EINTR; }
#endif
#include <stddef.h> //NULL
#include <stdlib.h>  //malloc
#include <stdio.h>  //snprintf
#include <string.h>  //memcpy, strchr
#include <limits.h>  //INT_MAX

int unet_errno() { return errno; }

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

int unet_set_flags(int sock, char flags)
{
  if(flags & UNET_NODELAY) {
    int nodelay = 1;
    if(setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay)))
      return -1;
  }
  if(flags & UNET_NOBLOCK) {
#ifdef _WIN32
    u_long no_block = 1;
    if(ioctlsocket(sock, FIONBIO, &no_block))
      return -1;
#else
    int fl = fcntl(sock, F_GETFL, 0);
    if(fl == -1 || fcntl(sock, F_SETFL, fl | O_NONBLOCK) == -1)
      return -1;
#endif
  }
  return 0;
}

// create socket for address, set options, and bind or connect; returns -1 on failure
static int unet_socket_addr(struct addrinfo* ai, int prot, int mode, char flags)
{
  int sock = -1;
  #ifdef _WIN32
    SOCKET wsck = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if(wsck == INVALID_SOCKET) return -1;
    // reject socket handle outside int range
    if(wsck > INT_MAX) {
      closesocket(wsck);
      return -1;
    }
    // convert to int
    sock = (int)wsck;
  #else
    sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if(sock == -1) return -1;
  #endif
  //make sure IPV6_ONLY is disabled (so IPv6 socket bound to all interfaces accepts IPv4 too)
  if(ai->ai_family == AF_INET6) {
    int no = 0;
    setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&no, sizeof(no));
  }
#ifndef _WIN32
  // Apparently TCP_NODELAY can only be set after connect on Windows?
  if(prot == UNET_TCP) {
    int nodelay = (flags&UNET_NODELAY) ? 1 : 0;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
  }
  // allow server to be restarted immediately (Windows SO_REUSEADDR allows stealing port, so not used)
  if(mode == UNET_BIND && prot == UNET_TCP) {
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
  }
#endif
  //bind if applicable
  if(mode == UNET_BIND && bind(sock, ai->ai_addr, (int)ai->ai_addrlen))
    goto error;
  //set non-blocking if needed
  if((flags & UNET_NOBLOCK) && unet_set_flags(sock, UNET_NOBLOCK))
    goto error;
  if(mode == UNET_CONNECT && connect(sock, ai->ai_addr, (int)ai->ai_addrlen) != 0 && !unet_would_block(errno))
    goto error;
  return sock;
error:
  unet_close(sock);
  return -1;
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
  // prefer IPv4 for connect and bind to specific host; prefer IPv6 (dual stack) when binding to all interfaces
  int prefer = (mode == UNET_BIND && !host) ? AF_INET6 : AF_INET;
  int pass;
  // DNS lookup (blocking)
  if(getaddrinfo(host, serv, &hint, &ai_top)) return -1;
  // for bind, try addresses in order of preference until one works; for connect, only the first (preferred)
  //  address is tried, since non-blocking connect doesn't fail immediately
  for(pass = 0; pass < 2 && sock == -1; ++pass) {
    for(ai = ai_top; ai && sock == -1; ai = ai->ai_next) {
      if((ai->ai_family == prefer) != (pass == 0)) continue;
      sock = unet_socket_addr(ai, prot, mode, flags);
      if(mode == UNET_CONNECT) { pass = 2; break; }
    }
  }
  freeaddrinfo(ai_top);
  return sock;
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

int unet_address_str(struct unet_addr* addr, char* buf, int size)
{
  char host[128], serv[32];
  const char* h = host;
  int res = getnameinfo((struct sockaddr*)addr, sizeof(struct unet_addr), host, sizeof(host), serv, sizeof(serv),
      NI_NUMERICHOST | NI_NUMERICSERV);
  if(res) return res;
  if(strncmp(h, "::ffff:", 7) == 0 && strchr(h + 7, '.')) h += 7;  // IPv4 mapped
  res = snprintf(buf, size, strchr(h, ':') ? "[%s]:%s" : "%s:%s", h, serv);
  return res < 0 || res >= size;
}

int unet_address_port(struct unet_addr* addr)
{
  struct sockaddr_storage ss;  // copy since unet_addr may not be suitably aligned
  memcpy(&ss, addr, sizeof(ss) < sizeof(*addr) ? sizeof(ss) : sizeof(*addr));
  if(ss.ss_family == AF_INET) return ntohs(((struct sockaddr_in*)&ss)->sin_port);
  if(ss.ss_family == AF_INET6) return ntohs(((struct sockaddr_in6*)&ss)->sin6_port);
  return -1;
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

int unet_poll(struct unet_pollfd* fds, int nfds, double timeout)
{
#ifdef _WIN32
  typedef WSAPOLLFD unet_sys_pollfd;
#else
  typedef struct pollfd unet_sys_pollfd;
#endif
  int ms = timeout < 0 ? -1 : (int)(timeout*1000 + 0.999);  // round up so small timeouts don't become 0 (busy loop)
  int ii, n = 0, res;
  unet_sys_pollfd stackfds[16];
  int stackidx[16];
  unet_sys_pollfd* pfds = nfds <= 16 ? stackfds : (unet_sys_pollfd*)malloc(nfds*sizeof(unet_sys_pollfd));
  int* idx = nfds <= 16 ? stackidx : (int*)malloc(nfds*sizeof(int));  // fds index for each pfds entry
  if(!pfds || !idx) {
    if(pfds != stackfds) free(pfds);
    if(idx != stackidx) free(idx);
    return -1;
  }
  for(ii = 0; ii < nfds; ++ii) {
    fds[ii].revents = 0;
    if(fds[ii].sock < 0) continue;  // POSIX poll ignores negative fd, but older WSAPoll fails
    pfds[n].fd = fds[ii].sock;
    pfds[n].events = ((fds[ii].events & UNET_POLLIN) ? POLLIN : 0) | ((fds[ii].events & UNET_POLLOUT) ? POLLOUT : 0);
    pfds[n].revents = 0;
    idx[n++] = ii;
  }
#ifdef _WIN32
  if(n == 0) {  // WSAPoll fails w/ no sockets
    Sleep(ms < 0 ? INFINITE : (DWORD)ms);
    res = 0;
  }
  else
    res = WSAPoll(pfds, (ULONG)n, ms);
#else
  res = poll(pfds, (nfds_t)n, ms);
#endif
  if(res < 0 && unet_would_block(errno))
    res = 0;  // interrupted
  if(res > 0) {
    for(ii = 0; ii < n; ++ii) {
      short r = pfds[ii].revents;
      fds[idx[ii]].revents = ((r & POLLIN) ? UNET_POLLIN : 0) | ((r & POLLOUT) ? UNET_POLLOUT : 0)
          | ((r & (POLLERR | POLLNVAL)) ? UNET_POLLERR : 0) | ((r & POLLHUP) ? UNET_POLLHUP : 0);
    }
  }
  if(pfds != stackfds) free(pfds);
  if(idx != stackidx) free(idx);
  return res;
}

int unet_select(int rdsock, int wrsock, double timeout)
{
#ifdef _WIN32
  // select() on Windows has no limit on socket values (FD_SETSIZE is a count) and, unlike WSAPoll before
  //  Windows 10 2004, reports failed non-blocking connect
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
  time.tv_sec = (long)timeout;
  time.tv_usec = (long)((timeout - time.tv_sec)*1000000.0);
  res = select(max_sock+1,
      (rdsock >= 0 ? &rdset : NULL), (wrsock >= 0 ? &wrset : NULL), NULL, (timeout < 0 ? NULL : &time));
  if(res <= 0)
    return res;
  return (rdsock >= 0 && FD_ISSET(rdsock, &rdset) ? UNET_RDY_RD : 0)
      | (wrsock >= 0 && FD_ISSET(wrsock, &wrset) ? UNET_RDY_WR : 0);
#else
  // select() is undefined for socket values >= FD_SETSIZE (1024), so use poll
  struct unet_pollfd fds[2];
  int n = 0, res, ready = 0, ii;
  if(rdsock >= 0) {
    fds[n].sock = rdsock;
    fds[n++].events = UNET_POLLIN | (wrsock == rdsock ? UNET_POLLOUT : 0);
  }
  if(wrsock >= 0 && wrsock != rdsock) {
    fds[n].sock = wrsock;
    fds[n++].events = UNET_POLLOUT;
  }
  res = unet_poll(fds, n, timeout);
  if(res <= 0)
    return res;
  for(ii = 0; ii < n; ++ii) {
    int err = fds[ii].revents & (UNET_POLLERR | UNET_POLLHUP);
    if(fds[ii].sock == rdsock && ((fds[ii].revents & UNET_POLLIN) || err))
      ready |= UNET_RDY_RD;
    if(fds[ii].sock == wrsock && ((fds[ii].revents & UNET_POLLOUT) || err))
      ready |= UNET_RDY_WR;
  }
  return ready;
#endif
}

int unet_multi_select(int* socks, int* ready, int socks_size, double timeout)
{
  struct unet_pollfd stackfds[16];
  struct unet_pollfd* fds = socks_size <= 16 ? stackfds
      : (struct unet_pollfd*)malloc(socks_size*sizeof(struct unet_pollfd));
  int i, res, n = 0;
  if(!fds) return -1;
  for(i = 0; i < socks_size; i++) {
    fds[i].sock = socks[i];
    fds[i].events = UNET_POLLIN;
  }
  res = unet_poll(fds, socks_size, timeout);
  for(i = 0; i < socks_size; i++) {
    int r = res > 0 && (fds[i].revents & (UNET_POLLIN | UNET_POLLERR | UNET_POLLHUP));
    if(ready) ready[i] = r;
    n += r;
  }
  if(fds != stackfds) free(fds);
  return res < 0 ? res : n;
}

#endif //UNET_IMPLEMENTATION
