#include "test.h"
#include <errno.h>
#include <poll.h>
#include <unistd.h>

// Closing stdin after it has been used must not trap. Polling stdin opens its
// input stream, along with the future that reports the stream's result, and
// closing stdin then has to release both.

#define TEST(c)                                                                \
  do {                                                                         \
    errno = 0;                                                                 \
    if (!(c))                                                                  \
      t_error("%s failed (errno = %d)\n", #c, errno);                          \
  } while (0)

int main(void) {
  struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN};
  TEST(poll(&pfd, 1, 0) >= 0);

  TEST(close(STDIN_FILENO) == 0);

  char c;
  TEST(read(STDIN_FILENO, &c, 1) == -1 && errno == EBADF);

  return t_status;
}
