#include "util/srw_lock.h"

#include <windows.h>

static_assert(sizeof(SRWLOCK) == sizeof(SrwLock));
static_assert(alignof(SRWLOCK) <= alignof(SrwLock));

SrwExclusiveLock::SrwExclusiveLock(SrwLock& lock) : lock_(lock.storage_) {
    AcquireSRWLockExclusive(static_cast<SRWLOCK*>(lock_));
}

SrwExclusiveLock::~SrwExclusiveLock() {
    ReleaseSRWLockExclusive(static_cast<SRWLOCK*>(lock_));
}
