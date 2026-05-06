#pragma once

class SrwLock {
public:
    SrwLock() = default;

    SrwLock(const SrwLock&) = delete;
    SrwLock& operator=(const SrwLock&) = delete;

private:
    friend class SrwExclusiveLock;

    // The native static initializer is zero storage; the implementation verifies this buffer matches it.
    alignas(void*) unsigned char storage_[sizeof(void*)]{};
};

class SrwExclusiveLock {
public:
    explicit SrwExclusiveLock(SrwLock& lock);
    ~SrwExclusiveLock();

    SrwExclusiveLock(const SrwExclusiveLock&) = delete;
    SrwExclusiveLock& operator=(const SrwExclusiveLock&) = delete;

private:
    void* lock_ = nullptr;
};
