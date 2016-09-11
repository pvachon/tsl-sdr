#pragma once

#include <tsl/cal.h>

#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>

struct ticket_lock {
    union {
        struct {
            _Atomic uint32_t current;
            _Atomic uint32_t ticket;
        } CAL_PACKED;
        _Atomic uint64_t value;
    };
} CAL_PACKED_ALIGNED(8);

#define TICKET_LOCK_INIT            (struct ticket_lock) { .value = 0 }
#define TICKET_LOCK_SHIFT           32
#define TICKET_LOCK_VALUE_MSK       0xffffffffull
#define TICKET_LOCK_INC             0x100000000ull

static inline
void ticket_lock_init(struct ticket_lock *lck)
{
    lck->value = 0;
}

static inline
void ticket_lock_acquire(struct ticket_lock *lck)
{
    /* Since this is a counter, we don't care about ordering as long as every operation
     * is unique.
     */
    uint64_t value = atomic_fetch_add_explicit(&lck->value, TICKET_LOCK_INC, memory_order_acquire);
    uint32_t ticket = value >> TICKET_LOCK_SHIFT;
    uint64_t backoff = 1;

    while (CAL_UNLIKELY(ticket != (value & TICKET_LOCK_VALUE_MSK))) {
        /* Backoff for 64-ish times per delta between our ticket and the current value */
        backoff = (ticket - (value & TICKET_LOCK_VALUE_MSK)) << 5;

        for (volatile uint64_t bo = backoff; bo > 0; bo--);

        value = atomic_load_explicit(&lck->value, memory_order_acquire);
    }
}

static inline
void ticket_lock_release(struct ticket_lock *lck)
{
    /* Only modify the ticket (a 32-bit int), so wraparound doesn't overflow into
     * the waiter ticket counter.
     */
    atomic_fetch_add_explicit(&lck->current, 1, memory_order_release);
}

static inline
bool ticket_lock_try_acquire(struct ticket_lock *lck)
{
    /*
     * We don't want a failure to be visible, so grab the lock value as-is
     */
    uint64_t value = atomic_load_explicit(&lck->value, memory_order_relaxed);
    uint64_t ticket = value >> TICKET_LOCK_SHIFT;

    if (ticket != (value & TICKET_LOCK_VALUE_MSK)) {
        return false;
    }

    return atomic_compare_exchange_strong_explicit(&lck->value, &value, value + TICKET_LOCK_INC,
            memory_order_acquire, memory_order_relaxed);
}
