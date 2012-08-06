#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static fd = -1;

void say(const char* format, ...) {
  struct sockaddr_in addr;
  unsigned short int port = 7979;
  char buffer[1024];
  va_list args;

  va_start(args,format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  if (fd >= 0 || (fd = socket(AF_INET, SOCK_DGRAM, 0)) >= 0) {
    memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);
    
    sendto(fd, buffer, strlen(buffer), 0, (struct sockaddr *) &addr, sizeof(addr));
    close(fd);
    fd = -1;
  }
}
