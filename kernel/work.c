/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * Second generation work queue implementation
 */

#include <kernel.h>
#include <kernel_structs.h>
#include <wait_q.h>
#include <spinlock.h>
#include <errno.h>
#include <ksched.h>
#include <sys/printk.h>

static inline void flag_clear(uint32_t *flagp,
			      uint32_t bit)
{
	*flagp &= ~BIT(bit);
}

static inline void flag_set(uint32_t *flagp,
			    uint32_t bit)
{
	*flagp |= BIT(bit);
}

static inline bool flag_test(const uint32_t *flagp,
			     uint32_t bit)
{
	return (*flagp & BIT(bit)) != 0U;
}

static inline bool flag_test_and_clear(uint32_t *flagp,
				       int bit)
{
	bool ret = flag_test(flagp, bit);

	flag_clear(flagp, bit);

	return ret;
}

static inline void flags_set(uint32_t *flagp,
			     uint32_t flags)
{
	*flagp = flags;
}

static inline uint32_t flags_get(const uint32_t *flagp)
{
	return *flagp;
}

/* Lock to protect the internal state of all work items, work queues,
 * and pending_cancels.
 */
static struct k_spinlock lock;

/* Invoked by work thread */
static void handle_flush(struct k_work *work)
{
	struct z_work_flusher *flusher
		= CONTAINER_OF(work, struct z_work_flusher, work);

	k_sem_give(&flusher->sem);
}

static inline void init_flusher(struct z_work_flusher *flusher)
{
	k_sem_init(&flusher->sem, 0, 1);
	k_work_init(&flusher->work, handle_flush);
}

/* List of pending cancellations. */
static sys_slist_t pending_cancels;

/* Initialize a canceler record and add it to the list of pending
 * cancels.
 *
 * Invoked with work lock held.
 *
 * @param canceler the structure used to notify a waiting process.
 * @param work the work structure that is to be canceled
 */
static inline void init_work_cancel(struct z_work_canceller *canceler,
				    struct k_work *work)
{
	k_sem_init(&canceler->sem, 0, 1);
	canceler->work = work;
	sys_slist_append(&pending_cancels, &canceler->node);
}

/* Complete cancellation of a work item and unlock held lock.
 *
 * Invoked with work lock held.
 *
 * Invoked from a work queue thread.
 *
 * Reschedules.
 *
 * @param work the work structre that has completed cancellation
 */
static void finalize_cancel_locked(struct k_work *work)
{
	struct z_work_canceller *wc, *tmp;
	sys_snode_t *prev = NULL;

	/* Clear this first, so released high-priority threads don't
	 * see it when doing things.
	 */
	flag_clear(&work->flags, K_WORK_CANCELING_BIT);

	/* Search for and remove the matching container, and release
	 * what's waiting for the completion.  The same work item can
	 * appear multiple times in the list if multiple threads
	 * attempt to cancel it.
	 */
	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&pending_cancels, wc, tmp, node) {
		if (wc->work == work) {
			sys_slist_remove(&pending_cancels, prev, &wc->node);
			k_sem_give(&wc->sem);
		} else {
			prev = &wc->node;
		}
	}
}

void k_work_init(struct k_work *work,
		  k_work_handler_t handler)
{
	__ASSERT_NO_MSG(work != NULL);
	__ASSERT_NO_MSG(handler != NULL);

	*work = (struct k_work)Z_WORK_INITIALIZER(handler);
}

static inline int work_busy_get_locked(const struct k_work *work)
{
	return flags_get(&work->flags) & K_WORK_MASK;
}

int k_work_busy_get(const struct k_work *work)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	int ret = work_busy_get_locked(work);

	k_spin_unlock(&lock, key);

	return ret;
}

/* Add a flusher work item to the queue.
 *
 * Invoked with work lock held.
 *
 * Caller must notify queue of pending work.
 *
 * @param queue queue on which a work item may appear.
 * @param work the work item that is either queued or running on @p
 * queue
 * @param flusher an uninitialized/unused flusher object
 */
static void queue_flusher_locked(struct k_work_q *queue,
				 struct k_work *work,
				 struct z_work_flusher *flusher)
{
	bool in_list = false;
	struct k_work *wn;

	/* Determine whether the work item is still queued. */
	SYS_SLIST_FOR_EACH_CONTAINER(&queue->pending, wn, node) {
		if (wn == work) {
			in_list = true;
			break;
		}
	}

	init_flusher(flusher);
	if (in_list) {
		sys_slist_insert(&queue->pending, &work->node,
				 &flusher->work.node);
	} else {
		sys_slist_prepend(&queue->pending, &flusher->work.node);
	}
}

/* Try to remove a work item from the given queue.
 *
 * Invoked with work lock held.
 *
 * @param queue the queue from which the work should be removed
 * @param work work that may be on the queue
 */
static inline void queue_remove_locked(struct k_work_q *queue,
				       struct k_work *work)
{
	if (flag_test_and_clear(&work->flags, K_WORK_QUEUED_BIT)) {
		(void)sys_slist_find_and_remove(&queue->pending, &work->node);
	}
}

/* Potentially notify a queue that it needs to look for pending work.
 *
 * This may make the work queue thread ready, but as the lock is held it
 * will not be a reschedule point.  Callers should yield after the lock is
 * released where appropriate (generally if this returns true).
 *
 * @param queue to be notified.  If this is null no notification is required.
 *
 * @return true if and only if the queue was notified and woken, i.e. a
 * reschedule is pending.
 */
static inline bool notify_queue_locked(struct k_work_q *queue)
{
	bool rv = false;

	if (queue != NULL) {
		rv = z_sched_wake(&queue->notifyq, 0, NULL);
	}

	return rv;
}

/* Submit an work item to a queue if queue state allows new work.
 *
 * Submission is rejected if no queue is provided, or if the queue is
 * draining and the work isn't being submitted from the queue's
 * thread (chained submission).
 *
 * Invoked with work lock held.
 * Conditionally notifies queue.
 *
 * @param queue the queue to which work should be submitted.  This may
 * be null, in which case the submission will fail.
 *
 * @param work to be submitted
 *
 * @retval 1 if successfully queued
 * @retval -EINVAL if no queue is provided
 * @retval -ENODEV if the queue is not started
 * @retval -EBUSY if the submission was rejected (draining, plugged)
 */
static inline int queue_submit_locked(struct k_work_q *queue,
				      struct k_work *work)
{
	if (queue == NULL) {
		return -EINVAL;
	}

	int ret = -EBUSY;
	bool chained = (_current == &queue->thread) && !k_is_in_isr();
	bool draining = flag_test(&queue->flags, K_WORK_QUEUE_DRAIN_BIT);
	bool plugged = flag_test(&queue->flags, K_WORK_QUEUE_PLUGGED_BIT);

	/* Test for acceptability, in priority order:
	 *
	 * * -ENODEV if the queue isn't running.
	 * * -EBUSY if draining and not chained
	 * * -EBUSY if plugged and not draining
	 * * otherwise OK
	 */
	if (!flag_test(&queue->flags, K_WORK_QUEUE_STARTED_BIT)) {
		ret = -ENODEV;
	} else if (draining && !chained) {
		ret = -EBUSY;
	} else if (plugged && !draining) {
		ret = -EBUSY;
	} else {
		sys_slist_append(&queue->pending, &work->node);
		ret = 1;
		(void)notify_queue_locked(queue);
	}

	return ret;
}

/* Attempt to submit work to a queue.
 *
 * The submission can fail if:
 * * the work is cancelling,
 * * no candidate queue can be identified;
 * * the candidate queue rejects the submission.
 *
 * Invoked with work lock held.
 * Conditionally notifies queue.
 *
 * @param work the work structure to be submitted

 * @param queuep pointer to a queue reference.  On input this should
 * dereference to the proposed queue (which may be null); after completion it
 * will be null if the work was not submitted or if submitted will reference
 * the queue it was submitted to.  That may or may not be the queue provided
 * on input.
 *
 * @retval 0 if work was already submitted to a queue
 * @retval 1 if work was not submitted and has been queued to @p queue
 * @retval 2 if work was running and has been queued to the queue that was
 * running it
 * @retval -EBUSY if canceling or submission was rejected by queue
 * @retval -EINVAL if no queue is provided
 * @retval -ENODEV if the queue is not started
 */
static int submit_to_queue_locked(struct k_work *work,
				  struct k_work_q **queuep)
{
	int ret = 0;

	if (flag_test(&work->flags, K_WORK_CANCELING_BIT)) {
		/* Disallowed */
		ret = -EBUSY;
	} else if (!flag_test(&work->flags, K_WORK_QUEUED_BIT)) {
		/* Not currently queued */
		ret = 1;

		/* If no queue specified resubmit to last queue.
		 */
		if (*queuep == NULL) {
			*queuep = work->queue;
		}

		/* If the work is currently running we have to use the
		 * queue it's running on to prevent handler
		 * re-entrancy.
		 */
		if (flag_test(&work->flags, K_WORK_RUNNING_BIT)) {
			__ASSERT_NO_MSG(work->queue != NULL);
			*queuep = work->queue;
			ret = 2;
		}

		int rc = queue_submit_locked(*queuep, work);

		if (rc < 0) {
			ret = rc;
		} else {
			flag_set(&work->flags, K_WORK_QUEUED_BIT);
			work->queue = *queuep;
		}
	} else {
		/* Already queued, do nothing. */
	}

	if (ret <= 0) {
		*queuep = NULL;
	}

	return ret;
}

int k_work_submit_to_queue(struct k_work_q *queue,
			    struct k_work *work)
{
	__ASSERT_NO_MSG(work != NULL);

	k_spinlock_key_t key = k_spin_lock(&lock);
	int ret = submit_to_queue_locked(work, &queue);

	k_spin_unlock(&lock, key);

	/* If we changed the queue contents (as indicated by a positive ret)
	 * the queue thread may now be ready, but we missed the reschedule
	 * point because the lock was held.  If this is being invoked by a
	 * preemptible thread then yield.
	 */
	if ((ret > 0) && (k_is_preempt_thread() != 0)) {
		k_yield();
	}


	return ret;
}

/* Flush the work item if necessary.
 *
 * Flushing is necessary only if the work is either queued or running.
 *
 * Invoked with work lock held by key.
 * Sleeps.
 *
 * @param work the work item that is to be flushed
 * @param flusher state used to synchronize the flush
 *
 * @retval true if work is queued or running.  If this happens the
 * caller must take the flusher semaphore after releasing the lock.
 *
 * @retval false otherwise.  No wait required.
 */
static bool work_flush_locked(struct k_work *work,
			      struct z_work_flusher *flusher)
{
	bool need_flush = (flags_get(&work->flags)
			   & (K_WORK_QUEUED | K_WORK_RUNNING)) != 0U;

	if (need_flush) {
		struct k_work_q *queue = work->queue;

		__ASSERT_NO_MSG(queue != NULL);

		queue_flusher_locked(queue, work, flusher);
		notify_queue_locked(queue);
	}

	return need_flush;
}

bool k_work_flush(struct k_work *work,
		  struct k_work_sync *sync)
{
	__ASSERT_NO_MSG(work != NULL);
	__ASSERT_NO_MSG(!flag_test(&work->flags, K_WORK_DELAYABLE_BIT));
	__ASSERT_NO_MSG(!k_is_in_isr());
	__ASSERT_NO_MSG(sync != NULL);
#ifdef CONFIG_KERNEL_COHERENCE
	__ASSERT_NO_MSG(arch_mem_coherent(sync));
#endif

	struct z_work_flusher *flusher = &sync->flusher;
	k_spinlock_key_t key = k_spin_lock(&lock);

	bool need_flush = work_flush_locked(work, flusher);

	k_spin_unlock(&lock, key);

	/* If necessary wait until the flusher item completes */
	if (need_flush) {
		k_sem_take(&flusher->sem, K_FOREVER);
	}

	return need_flush;
}

/* Execute the non-waiting steps necessary to cancel a work item.
 *
 * Invoked with work lock held.
 *
 * @param work the work item to be canceled.
 *
 * @retval true if we need to wait for the work item to finish canceling
 * @retval false if the work item is idle
 *
 * @return k_busy_wait() captured under lock
 */
static int cancel_async_locked(struct k_work *work)
{
	/* If we haven't already started canceling, do it now. */
	if (!flag_test(&work->flags, K_WORK_CANCELING_BIT)) {
		/* Remove it from the queue, if it's queued. */
		queue_remove_locked(work->queue, work);
	}

	/* If it's still busy after it's been dequeued, then flag it
	 * as canceling.
	 */
	int ret = work_busy_get_locked(work);

	if (ret != 0) {
		flag_set(&work->flags, K_WORK_CANCELING_BIT);
		ret = work_busy_get_locked(work);
	}

	return ret;
}

/* Complete cancellation necessary, release work lock, and wait if
 * necessary.
 *
 * Invoked with work lock held by key.
 * Sleeps.
 *
 * @param work work that is being canceled
 * @param canceller state used to synchronize the cancellation
 * @param key used by work lock
 *
 * @retval true if and only if the work was still active on entry.  The caller
 * must wait on the canceller semaphore after releasing the lock.
 *
 * @retval false if work was idle on entry.  The caller need not wait.
 */
static bool cancel_sync_locked(struct k_work *work,
			       struct z_work_canceller *canceller)
{
	bool ret = flag_test(&work->flags, K_WORK_CANCELING_BIT);

	/* If something's still running then we have to wait for
	 * completion, which is indicated when finish_cancel() gets
	 * invoked.
	 */
	if (ret) {
		init_work_cancel(canceller, work);
	}

	return ret;
}

int k_work_cancel(struct k_work *work)
{
	__ASSERT_NO_MSG(work != NULL);
	__ASSERT_NO_MSG(!flag_test(&work->flags, K_WORK_DELAYABLE_BIT));

	k_spinlock_key_t key = k_spin_lock(&lock);
	int ret = cancel_async_locked(work);

	k_spin_unlock(&lock, key);

	return ret;
}

bool k_work_cancel_sync(struct k_work *work,
			struct k_work_sync *sync)
{
	__ASSERT_NO_MSG(work != NULL);
	__ASSERT_NO_MSG(sync != NULL);
	__ASSERT_NO_MSG(!flag_test(&work->flags, K_WORK_DELAYABLE_BIT));
	__ASSERT_NO_MSG(!k_is_in_isr());
#ifdef CONFIG_KERNEL_COHERENCE
	__ASSERT_NO_MSG(arch_mem_coherent(sync));
#endif

	struct z_work_canceller *canceller = &sync->canceller;
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool pending = (work_busy_get_locked(work) != 0U);
	bool need_wait = false;

	if (pending) {
		(void)cancel_async_locked(work);
		need_wait = cancel_sync_locked(work, canceller);
	}

	k_spin_unlock(&lock, key);

	if (need_wait) {
		k_sem_take(&canceller->sem, K_FOREVER);
	}

	return pending;
}

/* Work has been dequeued and is about to be invoked by the work
 * thread.
 *
 * If the work is being canceled the cancellation will be completed
 * here, and the caller told not to use the work item.
 *
 * Invoked by work queue thread.
 * Takes and releases lock.
 * Reschedules via finalize_cancel_locked
 *
 * @param work work that is changing state
 * @param queue queue that is running work
 *
 * @retval true if work is to be run by the work thread
 * @retval false if it has been canceled and should not be run
 */
static inline bool work_set_running(struct k_work *work,
				    struct k_work_q *queue)
{
	bool ret = false;
	k_spinlock_key_t key = k_spin_lock(&lock);

	/* Allow the work to be queued again. */
	flag_clear(&work->flags, K_WORK_QUEUED_BIT);

	/* Normally we indicate that the work is being processed by
	 * setting RUNNING.  However, something may have initiated
	 * cancellation between when the work thread pulled this off
	 * its queue and this claimed the work lock.  If that happened
	 * we complete the cancellation now and tell the work thread
	 * not to do anything.
	 */
	ret = !flag_test(&work->flags, K_WORK_CANCELING_BIT);
	if (ret) {
		/* Not cancelling: mark running and go */
		flag_set(&work->flags, K_WORK_RUNNING_BIT);
	} else {
		/* Caught the item before being invoked; complete the
		 * cancellation now.
		 */
		finalize_cancel_locked(work);
	}

	k_spin_unlock(&lock, key);

	return ret;
}

/* Work handler has been called and is about to go idle.
 *
 * If the work is being canceled this will notify anything waiting
 * for the cancellation.
 *
 * Invoked by work queue thread.
 * Takes and releases lock.
 * Reschedules via finalize_cancel_locked
 *
 * @param work work that is in running state
 */
static inline void work_clear_running(struct k_work *work)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	/* Clear running */
	flag_clear(&work->flags, K_WORK_RUNNING_BIT);

	if (flag_test(&work->flags, K_WORK_CANCELING_BIT)) {
		finalize_cancel_locked(work);
	}

	k_spin_unlock(&lock, key);
}

/* Loop executed by a work queue thread.
 *
 * @param workq_ptr pointer to the work queue structure
 */
static void work_queue_main(void *workq_ptr, void *p2, void *p3)
{
	struct k_work_q *queue = (struct k_work_q *)workq_ptr;

	while (true) {
		sys_snode_t *node;
		struct k_work *work = NULL;
		k_spinlock_key_t key = k_spin_lock(&lock);

		/* Clear the record of processing any previous work, and check
		 * for new work.
		 */
		node = sys_slist_get(&queue->pending);
		if (node != NULL) {
			/* Mark that there's some work active that's
			 * not on the pending list.
			 */
			flag_set(&queue->flags, K_WORK_QUEUE_BUSY_BIT);
			work = CONTAINER_OF(node, struct k_work, node);
		} else if (flag_test_and_clear(&queue->flags,
					       K_WORK_QUEUE_DRAIN_BIT)) {
			/* Not busy and draining: move threads waiting for
			 * drain to ready state.  The held spinlock inhibits
			 * immediate reschedule; released threads get their
			 * chance when this invokes z_sched_wait() below.
			 *
			 * We don't touch K_WORK_QUEUE_PLUGGABLE, so getting
			 * here doesn't mean that the queue will allow new
			 * submissions.
			 */
			(void)z_sched_wake_all(&queue->drainq, 1, NULL);
		} else {
			/* No work is available and no queue state requires
			 * special handling.
			 */
			;
		}

		if (work == NULL) {
			/* Nothing's had a chance to add work since we took
			 * the lock, and we didn't find work nor got asked to
			 * stop.  Just go to sleep: when something happens the
			 * work thread will be woken and we can check again.
			 */

			(void)z_sched_wait(&lock, key, &queue->notifyq,
					   K_FOREVER, NULL);
			continue;
		}

		k_spin_unlock(&lock, key);

		if (work != NULL) {
			bool yield;
			k_work_handler_t handler = work->handler;

			__ASSERT_NO_MSG(handler != NULL);

			if (work_set_running(work, queue)) {
				handler(work);
				work_clear_running(work);
			}

			/* No longer referencing the work, so we can clear the
			 * BUSY flag while we yield to prevent starving other
			 * threads.
			 */
			key = k_spin_lock(&lock);
			flag_clear(&queue->flags, K_WORK_QUEUE_BUSY_BIT);
			yield = !flag_test(&queue->flags, K_WORK_QUEUE_NO_YIELD_BIT);
			k_spin_unlock(&lock, key);

			/* Optionally yield to prevent the work queue from
			 * starving other threads.
			 */
			if (yield) {
				k_yield();
			}
		}
	}
}

void k_work_queue_start(struct k_work_q *queue,
			k_thread_stack_t *stack,
			size_t stack_size,
			int prio,
			const struct k_work_queue_config *cfg)
{
	__ASSERT_NO_MSG(queue);
	__ASSERT_NO_MSG(stack);
	__ASSERT_NO_MSG(!flag_test(&queue->flags, K_WORK_QUEUE_STARTED_BIT));
	uint32_t flags = K_WORK_QUEUE_STARTED;

	sys_slist_init(&queue->pending);
	z_waitq_init(&queue->notifyq);
	z_waitq_init(&queue->drainq);

	if ((cfg != NULL) && cfg->no_yield) {
		flags |= K_WORK_QUEUE_NO_YIELD;
	}

	/* It hasn't actually been started yet, but all the state is in place
	 * so we can submit things and once the thread gets control it's ready
	 * to roll.
	 */
	flags_set(&queue->flags, flags);

	(void)k_thread_create(&queue->thread, stack, stack_size,
			      work_queue_main, queue, NULL, NULL,
			      prio, 0, K_FOREVER);

	if ((cfg != NULL) && (cfg->name != NULL)) {
		k_thread_name_set(&queue->thread, cfg->name);
	}

	k_thread_start(&queue->thread);
}

int k_work_queue_drain(struct k_work_q *queue,
		       bool plug)
{
	__ASSERT_NO_MSG(queue);
	__ASSERT_NO_MSG(!k_is_in_isr());

	int ret = 0;
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (((flags_get(&queue->flags)
	      & (K_WORK_QUEUE_BUSY | K_WORK_QUEUE_DRAIN)) != 0U)
	    || plug
	    || !sys_slist_is_empty(&queue->pending)) {
		flag_set(&queue->flags, K_WORK_QUEUE_DRAIN_BIT);
		if (plug) {
			flag_set(&queue->flags, K_WORK_QUEUE_PLUGGED_BIT);
		}

		notify_queue_locked(queue);
		ret = z_sched_wait(&lock, key, &queue->drainq,
				   K_FOREVER, NULL);
	} else {
		k_spin_unlock(&lock, key);
	}

	return ret;
}

int k_work_queue_unplug(struct k_work_q *queue)
{
	__ASSERT_NO_MSG(queue);

	int ret = -EALREADY;
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (flag_test_and_clear(&queue->flags, K_WORK_QUEUE_PLUGGED_BIT)) {
		ret = 0;
	}

	k_spin_unlock(&lock, key);

	return ret;
}

#ifdef CONFIG_SYS_CLOCK_EXISTS

/* Timeout handler for delayable work.
 *
 * Invoked by timeout infrastructure.
 * Takes and releases work lock.
 * Conditionally reschedules.
 */
static void work_timeout(struct _timeout *to)
{
	struct k_work_delayable *dw
		= CONTAINER_OF(to, struct k_work_delayable, timeout);
	struct k_work *wp = &dw->work;
	k_spinlock_key_t key = k_spin_lock(&lock);
	struct k_work_q *queue = NULL;

	/* If the work is still marked delayed (should be) then clear that
	 * state and submit it to the queue.  If successful the queue will be
	 * notified of new work at the next reschedule point.
	 *
	 * If not successful there is no notification that the work has been
	 * abandoned.  Sorry.
	 */
	if (flag_test_and_clear(&wp->flags, K_WORK_DELAYED_BIT)) {
		queue = dw->queue;
		(void)submit_to_queue_locked(wp, &queue);
	}

	k_spin_unlock(&lock, key);
}

void k_work_init_delayable(struct k_work_delayable *dwork,
			    k_work_handler_t handler)
{
	__ASSERT_NO_MSG(dwork != NULL);
	__ASSERT_NO_MSG(handler != NULL);

	*dwork = (struct k_work_delayable){
		.work = {
			.handler = handler,
			.flags = K_WORK_DELAYABLE,
		},
	};
	z_init_timeout(&dwork->timeout);
	(void)work_timeout;
}

static inline int work_delayable_busy_get_locked(const struct k_work_delayable *dwork)
{
	return atomic_get(&dwork->work.flags) & K_WORK_MASK;
}

int k_work_delayable_busy_get(const struct k_work_delayable *dwork)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	int ret = work_delayable_busy_get_locked(dwork);

	k_spin_unlock(&lock, key);
	return ret;
}

/* Attempt to schedule a work item for future (maybe immediate)
 * submission.
 *
 * Invoked with work lock held.
 *
 * See also submit_to_queue_locked(), which implements this for a no-wait
 * delay.
 *
 * Invoked with work lock held.
 *
 * @param queuep pointer to a pointer to a queue.  On input this
 * should dereference to the proposed queue (which may be null); after
 * completion it will be null if the work was not submitted or if
 * submitted will reference the queue it was submitted to.  That may
 * or may not be the queue provided on input.
 *
 * @param dwork the delayed work structure
 *
 * @param delay the delay to use before scheduling.
 *
 * @retval from submit_to_queue_locked() if delay is K_NO_WAIT; otherwise
 * @retval 1 to indicate successfully scheduled.
 */
static int schedule_for_queue_locked(struct k_work_q **queuep,
				     struct k_work_delayable *dwork,
				     k_timeout_t delay)
{
	int ret = 1;
	struct k_work *work = &dwork->work;

	if (K_TIMEOUT_EQ(delay, K_NO_WAIT)) {
		return submit_to_queue_locked(work, queuep);
	}

	flag_set(&work->flags, K_WORK_DELAYED_BIT);
	dwork->queue = *queuep;

	/* Add timeout */
	z_add_timeout(&dwork->timeout, work_timeout, delay);

	return ret;
}

/* Unschedule delayable work.
 *
 * If the work is delayed, cancel the timeout and clear the delayed
 * flag.
 *
 * Invoked with work lock held.
 *
 * @param dwork pointer to delayable work structure.
 *
 * @return true if and only if work had been delayed so the timeout
 * was cancelled.
 */
static inline bool unschedule_locked(struct k_work_delayable *dwork)
{
	bool ret = false;
	struct k_work *work = &dwork->work;

	/* If scheduled, try to cancel. */
	if (flag_test_and_clear(&work->flags, K_WORK_DELAYED_BIT)) {
		z_abort_timeout(&dwork->timeout);
		ret = true;
	}

	return ret;
}

/* Full cancellation of a delayable work item.
 *
 * Unschedules the delayed part then delegates to standard work
 * cancellation.
 *
 * Invoked with work lock held.
 *
 * @param dwork delayable work item
 *
 * @return k_work_busy_get() flags
 */
static int cancel_delayable_async_locked(struct k_work_delayable *dwork)
{
	(void)unschedule_locked(dwork);

	return cancel_async_locked(&dwork->work);
}

int k_work_schedule_for_queue(struct k_work_q *queue,
			       struct k_work_delayable *dwork,
			       k_timeout_t delay)
{
	__ASSERT_NO_MSG(dwork != NULL);

	struct k_work *work = &dwork->work;
	int ret = 0;
	k_spinlock_key_t key = k_spin_lock(&lock);

	/* Schedule the work item if it's idle or running. */
	if ((work_busy_get_locked(work) & ~K_WORK_RUNNING) == 0U) {
		ret = schedule_for_queue_locked(&queue, dwork, delay);
	}

	k_spin_unlock(&lock, key);

	return ret;
}

int k_work_reschedule_for_queue(struct k_work_q *queue,
				 struct k_work_delayable *dwork,
				 k_timeout_t delay)
{
	__ASSERT_NO_MSG(dwork != NULL);

	int ret = 0;
	k_spinlock_key_t key = k_spin_lock(&lock);

	/* Remove any active scheduling. */
	(void)unschedule_locked(dwork);

	/* Schedule the work item with the new parameters. */
	ret = schedule_for_queue_locked(&queue, dwork, delay);

	k_spin_unlock(&lock, key);

	return ret;
}

int k_work_cancel_delayable(struct k_work_delayable *dwork)
{
	__ASSERT_NO_MSG(dwork != NULL);

	k_spinlock_key_t key = k_spin_lock(&lock);
	int ret = cancel_delayable_async_locked(dwork);

	k_spin_unlock(&lock, key);
	return ret;
}

bool k_work_cancel_delayable_sync(struct k_work_delayable *dwork,
				  struct k_work_sync *sync)
{
	__ASSERT_NO_MSG(dwork != NULL);
	__ASSERT_NO_MSG(sync != NULL);
	__ASSERT_NO_MSG(!k_is_in_isr());
#ifdef CONFIG_KERNEL_COHERENCE
	__ASSERT_NO_MSG(arch_mem_coherent(sync));
#endif

	struct z_work_canceller *canceller = &sync->canceller;
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool pending = (work_delayable_busy_get_locked(dwork) != 0U);
	bool need_wait = false;

	if (pending) {
		(void)cancel_delayable_async_locked(dwork);
		need_wait = cancel_sync_locked(&dwork->work, canceller);
	}

	k_spin_unlock(&lock, key);

	if (need_wait) {
		k_sem_take(&canceller->sem, K_FOREVER);
	}

	return pending;
}

bool k_work_flush_delayable(struct k_work_delayable *dwork,
			    struct k_work_sync *sync)
{
	__ASSERT_NO_MSG(dwork != NULL);
	__ASSERT_NO_MSG(sync != NULL);
	__ASSERT_NO_MSG(!k_is_in_isr());
#ifdef CONFIG_KERNEL_COHERENCE
	__ASSERT_NO_MSG(arch_mem_coherent(sync));
#endif

	struct k_work *work = &dwork->work;
	struct z_work_flusher *flusher = &sync->flusher;
	k_spinlock_key_t key = k_spin_lock(&lock);

	/* If it's idle release the lock and return immediately. */
	if (work_busy_get_locked(work) == 0U) {
		k_spin_unlock(&lock, key);
		return false;
	}

	/* If unscheduling did something then submit it.  Ignore a
	 * failed submission (e.g. when cancelling).
	 */
	if (unschedule_locked(dwork)) {
		struct k_work_q *queue = dwork->queue;

		(void)submit_to_queue_locked(work, &queue);
	}

	/* Wait for it to finish */
	bool need_flush = work_flush_locked(work, flusher);

	k_spin_unlock(&lock, key);

	/* If necessary wait until the flusher item completes */
	if (need_flush) {
		k_sem_take(&flusher->sem, K_FOREVER);
	}

	return need_flush;
}

#endif /* CONFIG_SYS_CLOCK_EXISTS */
