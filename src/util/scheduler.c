/*
      This file is part of GNUnet
      (C) 2009 Christian Grothoff (and other contributing authors)

      GNUnet is free software; you can redistribute it and/or modify
      it under the terms of the GNU General Public License as published
      by the Free Software Foundation; either version 2, or (at your
      option) any later version.

      GNUnet is distributed in the hope that it will be useful, but
      WITHOUT ANY WARRANTY; without even the implied warranty of
      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
      General Public License for more details.

      You should have received a copy of the GNU General Public License
      along with GNUnet; see the file COPYING.  If not, write to the
      Free Software Foundation, Inc., 59 Temple Place - Suite 330,
      Boston, MA 02111-1307, USA.
 */

/**
 * @file util/scheduler.c
 * @brief schedule computations using continuation passing style
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet_common.h"
#include "gnunet_scheduler_lib.h"
#include "gnunet_signal_lib.h"
#include "gnunet_time_lib.h"

/**
 * Linked list of pending tasks.
 */
struct Task
{
  /**
   * This is a linked list.
   */
  struct Task *next;

  /**
   * Function to run when ready.
   */
  GNUNET_SCHEDULER_Task callback;

  /**
   * Closure for the callback.
   */
  void *callback_cls;

  /**
   * Set of file descriptors this task is waiting
   * for for reading.  Once ready, this is updated
   * to reflect the set of file descriptors ready
   * for operation.
   */
  struct GNUNET_NETWORK_FDSet *read_set;

  /**
   * Set of file descriptors this task is waiting
   * for for writing.  Once ready, this is updated
   * to reflect the set of file descriptors ready
   * for operation.
   */
  struct GNUNET_NETWORK_FDSet *write_set;

  /**
   * Unique task identifier.
   */
  GNUNET_SCHEDULER_TaskIdentifier id;

  /**
   * Identifier of a prerequisite task.
   */
  GNUNET_SCHEDULER_TaskIdentifier prereq_id;

  /**
   * Absolute timeout value for the task, or
   * GNUNET_TIME_UNIT_FOREVER_ABS for "no timeout".
   */
  struct GNUNET_TIME_Absolute timeout;

  /**
   * Why is the task ready?  Set after task is added to ready queue.
   * Initially set to zero.  All reasons that have already been
   * satisfied (i.e.  read or write ready) will be set over time.
   */
  enum GNUNET_SCHEDULER_Reason reason;

  /**
   * Task priority.
   */
  enum GNUNET_SCHEDULER_Priority priority;

  /**
   * Should this task be run on shutdown?
   */
  int run_on_shutdown;

};


/**
 * Handle for the scheduling service.
 */
struct GNUNET_SCHEDULER_Handle
{

  /**
   * List of tasks waiting for an event.
   */
  struct Task *pending;

  /**
   * List of tasks ready to run right now,
   * grouped by importance.
   */
  struct Task *ready[GNUNET_SCHEDULER_PRIORITY_COUNT];

  /**
   * Identity of the last task queued.  Incremented for each task to
   * generate a unique task ID (it is virtually impossible to start
   * more than 2^64 tasks during the lifetime of a process).
   */
  GNUNET_SCHEDULER_TaskIdentifier last_id;

  /**
   * Highest number so that all tasks with smaller identifiers
   * have already completed.  Also the lowest number of a task
   * still waiting to be executed.
   */
  GNUNET_SCHEDULER_TaskIdentifier lowest_pending_id;

  /**
   * GNUNET_NO if we are running normally,
   * GNUNET_YES if we are in shutdown mode.
   */
  int shutdown;

  /**
   * Number of tasks on the ready list.
   */
  unsigned int ready_count;

  /**
   * How many tasks have we run so far?
   */
  unsigned long long tasks_run;

  /**
   * Priority of the task running right now.  Only
   * valid while a task is running.
   */
  enum GNUNET_SCHEDULER_Priority current_priority;

};


/**
 * Check that the given priority is legal (and return it).
 *
 * @param p priority value to check
 * @return p on success, 0 on error
 */
static enum GNUNET_SCHEDULER_Priority
check_priority (enum GNUNET_SCHEDULER_Priority p)
{
  if ((p >= 0) && (p < GNUNET_SCHEDULER_PRIORITY_COUNT))
    return p;
  GNUNET_assert (0);
  return 0;                     /* make compiler happy */
}


/**
 * Is a task with this identifier still pending?  Also updates
 * "lowest_pending_id" as a side-effect (for faster checks in the
 * future), but only if the return value is "GNUNET_NO" (and
 * the "lowest_pending_id" check failed).
 *
 * @param sched the scheduler
 * @param id which task are we checking for
 * @return GNUNET_YES if so, GNUNET_NO if not
 */
static int
is_pending (struct GNUNET_SCHEDULER_Handle *sched,
            GNUNET_SCHEDULER_TaskIdentifier id)
{
  struct Task *pos;
  enum GNUNET_SCHEDULER_Priority p;
  GNUNET_SCHEDULER_TaskIdentifier min;

  if (id < sched->lowest_pending_id)
    return GNUNET_NO;
  min = -1;                     /* maximum value */
  pos = sched->pending;
  while (pos != NULL)
    {
      if (pos->id == id)
        return GNUNET_YES;
      if (pos->id < min)
        min = pos->id;
      pos = pos->next;
    }
  for (p = 0; p < GNUNET_SCHEDULER_PRIORITY_COUNT; p++)
    {
      pos = sched->ready[p];
      while (pos != NULL)
        {
          if (pos->id == id)
            return GNUNET_YES;
          if (pos->id < min)
            min = pos->id;
          pos = pos->next;
        }
    }
  sched->lowest_pending_id = min;
  return GNUNET_NO;
}


/**
 * Update all sets and timeout for select.
 *
 * @param sched the scheduler
 * @param rs read-set, set to all FDs we would like to read (updated)
 * @param ws write-set, set to all FDs we would like to write (updated)
 * @param timeout next timeout (updated)
 */
static void
update_sets (struct GNUNET_SCHEDULER_Handle *sched,
             struct GNUNET_NETWORK_FDSet * rs, 
	     struct GNUNET_NETWORK_FDSet * ws, 
	     struct GNUNET_TIME_Relative *timeout)
{
  struct Task *pos;

  pos = sched->pending;
  while (pos != NULL)
    {

      if ((pos->prereq_id != GNUNET_SCHEDULER_NO_TASK) &&
          (GNUNET_YES == is_pending (sched, pos->prereq_id)))
        {
          pos = pos->next;
          continue;
        }

      if (pos->timeout.value != GNUNET_TIME_UNIT_FOREVER_ABS.value)
        {
          struct GNUNET_TIME_Relative to;

          to = GNUNET_TIME_absolute_get_remaining (pos->timeout);
          if (timeout->value > to.value)
              *timeout = to;
        }

      GNUNET_NETWORK_fdset_add (rs, pos->read_set);
      GNUNET_NETWORK_fdset_add (ws, pos->write_set);
      pos = pos->next;
    }
}


/**
 * Check if the ready set overlaps with the set we want to have ready.
 * If so, update the want set (set all FDs that are ready).  If not,
 * return GNUNET_NO.
 *
 * @param ready set that is ready
 * @param want set that we want to be ready
 * @return GNUNET_YES if there was some overlap
 */
static int
set_overlaps (const struct GNUNET_NETWORK_FDSet * ready, 
	      struct GNUNET_NETWORK_FDSet * want)
{
  if (GNUNET_NETWORK_fdset_overlap (ready, want))
    {
      /* copy all over (yes, there maybe unrelated bits,
         but this should not hurt well-written clients) */
      GNUNET_NETWORK_fdset_copy (want, ready);
      return GNUNET_YES;
    }
  return GNUNET_NO;
}


/**
 * Check if the given task is eligible to run now.
 * Also set the reason why it is eligible.
 *
 * @param sched the scheduler
 * @param task task to check if it is ready
 * @param now the current time
 * @param rs set of FDs ready for reading
 * @param ws set of FDs ready for writing
 * @return GNUNET_YES if we can run it, GNUNET_NO if not.
 */
static int
is_ready (struct GNUNET_SCHEDULER_Handle *sched,
          struct Task *task,
          struct GNUNET_TIME_Absolute now,
          const struct GNUNET_NETWORK_FDSet *rs,
	  const struct GNUNET_NETWORK_FDSet * ws)
{
  if ((GNUNET_NO == task->run_on_shutdown) && (GNUNET_YES == sched->shutdown))
    return GNUNET_NO;
  if ((GNUNET_YES == task->run_on_shutdown) &&
      (GNUNET_YES == sched->shutdown))
    task->reason |= GNUNET_SCHEDULER_REASON_SHUTDOWN;
  if (now.value >= task->timeout.value)
    task->reason |= GNUNET_SCHEDULER_REASON_TIMEOUT;
  if ((0 == (task->reason & GNUNET_SCHEDULER_REASON_READ_READY)) &&
      (rs != NULL) && (set_overlaps (rs, task->read_set)))
    task->reason |= GNUNET_SCHEDULER_REASON_READ_READY;
  if ((0 == (task->reason & GNUNET_SCHEDULER_REASON_WRITE_READY)) &&
      (ws != NULL) && (set_overlaps (ws, task->write_set)))
    task->reason |= GNUNET_SCHEDULER_REASON_WRITE_READY;
  if (task->reason == 0)
    return GNUNET_NO;           /* not ready */
  if (task->prereq_id != GNUNET_SCHEDULER_NO_TASK)
    {
      if (GNUNET_YES == is_pending (sched, task->prereq_id))
        return GNUNET_NO;       /* prereq waiting */
      task->reason |= GNUNET_SCHEDULER_REASON_PREREQ_DONE;
    }
  return GNUNET_YES;
}


/**
 * Put a task that is ready for execution into the ready queue.
 *
 * @param handle the scheduler
 * @param task task ready for execution
 */
static void
queue_ready_task (struct GNUNET_SCHEDULER_Handle *handle, 
		  struct Task *task)
{
  task->next = handle->ready[check_priority (task->priority)];
  handle->ready[check_priority (task->priority)] = task;
  handle->ready_count++;
}


/**
 * Check which tasks are ready and move them
 * to the respective ready queue.
 *
 * @param handle the scheduler
 * @param rs FDs ready for reading
 * @param ws FDs ready for writing
 */
static void
check_ready (struct GNUNET_SCHEDULER_Handle *handle,
             const struct GNUNET_NETWORK_FDSet * rs,
	     const struct GNUNET_NETWORK_FDSet * ws)
{
  struct Task *pos;
  struct Task *prev;
  struct Task *next;
  struct GNUNET_TIME_Absolute now;

  now = GNUNET_TIME_absolute_get ();
  prev = NULL;
  pos = handle->pending;
  while (pos != NULL)
    {
      next = pos->next;
      if (GNUNET_YES == is_ready (handle, pos, now, rs, ws))
        {
          if (prev == NULL)
            handle->pending = next;
          else
            prev->next = next;
          queue_ready_task (handle, pos);
          pos = next;
          continue;
        }
      prev = pos;
      pos = next;
    }
}


/**
 * Destroy a task (release associated resources)
 *
 * @param t task to destroy
 */
static void destroy_task (struct Task *t)
{
  if (t->read_set)
    GNUNET_NETWORK_fdset_destroy (t->read_set);
  if (t->write_set)
    GNUNET_NETWORK_fdset_destroy (t->write_set);
  GNUNET_free (t);
}


/**
 * Run at least one task in the highest-priority queue that is not
 * empty.  Keep running tasks until we are either no longer running
 * "URGENT" tasks or until we have at least one "pending" task (which
 * may become ready, hence we should select on it).  Naturally, if
 * there are no more ready tasks, we also return.
 *
 * @param sched the scheduler
 */
static void
run_ready (struct GNUNET_SCHEDULER_Handle *sched)
{
  enum GNUNET_SCHEDULER_Priority p;
  struct Task *pos;
  struct GNUNET_SCHEDULER_TaskContext tc;

  do
    {
      if (sched->ready_count == 0)
        return;
      GNUNET_assert (sched->ready[GNUNET_SCHEDULER_PRIORITY_KEEP] == NULL);
      /* yes, p>0 is correct, 0 is "KEEP" which should
         always be an empty queue (see assertion)! */
      for (p = GNUNET_SCHEDULER_PRIORITY_COUNT - 1; p > 0; p--)
        {
          pos = sched->ready[p];
          if (pos != NULL)
            break;
        }
      GNUNET_assert (pos != NULL);      /* ready_count wrong? */
      sched->ready[p] = pos->next;
      sched->ready_count--;
      sched->current_priority = p;
      GNUNET_assert (pos->priority == p);
      tc.sched = sched;
      tc.reason = pos->reason;
      tc.read_ready = pos->read_set;
      tc.write_ready = pos->write_set;
      pos->callback (pos->callback_cls, &tc);
      destroy_task (pos);
      sched->tasks_run++;
    }
  while ((sched->pending == NULL) || (p == GNUNET_SCHEDULER_PRIORITY_URGENT));
}


/**
 * Have we (ever) received a SIGINT/TERM/QUIT/HUP?
 */
static volatile int sig_shutdown;


/**
 * Signal handler called for signals that should cause us to shutdown.
 */
static void
sighandler_shutdown ()
{
  sig_shutdown = 1;
}


/**
 * Initialize a scheduler using this thread.  This function will
 * return when either a shutdown was initiated (via signal) and all
 * tasks marked to "run_on_shutdown" have been completed or when all
 * tasks in general have been completed.
 *
 * @param task task to run immediately
 * @param cls closure of task
 */
void
GNUNET_SCHEDULER_run (GNUNET_SCHEDULER_Task task, void *cls)
{
  struct GNUNET_SCHEDULER_Handle sched;
  struct GNUNET_NETWORK_FDSet *rs;
  struct GNUNET_NETWORK_FDSet *ws;
  struct GNUNET_TIME_Relative timeout;
  int ret;
  struct GNUNET_SIGNAL_Context *shc_int;
  struct GNUNET_SIGNAL_Context *shc_term;
  struct GNUNET_SIGNAL_Context *shc_quit;
  struct GNUNET_SIGNAL_Context *shc_hup;
  struct Task *tpos;
  unsigned long long last_tr;
  unsigned int busy_wait_warning;

  sig_shutdown = 0;
  rs = GNUNET_NETWORK_fdset_create ();
  ws = GNUNET_NETWORK_fdset_create ();
#ifndef MINGW
  shc_int = GNUNET_SIGNAL_handler_install (SIGINT, &sighandler_shutdown);
  shc_term = GNUNET_SIGNAL_handler_install (SIGTERM, &sighandler_shutdown);
  shc_quit = GNUNET_SIGNAL_handler_install (SIGQUIT, &sighandler_shutdown);
  shc_hup = GNUNET_SIGNAL_handler_install (SIGHUP, &sighandler_shutdown);
#endif
  memset (&sched, 0, sizeof (sched));
  sched.current_priority = GNUNET_SCHEDULER_PRIORITY_DEFAULT;
  GNUNET_SCHEDULER_add_continuation (&sched,
                                     GNUNET_YES,
                                     task,
                                     cls, GNUNET_SCHEDULER_REASON_STARTUP);
  last_tr = 0;
  busy_wait_warning = 0;
  while ((GNUNET_NO == sched.shutdown) &&
         (!sig_shutdown) &&
         ((sched.pending != NULL) || (sched.ready_count > 0)))
    {
      GNUNET_NETWORK_fdset_zero (rs);
      GNUNET_NETWORK_fdset_zero (ws);
      timeout = GNUNET_TIME_relative_get_forever();
      update_sets (&sched, rs, ws, &timeout);
      if (sched.ready_count > 0)
        {
          /* no blocking, more work already ready! */
          timeout = GNUNET_TIME_relative_get_zero();
        }
      ret = GNUNET_NETWORK_socket_select (rs, ws, NULL, timeout);
      if (last_tr == sched.tasks_run)
	{
	  busy_wait_warning++;
	}
      else
	{
	  last_tr = sched.tasks_run;
	  busy_wait_warning = 0;
	}
      if ( (ret == 0) &&
	   (timeout.value == 0) &&
	   (busy_wait_warning > 16) )
	{
	  GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
		      _("Looks like we're busy waiting...\n"));
	  sleep (1); /* mitigate */
	}
      if (ret == GNUNET_SYSERR)
        {
          if (errno == EINTR)
            continue;
          GNUNET_log_strerror (GNUNET_ERROR_TYPE_ERROR, "select");
          break;
        }
      check_ready (&sched, rs, ws);
      run_ready (&sched);
    }
  if (sig_shutdown)
    sched.shutdown = GNUNET_YES;
  GNUNET_SIGNAL_handler_uninstall (shc_int);
  GNUNET_SIGNAL_handler_uninstall (shc_term);
  GNUNET_SIGNAL_handler_uninstall (shc_quit);
  GNUNET_SIGNAL_handler_uninstall (shc_hup);
  do
    {
      run_ready (&sched);
      check_ready (&sched, NULL, NULL);
    }
  while (sched.ready_count > 0);
  while (NULL != (tpos = sched.pending))
    {
      sched.pending = tpos->next;
      GNUNET_free (tpos);
    }
  GNUNET_NETWORK_fdset_destroy (rs);
  GNUNET_NETWORK_fdset_destroy (ws);
}


/**
 * Request the shutdown of a scheduler.  This function can be used to
 * stop a scheduling thread when created with the
 * "GNUNET_SCHEDULER_init_thread" function or from within the signal
 * handler for signals causing shutdowns.
 *
 * @param sched the scheduler
 */
void
GNUNET_SCHEDULER_shutdown (struct GNUNET_SCHEDULER_Handle *sched)
{
  sched->shutdown = GNUNET_YES;
}


/**
 * Get information about the current load of this scheduler.  Use this
 * function to determine if an elective task should be added or simply
 * dropped (if the decision should be made based on the number of
 * tasks ready to run).
 *
 * @param sched scheduler to query
 * @param p priority level to look at
 * @return number of tasks pending right now
 */
unsigned int
GNUNET_SCHEDULER_get_load (struct GNUNET_SCHEDULER_Handle *sched,
                           enum GNUNET_SCHEDULER_Priority p)
{
  struct Task *pos;
  unsigned int ret;

  if (p == GNUNET_SCHEDULER_PRIORITY_COUNT)
    return sched->ready_count;
  if (p == GNUNET_SCHEDULER_PRIORITY_KEEP)
    p = sched->current_priority;
  ret = 0;
  pos = sched->ready[p];
  while (pos != NULL)
    {
      pos = pos->next;
      ret++;
    }
  return ret;
}


/**
 * Cancel the task with the specified identifier.
 * The task must not yet have run.
 *
 * @param sched scheduler to use
 * @param task id of the task to cancel
 * @return original closure of the task
 */
void *
GNUNET_SCHEDULER_cancel (struct GNUNET_SCHEDULER_Handle *sched,
                         GNUNET_SCHEDULER_TaskIdentifier task)
{
  struct Task *t;
  struct Task *prev;
  enum GNUNET_SCHEDULER_Priority p;
  void *ret;

  prev = NULL;
  t = sched->pending;
  while (t != NULL)
    {
      if (t->id == task)
        break;
      prev = t;
      t = t->next;
    }
  p = 0;
  while (t == NULL)
    {
      p++;
      GNUNET_assert (p < GNUNET_SCHEDULER_PRIORITY_COUNT);
      prev = NULL;
      t = sched->ready[p];
      while (t != NULL)
        {
          if (t->id == task)
            {
              sched->ready_count--;
              break;
            }
          prev = t;
          t = t->next;
        }
    }
  if (prev == NULL)
    {
      if (p == 0)
        sched->pending = t->next;
      else
        sched->ready[p] = t->next;
    }
  else
    prev->next = t->next;
  ret = t->callback_cls;
  destroy_task (t);
  return ret;
}


/**
 * Continue the current execution with the given function.  This is
 * similar to the other "add" functions except that there is no delay
 * and the reason code can be specified.
 *
 * @param sched scheduler to use
 * @param run_on_shutdown should this task be run if we are shutting down?
 * @param main main function of the task
 * @param cls closure for 'main'
 * @param reason reason for task invocation
 */
void
GNUNET_SCHEDULER_add_continuation (struct GNUNET_SCHEDULER_Handle *sched,
                                   int run_on_shutdown,
                                   GNUNET_SCHEDULER_Task main,
                                   void *cls,
                                   enum GNUNET_SCHEDULER_Reason reason)
{
  struct Task *task;

  task = GNUNET_malloc (sizeof (struct Task));
  task->callback = main;
  task->callback_cls = cls;
  task->id = ++sched->last_id;
  task->reason = reason;
  task->priority = sched->current_priority;
  task->run_on_shutdown = run_on_shutdown;
  queue_ready_task (sched, task);
}


/**
 * Schedule a new task to be run after the specified
 * prerequisite task has completed.
 *
 * @param sched scheduler to use
 * @param run_on_shutdown run on shutdown?
 * @param prio how important is this task?
 * @param prerequisite_task run this task after the task with the given
 *        task identifier completes (and any of our other
 *        conditions, such as delay, read or write-readyness
 *        are satisfied).  Use  GNUNET_SCHEDULER_NO_TASK to not have any dependency
 *        on completion of other tasks.
 * @param main main function of the task
 * @param cls closure for 'main'
 * @return unique task identifier for the job
 *         only valid until "main" is started!
 */
GNUNET_SCHEDULER_TaskIdentifier
GNUNET_SCHEDULER_add_after (struct GNUNET_SCHEDULER_Handle *sched,
                            int run_on_shutdown,
                            enum GNUNET_SCHEDULER_Priority prio,
                            GNUNET_SCHEDULER_TaskIdentifier prerequisite_task,
                            GNUNET_SCHEDULER_Task main, void *cls)
{
  return GNUNET_SCHEDULER_add_select (sched, run_on_shutdown, prio,
                                      prerequisite_task,
                                      GNUNET_TIME_UNIT_ZERO,
                                      NULL, NULL, main, cls);
}


/**
 * Schedule a new task to be run with a specified delay.  The task
 * will be scheduled for execution once the delay has expired and the
 * prerequisite task has completed.
 *
 * @param sched scheduler to use
 * @param run_on_shutdown run on shutdown? You can use this
 *        argument to run a function only during shutdown
 *        by setting delay to -1.  Set this
 *        argument to GNUNET_NO to skip this task if
 *        the user requested process termination.
 * @param prio how important is this task?
 * @param prerequisite_task run this task after the task with the given
 *        task identifier completes (and any of our other
 *        conditions, such as delay, read or write-readyness
 *        are satisfied).  Use  GNUNET_SCHEDULER_NO_TASK to not have any dependency
 *        on completion of other tasks.
 * @param delay how long should we wait? Use  GNUNET_TIME_UNIT_FOREVER_REL for "forever"
 * @param main main function of the task
 * @param cls closure of task
 * @return unique task identifier for the job
 *         only valid until "main" is started!
 */
GNUNET_SCHEDULER_TaskIdentifier
GNUNET_SCHEDULER_add_delayed (struct GNUNET_SCHEDULER_Handle * sched,
                              int run_on_shutdown,
                              enum GNUNET_SCHEDULER_Priority prio,
                              GNUNET_SCHEDULER_TaskIdentifier
                              prerequisite_task,
                              struct GNUNET_TIME_Relative delay,
                              GNUNET_SCHEDULER_Task main, void *cls)
{
  return GNUNET_SCHEDULER_add_select (sched, run_on_shutdown, prio,
                                      prerequisite_task, delay,
                                      NULL, NULL, main, cls);
}


/**
 * Schedule a new task to be run with a specified delay or when the
 * specified file descriptor is ready for reading.  The delay can be
 * used as a timeout on the socket being ready.  The task will be
 * scheduled for execution once either the delay has expired or the
 * socket operation is ready.
 *
 * @param sched scheduler to use
 * @param run_on_shutdown run on shutdown? Set this
 *        argument to GNUNET_NO to skip this task if
 *        the user requested process termination.
 * @param prio how important is this task?
 * @param prerequisite_task run this task after the task with the given
 *        task identifier completes (and any of our other
 *        conditions, such as delay, read or write-readyness
 *        are satisfied).  Use  GNUNET_SCHEDULER_NO_TASK to not have any dependency
 *        on completion of other tasks.
 * @param delay how long should we wait? Use  GNUNET_TIME_UNIT_FOREVER_REL for "forever"
 * @param rfd read file-descriptor
 * @param main main function of the task
 * @param cls closure of task
 * @return unique task identifier for the job
 *         only valid until "main" is started!
 */
GNUNET_SCHEDULER_TaskIdentifier
GNUNET_SCHEDULER_add_read_net (struct GNUNET_SCHEDULER_Handle * sched,
                           int run_on_shutdown,
                           enum GNUNET_SCHEDULER_Priority prio,
                           GNUNET_SCHEDULER_TaskIdentifier prerequisite_task,
                           struct GNUNET_TIME_Relative delay,
                           struct GNUNET_NETWORK_Handle *rfd, GNUNET_SCHEDULER_Task main, void *cls)
{
  struct GNUNET_NETWORK_FDSet *rs;
  GNUNET_SCHEDULER_TaskIdentifier ret;

  GNUNET_assert (rfd != NULL);
  rs = GNUNET_NETWORK_fdset_create ();
  GNUNET_NETWORK_fdset_set (rs, rfd);
  ret = GNUNET_SCHEDULER_add_select (sched, run_on_shutdown, prio,
                                      prerequisite_task, delay,
                                      rs, NULL, main, cls);
  GNUNET_NETWORK_fdset_destroy (rs);
  return ret;
}


/**
 * Schedule a new task to be run with a specified delay or when the
 * specified file descriptor is ready for writing.  The delay can be
 * used as a timeout on the socket being ready.  The task will be
 * scheduled for execution once either the delay has expired or the
 * socket operation is ready.
 *
 * @param sched scheduler to use
 * @param run_on_shutdown run on shutdown? Set this
 *        argument to GNUNET_NO to skip this task if
 *        the user requested process termination.
 * @param prio how important is this task?
 * @param prerequisite_task run this task after the task with the given
 *        task identifier completes (and any of our other
 *        conditions, such as delay, read or write-readyness
 *        are satisfied).  Use  GNUNET_SCHEDULER_NO_TASK to not have any dependency
 *        on completion of other tasks.
 * @param delay how long should we wait? Use  GNUNET_TIME_UNIT_FOREVER_REL for "forever"
 * @param wfd write file-descriptor
 * @param main main function of the task
 * @param cls closure of task
 * @return unique task identifier for the job
 *         only valid until "main" is started!
 */
GNUNET_SCHEDULER_TaskIdentifier
GNUNET_SCHEDULER_add_write_net (struct GNUNET_SCHEDULER_Handle * sched,
                            int run_on_shutdown,
                            enum GNUNET_SCHEDULER_Priority prio,
                            GNUNET_SCHEDULER_TaskIdentifier prerequisite_task,
                            struct GNUNET_TIME_Relative delay,
                            struct GNUNET_NETWORK_Handle *wfd, GNUNET_SCHEDULER_Task main, void *cls)
{
  struct GNUNET_NETWORK_FDSet *ws;
  GNUNET_SCHEDULER_TaskIdentifier ret;

  GNUNET_assert (wfd != NULL);
  ws = GNUNET_NETWORK_fdset_create ();
  GNUNET_NETWORK_fdset_set (ws, wfd);
  ret = GNUNET_SCHEDULER_add_select (sched, run_on_shutdown, prio,
                                      prerequisite_task, delay,
                                      NULL, ws, main, cls);
  GNUNET_NETWORK_fdset_destroy (ws);
  return ret;
}


/**
 * Schedule a new task to be run with a specified delay or when any of
 * the specified file descriptor sets is ready.  The delay can be used
 * as a timeout on the socket(s) being ready.  The task will be
 * scheduled for execution once either the delay has expired or any of
 * the socket operations is ready.  This is the most general
 * function of the "add" family.  Note that the "prerequisite_task"
 * must be satisfied in addition to any of the other conditions.  In
 * other words, the task will be started when
 * <code>
 * (prerequisite-run)
 * && (delay-ready
 *     || any-rs-ready
 *     || any-ws-ready
 *     || (shutdown-active && run-on-shutdown) )
 * </code>
 *
 * @param sched scheduler to use
 * @param run_on_shutdown run on shutdown?  Set this
 *        argument to GNUNET_NO to skip this task if
 *        the user requested process termination.
 * @param prio how important is this task?
 * @param prerequisite_task run this task after the task with the given
 *        task identifier completes (and any of our other
 *        conditions, such as delay, read or write-readyness
 *        are satisfied).  Use GNUNET_SCHEDULER_NO_TASK to not have any dependency
 *        on completion of other tasks.
 * @param delay how long should we wait? Use GNUNET_TIME_UNIT_FOREVER_REL for "forever"
 * @param rs set of file descriptors we want to read (can be NULL)
 * @param ws set of file descriptors we want to write (can be NULL)
 * @param main main function of the task
 * @param cls closure of task
 * @return unique task identifier for the job
 *         only valid until "main" is started!
 */
GNUNET_SCHEDULER_TaskIdentifier
GNUNET_SCHEDULER_add_select (struct GNUNET_SCHEDULER_Handle * sched,
                             int run_on_shutdown,
                             enum GNUNET_SCHEDULER_Priority prio,
                             GNUNET_SCHEDULER_TaskIdentifier
                             prerequisite_task,
                             struct GNUNET_TIME_Relative delay,
                             const struct GNUNET_NETWORK_FDSet * rs, const struct GNUNET_NETWORK_FDSet * ws,
                             GNUNET_SCHEDULER_Task main, void *cls)
{
  struct Task *task;

  task = GNUNET_malloc (sizeof (struct Task));
  task->callback = main;
  task->callback_cls = cls;
  task->read_set = GNUNET_NETWORK_fdset_create ();
  if (rs != NULL)
    GNUNET_NETWORK_fdset_copy (task->read_set, rs);
  task->write_set = GNUNET_NETWORK_fdset_create ();
  if (ws != NULL)
    GNUNET_NETWORK_fdset_copy (task->write_set, ws);
  task->id = ++sched->last_id;
  task->prereq_id = prerequisite_task;
  task->timeout = GNUNET_TIME_relative_to_absolute (delay);
  task->priority =
    check_priority ((prio ==
                     GNUNET_SCHEDULER_PRIORITY_KEEP) ? sched->current_priority
                    : prio);
  task->run_on_shutdown = run_on_shutdown;
  task->next = sched->pending;
  sched->pending = task;
  return task->id;
}

/**
 * Schedule a new task to be run with a specified delay or when the
 * specified file descriptor is ready for reading.  The delay can be
 * used as a timeout on the socket being ready.  The task will be
 * scheduled for execution once either the delay has expired or the
 * socket operation is ready.
 *
 * @param sched scheduler to use
 * @param run_on_shutdown run on shutdown? Set this
 *        argument to GNUNET_NO to skip this task if
 *        the user requested process termination.
 * @param prio how important is this task?
 * @param prerequisite_task run this task after the task with the given
 *        task identifier completes (and any of our other
 *        conditions, such as delay, read or write-readyness
 *        are satisfied).  Use  GNUNET_SCHEDULER_NO_TASK to not have any dependency
 *        on completion of other tasks.
 * @param delay how long should we wait? Use  GNUNET_TIME_UNIT_FOREVER_REL for "forever"
 * @param rfd read file-descriptor
 * @param main main function of the task
 * @param cls closure of task
 * @return unique task identifier for the job
 *         only valid until "main" is started!
 */
GNUNET_SCHEDULER_TaskIdentifier
GNUNET_SCHEDULER_add_read_file (struct GNUNET_SCHEDULER_Handle * sched,
				int run_on_shutdown,
				enum GNUNET_SCHEDULER_Priority prio,
				GNUNET_SCHEDULER_TaskIdentifier prerequisite_task,
				struct GNUNET_TIME_Relative delay,
				const struct GNUNET_DISK_FileHandle *rfd,
				GNUNET_SCHEDULER_Task main, void *cls)
{
  struct GNUNET_NETWORK_FDSet *rs;
  GNUNET_SCHEDULER_TaskIdentifier ret;

  GNUNET_assert (rfd != NULL);
  rs = GNUNET_NETWORK_fdset_create ();
  GNUNET_NETWORK_fdset_handle_set (rs, rfd);
  ret = GNUNET_SCHEDULER_add_select (sched, run_on_shutdown, prio,
                                      prerequisite_task, delay,
                                      rs, NULL, main, cls);
  GNUNET_NETWORK_fdset_destroy (rs);
  return ret;
}


/**
 * Schedule a new task to be run with a specified delay or when the
 * specified file descriptor is ready for writing.  The delay can be
 * used as a timeout on the socket being ready.  The task will be
 * scheduled for execution once either the delay has expired or the
 * socket operation is ready.
 *
 * @param sched scheduler to use
 * @param run_on_shutdown run on shutdown? Set this
 *        argument to GNUNET_NO to skip this task if
 *        the user requested process termination.
 * @param prio how important is this task?
 * @param prerequisite_task run this task after the task with the given
 *        task identifier completes (and any of our other
 *        conditions, such as delay, read or write-readyness
 *        are satisfied).  Use  GNUNET_SCHEDULER_NO_TASK to not have any dependency
 *        on completion of other tasks.
 * @param delay how long should we wait? Use  GNUNET_TIME_UNIT_FOREVER_REL for "forever"
 * @param wfd write file-descriptor
 * @param main main function of the task
 * @param cls closure of task
 * @return unique task identifier for the job
 *         only valid until "main" is started!
 */
GNUNET_SCHEDULER_TaskIdentifier
GNUNET_SCHEDULER_add_write_file (struct GNUNET_SCHEDULER_Handle * sched,
				 int run_on_shutdown,
				 enum GNUNET_SCHEDULER_Priority prio,
				 GNUNET_SCHEDULER_TaskIdentifier prerequisite_task,
				 struct GNUNET_TIME_Relative delay,
				 const struct GNUNET_DISK_FileHandle *wfd,
				 GNUNET_SCHEDULER_Task main, void *cls)
{
  struct GNUNET_NETWORK_FDSet *ws;
  GNUNET_SCHEDULER_TaskIdentifier ret;

  GNUNET_assert (wfd != NULL);
  ws = GNUNET_NETWORK_fdset_create ();
  GNUNET_NETWORK_fdset_handle_set (ws, wfd);
  ret = GNUNET_SCHEDULER_add_select (sched, run_on_shutdown, prio,
                                      prerequisite_task, delay,
                                      NULL, ws, main, cls);
  GNUNET_NETWORK_fdset_destroy (ws);
  return ret;
}


/* end of scheduler.c */
