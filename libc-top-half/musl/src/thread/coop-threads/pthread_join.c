#define _GNU_SOURCE
#include "pthread_impl.h"
#include <sched.h>
#include <time.h>

static int __pthread_timedjoin_np(pthread_t t, void **res,
                                  const struct timespec *at) {
  int state = t->detach_state;

  /* Cannot join a detached thread */
  if (state >= DT_DETACHED) {
    return EINVAL;
  }

  /* If already exited, just return the result */
  if (state == DT_EXITED) {
    if (res)
      *res = t->result;
    if (t->map_base)
      free(t->map_base);
    return 0;
  }

  if (t->joiner_futex) {
    // Only one thread can wait to join at a time
    return EINVAL;
  }

  // `__timedwait` returns 0 for a spurious wakeup as well as a real one, and
  // it folds errors such as futex-table allocation failure into 0, so wait
  // until the thread has actually exited. The exiting thread sets `DT_EXITED`
  // before it wakes us.
  //
  // If the wait returns without the thread having exited, yield before waiting
  // again. Threads here are cooperative, so if the futex layer keeps failing to
  // allocate, retrying straight away would spin forever without ever letting
  // the thread we're joining run to completion.
  //
  // A wait that fails to allocate returns before it looks at the deadline, so
  // check the deadline here too. Otherwise a timed join would never time out
  // while allocations keep failing.
  t->joiner_futex = 1;
  int rc = 0;
  while (t->detach_state != DT_EXITED) {
    rc = __timedwait(&t->joiner_futex, 1, CLOCK_REALTIME, at, 0);
    if (rc != 0 || t->detach_state == DT_EXITED)
      break;
    if (at) {
      struct timespec now;
      if (__clock_gettime(CLOCK_REALTIME, &now) == 0 &&
          (now.tv_sec > at->tv_sec ||
           (now.tv_sec == at->tv_sec && now.tv_nsec >= at->tv_nsec))) {
        rc = ETIMEDOUT;
        break;
      }
    }
    sched_yield();
  }
  t->joiner_futex = 0;
  if (rc != 0) {
    return rc;
  }

  if (res)
    *res = t->result;
  if (t->map_base)
    free(t->map_base);
  return 0;
}

int __pthread_join(pthread_t t, void **res) {
  return __pthread_timedjoin_np(t, res, 0);
}

static int __pthread_tryjoin_np(pthread_t t, void **res) {
  return t->detach_state == DT_JOINABLE ? EBUSY : __pthread_join(t, res);
}

weak_alias(__pthread_tryjoin_np, pthread_tryjoin_np);
weak_alias(__pthread_timedjoin_np, pthread_timedjoin_np);
weak_alias(__pthread_join, pthread_join);
