#define _GNU_SOURCE
#include "test.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <time.h>
#include <wasi/libc.h>

// When the futex layer can't allocate its waitlist entry,
// `__wasilibc_futex_wait` must report `-ENOMEM`, and `pthread_join` must still
// wait for the thread to exit instead of returning early and freeing a stack
// that's still in use.
//
// This test is linked with `-Wl,--wrap=malloc` so it can fail the next
// allocation on demand.

#define TEST(c)                                                                \
  do {                                                                         \
    if (!(c))                                                                  \
      t_error("%s failed\n", #c);                                              \
  } while (0)

void *__real_malloc(size_t);
static volatile int fail_next_malloc;
static volatile int fail_all_mallocs;

void *__wrap_malloc(size_t n) {
  if (fail_all_mallocs)
    return NULL;
  if (fail_next_malloc) {
    fail_next_malloc = 0;
    return NULL;
  }
  return __real_malloc(n);
}

static volatile int child_done;

static void *child(void *arg) {
  (void)arg;
  for (int i = 0; i < 100; i++)
    sched_yield();
  child_done = 1;
  return (void *)0x1234;
}

static volatile int release_waiter;

static void *waiter(void *arg) {
  (void)arg;
  while (!release_waiter)
    sched_yield();
  return (void *)0x5678;
}

int main(void) {
  // The first wait has to create the futex table itself, and fails with
  // `-ENOMEM` if that can't be allocated.
  static volatile int word = 0;
  fail_next_malloc = 1;
  int rc = __wasilibc_futex_wait(&word, 0, CLOCK_MONOTONIC, NULL, 0);
  TEST(!fail_next_malloc);
  TEST(rc == -ENOMEM);

  // A wait that times out straight away creates the futex table, so the waits
  // below only need to allocate a waitlist entry for their address.
  struct timespec zero = {0, 0};
  rc = __wasilibc_futex_wait(&word, 0, CLOCK_MONOTONIC, &zero, 0);
  TEST(rc == -ETIMEDOUT);

  // With the table in place, a wait fails with `-ENOMEM` if the waitlist entry
  // for its address can't be allocated.
  fail_next_malloc = 1;
  rc = __wasilibc_futex_wait(&word, 0, CLOCK_MONOTONIC, NULL, 0);
  TEST(!fail_next_malloc);
  TEST(rc == -ENOMEM);

  // `pthread_join` waits on a futex too. If that wait fails to allocate, the
  // join must keep waiting until the thread has actually exited.
  pthread_t t;
  TEST(pthread_create(&t, NULL, child, NULL) == 0);
  void *res = NULL;
  fail_next_malloc = 1;
  TEST(pthread_join(t, &res) == 0);
  TEST(!fail_next_malloc);
  TEST(child_done);
  TEST(res == (void *)0x1234);

  // A timed join must still time out if every wait fails to allocate, rather
  // than retrying past its deadline.
  TEST(pthread_create(&t, NULL, waiter, NULL) == 0);
  struct timespec deadline;
  TEST(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
  deadline.tv_nsec += 10 * 1000 * 1000;
  if (deadline.tv_nsec >= 1000000000) {
    deadline.tv_sec++;
    deadline.tv_nsec -= 1000000000;
  }
  fail_all_mallocs = 1;
  rc = pthread_timedjoin_np(t, &res, &deadline);
  fail_all_mallocs = 0;
  TEST(rc == ETIMEDOUT);
  release_waiter = 1;
  res = NULL;
  TEST(pthread_join(t, &res) == 0);
  TEST(res == (void *)0x5678);

  return t_status;
}
