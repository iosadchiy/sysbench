/* Copyright (C) 2004 MySQL AB
   Copyright (C) 2004-2018 Alexey Kopytov <akopytov@gmail.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_PTHREAD_H
# include <pthread.h>
#endif

#include "sysbench.h"
#include "sb_ck_pr.h"
#include "sb_rand.h"

typedef struct
{
  pthread_mutex_t mutex;
  char            pad[256];
} thread_lock;


/* Mutex test arguments */
static sb_arg_t ipi_args[] =
{
  SB_OPT("mutex-num", "total size of mutex array", "4096", INT),
  SB_OPT("mutex-locks", "number of mutex locks to do per thread", "50000", INT),
  SB_OPT("mutex-loops", "number of empty loops to do outside mutex lock",
         "10000", INT),

  SB_OPT_END
};

/* Mutex test operations */
static int ipi_init(void);
static int ipi_thread_run(int);
static void ipi_print_mode(void);
static sb_event_t ipi_next_event(int);
static int ipi_execute_event(sb_event_t *, int);
static int ipi_done(void);

static sb_test_t ipi_test =
{
  .sname = "ipi",
  .lname = "Inter-processor interrupts performance test",
  .ops = {
     .init = ipi_init,
     .thread_run = ipi_thread_run,
     .print_mode = ipi_print_mode,
     .next_event = ipi_next_event,
     .execute_event = ipi_execute_event,
     .done = ipi_done
  },
  .args = ipi_args
};


static thread_lock *thread_locks;
static unsigned int mutex_num;
static unsigned int mutex_loops;
static unsigned int mutex_locks;
static unsigned int global_var;

static TLS int tls_counter;

int register_test_ipi(sb_list_t *tests)
{
  SB_LIST_ADD_TAIL(&ipi_test.listitem, tests);

  return 0;
}


int ipi_init(void)
{
  unsigned int i;
  
  mutex_num = sb_get_value_int("mutex-num");
  mutex_loops = sb_get_value_int("mutex-loops");
  mutex_locks = sb_get_value_int("mutex-locks");

  thread_locks = (thread_lock *)malloc(mutex_num * sizeof(thread_lock));
  if (thread_locks == NULL)
  {
    log_text(LOG_FATAL, "Memory allocation failure!");
    return 1;
  }

  for (i = 0; i < mutex_num; i++)
    pthread_mutex_init(&thread_locks[i].mutex, NULL);
  
  return 0;
}


int ipi_done(void)
{
  unsigned int i;

  for(i=0; i < mutex_num; i++)
    pthread_mutex_destroy(&thread_locks[i].mutex);
  free(thread_locks);
  
  return 0;
}

static int ipi_thread_run(int thread_id)
{
  sb_test_t *test = &ipi_test;

  sb_event_t        event;
  int               rc = 0;

  cpu_set_t cpuset;
  pthread_t thread;
  thread = pthread_self();
  CPU_ZERO(&cpuset);
  CPU_SET(thread_id, &cpuset);
  pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);

  while (sb_more_events(thread_id) && rc == 0)
  {
    event = test->ops.next_event(thread_id);
    if (event.type == SB_REQ_TYPE_NULL)
      break;

    sb_event_start(thread_id);

    rc = test->ops.execute_event(&event, thread_id);

    sb_event_stop(thread_id);
  }

  return rc;
}


sb_event_t ipi_next_event(int thread_id)
{
  sb_event_t         sb_req;
  sb_mutex_request_t   *mutex_req = &sb_req.u.mutex_request;

  (void) thread_id; /* unused */

  /* Perform only one request per thread */
  if (tls_counter++ > 0)
    sb_req.type = SB_REQ_TYPE_NULL;
  else
  {
    sb_req.type = SB_REQ_TYPE_MUTEX;
    mutex_req->nlocks = mutex_locks;
    mutex_req->nloops = mutex_loops;
  }

  return sb_req;
}


int ipi_execute_event(sb_event_t *sb_req, int thread_id)
{
  unsigned int         i;
  unsigned int         current_lock;
  sb_mutex_request_t   *mutex_req = &sb_req->u.mutex_request;

  (void) thread_id; /* unused */

  do
  {
    current_lock = sb_rand_uniform(0, mutex_num - 1);

    for (i = 0; i < mutex_req->nloops; i++)
      ck_pr_barrier();

    pthread_mutex_lock(&thread_locks[current_lock].mutex);
    global_var += global_var*sb_rand_uniform(0,10);
    pthread_mutex_unlock(&thread_locks[current_lock].mutex);
    mutex_req->nlocks--;
  }
  while (mutex_req->nlocks > 0);

  return 0;
}


void ipi_print_mode(void)
{
  log_text(LOG_INFO, "Doing mutex performance test");
}

