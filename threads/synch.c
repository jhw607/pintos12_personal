/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void sema_init(struct semaphore *sema, unsigned value)
{
	ASSERT(sema != NULL);

	sema->value = value;
	list_init(&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void sema_down(struct semaphore *sema)
{

	/* Project2 - Priority Scheduling
	 * semaphore를 얻고 waiters 리스트 삽입 시, 우선순위대로 삽입되도록 수정
	 */
	enum intr_level old_level;

	ASSERT(sema != NULL);
	ASSERT(!intr_context());

	old_level = intr_disable();
	while (sema->value == 0)
	{
		list_insert_ordered(&sema->waiters, &thread_current()->elem, cmp_priority, NULL);
		thread_block();
	}
	sema->value--;
	intr_set_level(old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema)
{
	enum intr_level old_level;
	bool success;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level(old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void sema_up(struct semaphore *sema)
{

	/* Project2 - Priority Scheduling
	 * waiter list에 있는 쓰레드의 우선순위가 변경 되었을 경우를 고려하여 waiter list를 정렬 (list_sort)
	 * 세마포어 해제 후 priority preemption 기능 추가
	 */
	enum intr_level old_level;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (!list_empty(&sema->waiters))
	{
		list_sort(&sema->waiters, cmp_priority, NULL);
		thread_unblock(list_entry(list_pop_front(&sema->waiters), struct thread, elem));
	}
	sema->value++;
	test_max_priority();
	intr_set_level(old_level);
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void sema_self_test(void)
{
	struct semaphore sema[2];
	int i;

	printf("Testing semaphores...");
	sema_init(&sema[0], 0);
	sema_init(&sema[1], 0);
	thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up(&sema[0]);
		sema_down(&sema[1]);
	}
	printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void *sema_)
{
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down(&sema[0]);
		sema_up(&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void lock_init(struct lock *lock)
{
	ASSERT(lock != NULL);

	lock->holder = NULL;
	sema_init(&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void lock_acquire(struct lock *lock)
{
	/*
	 * project 1 - Priority Donation
	 * lock을 점유하고 있는 스레드와 요청 하는 스레드의 우선순위를 비교하여
	 * priority donation을 수행하도록 수정
	 */
	if (!thread_mlfqs)
	{
		struct thread *t = thread_current();

		if (lock->holder != NULL)
		{
			t->wait_on_lock = lock;

			if (lock->holder->priority < t->priority)
			{
				list_push_back(&(lock->holder->donations), &t->d_elem);
				donate_priority();
			}
		}

		ASSERT(lock != NULL);
		ASSERT(!intr_context());
		ASSERT(!lock_held_by_current_thread(lock));

		sema_down(&lock->semaphore);
		lock->holder = t;
		t->wait_on_lock = NULL;

		return;
	}
	enum intr_level old_level = intr_disable();
	sema_down(&lock->semaphore);
	lock->holder = thread_current();
	intr_set_level(old_level);
	return;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire(struct lock *lock)
{
	bool success;

	ASSERT(lock != NULL);
	ASSERT(!lock_held_by_current_thread(lock));

	success = sema_try_down(&lock->semaphore);
	if (success)
		lock->holder = thread_current();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */

void lock_release(struct lock *lock)
{
	/* donation list 에서 스레드를 제거하고 우선순위를 다시 계산하도록
	 * remove_with_lock(), refresh_prioriy() 함수를 호출
	 */
	if (!thread_mlfqs)
	{
		ASSERT(lock != NULL);
		ASSERT(lock_held_by_current_thread(lock));

		remove_with_lock(lock);
		refresh_priority();

		lock->holder = NULL;
		sema_up(&lock->semaphore);

		return;
	}
	enum intr_level old_level = intr_disable();
	sema_up(&lock->semaphore);
	lock->holder = NULL;
	intr_set_level(old_level);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock *lock)
{
	ASSERT(lock != NULL);

	return lock->holder == thread_current();
}

/* One semaphore in a list. */
struct semaphore_elem
{
	struct list_elem elem;		/* List element. */
	struct semaphore semaphore; /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
/* cond_init() : conditional varaible 자료구조를 초기화 */
void cond_init(struct condition *cond)
{
	ASSERT(cond != NULL);

	list_init(&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void cond_wait(struct condition *cond, struct lock *lock)
{
	/* Project1 - Priority Scheduling
	 */
	struct semaphore_elem waiter; // 세마포어의 element

	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	// condition variable의 waiters list에 우선순위로 삽입
	sema_init(&waiter.semaphore, 0);
	list_insert_ordered(&cond->waiters, &waiter.elem, cmp_sem_priority, NULL);

	// sema_down 에서 block 될 수 있기 때문에 다른 쓰레드가 사용할 수 있게 lock을 해제한다.
	lock_release(lock);
	sema_down(&waiter.semaphore);
	lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{

	/* Project1 - Priority Scheduling
	 * condition variable의 waiters list를 우선순위로 재정렬
	 * 대기 중에 우선순위가 변경되었을 가능성이 있음
	 */
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	if (!list_empty(&cond->waiters))
	{
		list_sort(&cond->waiters, cmp_sem_priority, NULL);
		sema_up(&list_entry(list_pop_front(&cond->waiters), struct semaphore_elem, elem)->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);

	while (!list_empty(&cond->waiters))
		cond_signal(cond, lock);
}

/*
 * cmp_sem_priority()
 * semaphore_elem으로 부터 각 semaphore_elem의 쓰레드 디스크립터를 획득
 * 첫 번째 인자로 주어진 세마포어를 위해 대기 중인 가장 높은 우선순위의 스레드와 두 번째 인자로 주어진 세마포어를 위해 대기 중인 가장 높은 우선순위의 스레드와 비교
 */
bool cmp_sem_priority(const struct list_elem *a, const struct list_elem *b, void *aux)
{
	struct semaphore_elem *sem_a = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sem_b = list_entry(b, struct semaphore_elem, elem);

	struct thread *th_a = list_entry(list_begin(&(sem_a->semaphore.waiters)), struct thread, elem);

	struct thread *th_b = list_entry(list_begin(&(sem_b->semaphore.waiters)), struct thread, elem);

	// 첫 번째 인자의 우선순위가 두 번째 인자의 우선순위보다 높으면 1을 반환, 낮으면 0을 반환
	return th_a->priority > th_b->priority ? true : false;
}