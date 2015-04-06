#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <stdio.h>
#include <assert.h>
#include <atomic.h>
#include <default_sched.h>

// the list of timer
static timer_list_t __timer_list;
static timer_list_t *timer_list;

static struct sched_class *sched_class;
static struct run_queue *rq;

static inline void
sched_class_enqueue(struct proc_struct *proc) {
    if (proc != idleproc) {
        sched_class->enqueue(rq, proc);
    }
}

static inline void
sched_class_dequeue(struct proc_struct *proc) {
    sched_class->dequeue(rq, proc);
}

static inline struct proc_struct *
sched_class_pick_next(void) {
    return sched_class->pick_next(rq);
}

static void
sched_class_proc_tick(struct proc_struct *proc) {
    if (proc != idleproc) {
        sched_class->proc_tick(rq, proc);
    }
    else {
        proc->need_resched = 1;
    }
}

static struct run_queue __rq;

void
sched_init(void) {
	timer_list = &__timer_list;
    list_init(&timer_list->list);
	spin_init(&timer_list->lock);


    sched_class = &default_sched_class;

    rq = &__rq;
    rq->max_time_slice = MAX_TIME_SLICE;
    sched_class->init(rq);

    cprintf("sched class: %s\n", sched_class->name);
}

void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE);
    spin_lock(&rq->lock);
    {
        if (proc->state != PROC_RUNNABLE) {
            proc->state = PROC_RUNNABLE;
            proc->wait_state = 0;
            if (proc != current) {
                sched_class_enqueue(proc);
            }
        }
        else {
            warn("wakeup runnable process.\n");
        }
    }
    spin_unlock(&rq->lock);
}

void
schedule(void) {
   
    struct proc_struct *next;
    spin_lock(&rq->lock);
    {
        current->need_resched = 0;
        if (current->state == PROC_RUNNABLE) {
            sched_class_enqueue(current);
        }
        if ((next = sched_class_pick_next()) != NULL) {
            sched_class_dequeue(next);
        }
        if (next == NULL) {
            next = idleproc;
        }
        next->runs ++;
        if (next != current) {
            proc_run(next);
        }
    }
    spin_unlock(&rq->lock);
}

void
add_timer(timer_t *timer) {
    spin_lock(&timer_list->lock);
    {
        assert(timer->expires > 0 && timer->proc != NULL);
        assert(list_empty(&(timer->timer_link)));
        list_entry_t *le = list_next(&timer_list->list);
        while (le != &timer_list->list) {
            timer_t *next = le2timer(le, timer_link);
            if (timer->expires < next->expires) {
                next->expires -= timer->expires;
                break;
            }
            timer->expires -= next->expires;
            le = list_next(le);
        }
        list_add_before(le, &(timer->timer_link));
    }
    spin_unlock(&timer_list->lock);
}

// del timer from timer_list
void
del_timer(timer_t *timer) {
    spin_lock(&timer_list->lock);
    {
        if (!list_empty(&(timer->timer_link))) {
            if (timer->expires != 0) {
                list_entry_t *le = list_next(&(timer->timer_link));
                if (le != &timer_list->list) {
                    timer_t *next = le2timer(le, timer_link);
                    next->expires += timer->expires;
                }
            }
            list_del_init(&(timer->timer_link));
        }
    }
    spin_unlock(&timer_list->lock);
}

// call scheduler to update tick related info, and check the timer is expired? If expired, then wakup proc
void
run_timer_list(void) {
    spin_lock(&timer_list->lock);
    {
        list_entry_t *le = list_next(&timer_list->list);
        if (le != &timer_list->list) {
            timer_t *timer = le2timer(le, timer_link);
            assert(timer->expires != 0);
            timer->expires --;
            while (timer->expires == 0) {
                le = list_next(le);
                struct proc_struct *proc = timer->proc;
                if (proc->wait_state != 0) {
                    assert(proc->wait_state & WT_INTERRUPTED);
                }
                else {
                    warn("process %d's wait_state == 0.\n", proc->pid);
                }
                wakeup_proc(proc);
                del_timer(timer);
                if (le == &timer_list->list) {
                    break;
                }
                timer = le2timer(le, timer_link);
            }
        }
        sched_class_proc_tick(current);
    }
    spin_unlock(&timer_list->lock);
}
