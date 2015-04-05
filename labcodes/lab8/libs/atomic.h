#ifndef __LIBS_ATOMIC_H__
#define __LIBS_ATOMIC_H__

/* Atomic operations that C can't guarantee us. Useful for resource counting etc.. */

static inline void set_bit(int nr, volatile void *addr) __attribute__((always_inline));
static inline void clear_bit(int nr, volatile void *addr) __attribute__((always_inline));
static inline void change_bit(int nr, volatile void *addr) __attribute__((always_inline));
static inline bool test_bit(int nr, volatile void *addr) __attribute__((always_inline));

/* *
 * set_bit - Atomically set a bit in memory
 * @nr:     the bit to set
 * @addr:   the address to start counting from
 *
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 * */
static inline void
set_bit(int nr, volatile void *addr) {
    asm volatile ("btsl %1, %0" :"=m" (*(volatile long *)addr) : "Ir" (nr));
}

/* *
 * clear_bit - Atomically clears a bit in memory
 * @nr:     the bit to clear
 * @addr:   the address to start counting from
 * */
static inline void
clear_bit(int nr, volatile void *addr) {
    asm volatile ("btrl %1, %0" :"=m" (*(volatile long *)addr) : "Ir" (nr));
}

/* *
 * change_bit - Atomically toggle a bit in memory
 * @nr:     the bit to change
 * @addr:   the address to start counting from
 * */
static inline void
change_bit(int nr, volatile void *addr) {
    asm volatile ("btcl %1, %0" :"=m" (*(volatile long *)addr) : "Ir" (nr));
}

/* *
 * test_bit - Determine whether a bit is set
 * @nr:     the bit to test
 * @addr:   the address to count from
 * */
static inline bool
test_bit(int nr, volatile void *addr) {
    int oldbit;
    asm volatile ("btl %2, %1; sbbl %0,%0" : "=r" (oldbit) : "m" (*(volatile long *)addr), "Ir" (nr));
    return oldbit != 0;
}

/* *
 * test_and_set_bit - Atomically set a bit and return its old value
 * @nr:     the bit to set
 * @addr:   the address to count from
 * */
static inline bool
test_and_set_bit(int nr, volatile void *addr) {
    int oldbit;
    asm volatile ("btsl %2, %1; sbbl %0, %0" : "=r" (oldbit), "=m" (*(volatile long *)addr) : "Ir" (nr) : "memory");
    return oldbit != 0;
}

/* *
 * test_and_clear_bit - Atomically clear a bit and return its old value
 * @nr:     the bit to clear
 * @addr:   the address to count from
 * */
static inline bool
test_and_clear_bit(int nr, volatile void *addr) {
    int oldbit;
    asm volatile ("btrl %2, %1; sbbl %0, %0" : "=r" (oldbit), "=m" (*(volatile long *)addr) : "Ir" (nr) : "memory");
    return oldbit != 0;
}

typedef struct spinlock{
	volatile int32_t locker;
}spinlock_t;

#define SPIN_LOCK_UNLOCK 1;
#define SPIN_LOCK_LOCKED 0;

#ifdef CFG_SMP		//Only if SMP, the spin lock will work
#define spin_init(sl) arch_spin_init(sl)
#define spin_init_locked(sl) arch_spin_init_locked(sl)
#define spin_is_locked(sl) arch_spin_is_locked(sl)
#define spin_lock(sl) arch_spin_lock(sl)
#define spin_try_lock(sl) arch_spin_try_lock(sl)
#define spin_unlock(sl) arch_spin_unlock(sl)


#define arch_spin_init(sl)	(sl->locked = SPIN_LOCK_UNLOCK;)

#define arch_spin_init_locked(sl) (sl->locked = SPIN_LOCK_LOCKED;)

#define arch_spin_is_locked (sl->locked!=0)

static inline void arch_spin_lock(spinlock_t *sl)
{
	__asm__ __volatile__(
			"1:\n\t"\
			"lock;decb %0\n\t"\
			"jns 3f\n\t"\
			"2:\n\t"\
			"cmpb $0,%0\n\t"\
			"jle 2b\n\t"\
			"jmp 1b\n\t"\
			"3:\n\t"\
			:"=m" (sl->locker) : : "memory"
			);
}

static inline void arch_spin_unlock(spinlock_t *sl)
{
	char oldval=1;
	__asm__ __volatile__(
		"xchgb %b0,%1"
		:"=q" (oldval), "=m" (sl->locker)
		:"0" (oldval) : "memory");
}
static inline int arch_spin_trylock(spinlock_t *sl)
{
	char oldval;
	__asm__ __volatile__(
			"xchgb %b0,%1"
			:"=q" (oldval), "=m" (sl->locker)
			:"0" (0) : "memory");
	return oldval > 0;
}

#else
#define spin_init(sl) 
#define spin_init_locked(sl) 
#define spin_is_locked(sl) 
#define spin_lock(sl) 
#define spin_try_lock(sl) 
#define spin_unlock(sl) 

#endif 
#endif /* !__LIBS_ATOMIC_H__ */

