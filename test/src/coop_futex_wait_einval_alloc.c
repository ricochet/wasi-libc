#include "test.h"
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <wasi/libc.h>

// A futex wait with an invalid timeout must fail before it allocates anything.
// A relative timeout with a negative `tv_sec` used to be rejected only after
// the waitlist entry for the address had been created and referenced, so the
// entry was never released.
//
// This test is linked with `-Wl,--wrap=malloc` so it can count allocations.

#define TEST(c)                                                                \
  do {                                                                         \
    if (!(c))                                                                  \
      t_error("%s failed\n", #c);                                              \
  } while (0)

void *__real_malloc(size_t);
static volatile int mallocs;

void *__wrap_malloc(size_t n) {
  mallocs++;
  return __real_malloc(n);
}

static volatile int words[16];

int main(void) {
  // Make sure the futex table exists, so only per-address allocations remain.
  struct timespec zero = {0, 0};
  TEST(__wasilibc_futex_wait(&words[0], 0, CLOCK_MONOTONIC, &zero, 0) ==
       -ETIMEDOUT);

  struct timespec negative = {-1, 0};
  mallocs = 0;
  for (int i = 0; i < 16; i++)
    TEST(__wasilibc_futex_wait(&words[i], 0, CLOCK_MONOTONIC, &negative, 0) ==
         -EINVAL);
  TEST(mallocs == 0);

  return t_status;
}
