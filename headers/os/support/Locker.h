/*
 * Copyright 2001-2010 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef	_LOCKER_H
#define	_LOCKER_H


#ifdef __VOS__
#include <BeBuild.h>
#endif /* __VOS__ */
#include <OS.h>


class BLocker {
public:
								BLocker();
								BLocker(const char* name);
								BLocker(bool benaphoreStyle);
								BLocker(const char* name, bool benaphoreStyle);
	virtual						~BLocker();

			status_t			InitCheck() const;

			bool				Lock();
			status_t			LockWithTimeout(bigtime_t timeout);
			void				Unlock();

			thread_id			LockingThread() const;
			bool				IsLocked() const;
			int32				CountLocks() const;
			int32				CountLockRequests() const;
#ifndef __VOS__
			sem_id				Sem() const;
#else /* __VOS__ */

			// Deprecated, no semaphore backs the lock and it returns
			// B_ERROR.
			sem_id				Sem() const _DEPRECATED;
#endif /* __VOS__ */

private:
								BLocker(const char* name, bool benaphoreStyle,
									bool _ignored);
								BLocker(const BLocker&);
								BLocker& operator=(const BLocker&);
			void				InitLocker(const char* name,
									bool benaphoreStyle);
			bool				AcquireLock(bigtime_t timeout, status_t* error);

			int32				fBenaphoreCount;
#ifndef __VOS__
			sem_id				fSemaphoreID;
#endif /* ! __VOS__ */
			thread_id			fLockOwner;
#ifndef __VOS__
			int32				fRecursiveCount;
#endif /* ! __VOS__ */

#ifndef __VOS__
			int32				_reserved[4];
#else /* __VOS__ */
			// Storage of a user_sem, see UserSemaphore.h.
			int32				fSemLock;
			int32				fSemCount;
			int32				fSemInside;
			int32				fRecursiveCount;	// the user_sem spare word
			void*				fSemQueue;
#endif /* __VOS__ */
};


#endif	// _LOCKER_H
