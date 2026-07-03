# Codexion — Personal Study Guide

> This is **not** the README the subject asks for (that's `README.md`).
> This file is for **you**: every concept, every file, every line that
> matters, and the answers to expect during the evaluation.

---

## Table of contents

1. [The problem in plain English](#1-the-problem-in-plain-english)
2. [POSIX threads cheat sheet](#2-posix-threads-cheat-sheet)
3. [Architecture overview](#3-architecture-overview)
4. [File-by-file walkthrough](#4-file-by-file-walkthrough)
5. [Full execution trace](#5-full-execution-trace)
6. [Defense Q&A](#6-defense-qa)
7. [Common traps & how to debug](#7-common-traps--how-to-debug)

---

## 1. The problem in plain English

You have N coders sitting in a circle. Between every pair of neighbors there is **one** USB dongle. So if there are 5 coders, there are 5 dongles.

A coder cycles through three states:

```
compile -> debug -> refactor -> compile -> debug -> refactor -> ...
```

To **compile**, a coder needs to hold **two** dongles simultaneously (one in each hand). They are the dongle on their left and the dongle on their right.

Two extra rules make it tricky:

- **Burnout** — if a coder hasn't *started a new compile* within `time_to_burnout` ms of starting the previous one, they burn out and the simulation stops.
- **Cooldown** — after a dongle is released, it cannot be picked up again for `dongle_cooldown` ms.

When several coders want the same dongle, a scheduler decides who gets it:

- **FIFO** — whoever asked first.
- **EDF** (Earliest Deadline First) — whoever is closest to burning out.

The whole thing is a classic concurrency exercise (it's literally the **Dining Philosophers** problem with new names).

---

## 2. POSIX threads cheat sheet

Open this section every time you forget what a primitive does.

### Thread

A thread is a path of execution **inside** a process. All threads share the same memory.

```c
pthread_t t;
pthread_create(&t, NULL, my_function, my_argument);
pthread_join(t, NULL);   // wait for thread to finish
```

`my_function` has signature `void *f(void *)`. The `void *` argument is how you pass data in; the return value is how you pass data out.

### Mutex

A mutex is a lock. Only one thread can hold it at a time. Use it around any shared data you read or write.

```c
pthread_mutex_t m;
pthread_mutex_init(&m, NULL);
pthread_mutex_lock(&m);   // blocks if someone else has it
/* critical section */
pthread_mutex_unlock(&m);
pthread_mutex_destroy(&m);
```

If you forget to unlock, every other thread that tries to lock it freezes forever. That's a **deadlock**.

### Condition variable

A way for a thread to **sleep until something happens**, without burning CPU.

```c
pthread_cond_t c;
pthread_mutex_t m;          /* condition variables ALWAYS come with a mutex */

pthread_mutex_lock(&m);
while (!some_condition)
    pthread_cond_wait(&c, &m);   /* atomically unlock m + sleep; relock on wake */
pthread_mutex_unlock(&m);
```

To wake waiters:

```c
pthread_mutex_lock(&m);
some_condition = 1;
pthread_cond_broadcast(&c);   /* wakes ALL waiters */
pthread_mutex_unlock(&m);
```

Why `while`, not `if`? Three reasons:

1. **Spurious wakeups** happen in real systems.
2. Another waiter might have raced you and taken the resource.
3. The condition might have flipped back to false.

### `pthread_cond_timedwait`

Like `pthread_cond_wait` but wakes after an **absolute** wall-clock time.

```c
struct timespec ts;     /* absolute time, not delta */
pthread_cond_timedwait(&c, &m, &ts);
```

We use this when a coder is *next in line* for a dongle but the dongle is still in cooldown. They sleep exactly until the cooldown ends.

### `gettimeofday`

Returns the current wall-clock time.

```c
struct timeval tv;
gettimeofday(&tv, NULL);
long us = tv.tv_sec * 1000000L + tv.tv_usec;   /* microseconds since epoch */
```

We use microseconds internally for precision and divide by 1000 only when *printing* (the subject wants milliseconds).

---

## 3. Architecture overview

```
                    +-----------------+
                    |     main.c      |
                    |   (main thread) |
                    +--------+--------+
                             |
                  parses args + sim_init
                             |
            +----------------+----------------+
            |                                 |
       spawn coders                     spawn monitor
            |                                 |
   N pthreads running              1 pthread running
   coder_routine                   monitor_routine
            |                                 |
   each one alternates:               loops every 500us:
     - acquire 2 dongles                - if any coder is past burnout -> stop + print
     - compile (200ms)                  - if all reached n_required compiles -> stop
     - release 2 dongles
     - debug, refactor
     - back to acquire
```

**Shared state and which lock protects it**

| Data                                | Protected by         |
|-------------------------------------|----------------------|
| `dongle.available`, `last_release`, `queue` | `dongle.mtx`     |
| `coder.last_compile`, `compiles`    | `coder.mtx`          |
| `sim.stop`, `sim.ready`, `sim.go`   | `sim.sim_mtx`        |
| `stdout`                            | `sim.print_mtx`      |

**Condition variables**

| Cond variable     | Wakes who                                                   |
|-------------------|-------------------------------------------------------------|
| `sim.sim_cond`    | Coders + monitor waiting for the simulation to start; also broadcast on stop |
| `dongle.cond`     | Coders waiting for that specific dongle                     |

---

## 4. File-by-file walkthrough

### 4.1 `codexion.h` — the data model

Everything else in the project is defined in terms of these structs.

```c
typedef struct s_args {
    int n_coders, t_burnout, t_compile, t_debug, t_refactor;
    int n_required, t_cooldown, scheduler;   /* scheduler is FIFO or EDF */
} t_args;
```

The 8 command-line arguments, after parsing.

```c
typedef struct s_request { int id; long key; } t_request;
typedef struct s_heap    { t_request items[2]; int size; } t_heap;
```

One slot in the heap is a (coder id, sort key) pair. Heap size is at most 2 because **only two neighbors ever compete for the same dongle**.

```c
struct s_dongle {
    int             id;
    int             available;     /* 1 = on the table, 0 = in a coder's hand */
    long            last_release;  /* microseconds since epoch */
    t_heap          queue;         /* who wants this dongle */
    pthread_mutex_t mtx;
    pthread_cond_t  cond;
};
```

```c
struct s_coder {
    int             id;            /* 1..n */
    int             compiles;      /* how many times this coder has compiled */
    long            last_compile;  /* timestamp of the last compile START */
    pthread_mutex_t mtx;
    t_dongle       *low;           /* the dongle with the SMALLER id */
    t_dongle       *high;          /* the dongle with the LARGER id */
    t_sim          *sim;
};
```

The reason for `low`/`high` is **deadlock prevention** (resource hierarchy — explained later).

```c
struct s_sim {
    t_args          args;
    long            start;         /* simulation start in microseconds */
    int             stop;          /* 1 = simulation must end */
    int             ready;         /* how many coders called wait_for_start */
    int             go;            /* 1 = main thread says "go" */
    int             started;       /* how many coder threads were created */
    int             mon_started;   /* 1 = monitor thread was created */
    t_coder        *coders;
    t_dongle       *dongles;
    pthread_t      *threads;
    pthread_t       mon;
    pthread_mutex_t print_mtx;     /* serializes printf */
    pthread_mutex_t sim_mtx;       /* protects stop, ready, go */
    pthread_cond_t  sim_cond;
};
```

The `started` and `mon_started` counters exist so that if `pthread_create` fails partway, `join_all` still knows how many threads to join.

---

### 4.2 `parsing.c` — turning argv into `t_args`

```c
static int ft_atoi_pos(const char *s, int *out)
```

A safe `atoi` that:

- **Rejects** empty strings (`!*s`).
- **Rejects** any non-digit (so `-5`, `12abc`, `  3` all fail).
- **Rejects** overflows (`n > 2147483647L`).
- **Writes** the result through `out` and returns `OK`/`ERR`.

We use `long` for `n` so that we can detect the overflow before truncating to `int`.

```c
static int parse_scheduler(const char *s, int *out)
```

Returns `OK` only for the literal strings `"fifo"` or `"edf"`. `strcmp` returns 0 on equal, so `!strcmp` is the idiomatic "strings are equal".

```c
static int parse_numbers(char **av, t_args *a)
```

Wires the 7 numeric arguments into the 7 numeric fields using an array of pointers — this is what keeps the function short enough for the 42 Norm.

```c
int parse_args(int ac, char **av, t_args *a)
```

The top-level check:

- `ac != 9` → reject (program name + 8 args).
- Parse numbers and scheduler.
- Reject `n_coders < 1` (you need at least one coder) and `n_required < 1` (it would terminate immediately).

We accept `t_burnout = 0` etc. — those just make the simulation end fast. The subject does not forbid them.

---

### 4.3 `utils.c` — time, logging, sleep, stop check

```c
long now_us(void)
```

One-line wrapper around `gettimeofday`. Returns absolute microseconds since the Unix epoch. **Never call `gettimeofday` directly in the rest of the code** — always go through this function.

```c
int sim_stopped(t_sim *sim)
```

Locks `sim_mtx`, reads `stop`, unlocks. The lock is mandatory because `stop` is shared between the monitor (writer) and every coder (reader).

```c
int log_state(t_coder *c, const char *msg)
```

The normal logger used by coders. Steps:

1. Lock `print_mtx`.
2. Lock `sim_mtx`, read `stop`, unlock `sim_mtx`.
3. If stopped, **abort**: unlock `print_mtx`, return `ERR`. This prevents any state line ("is debugging" etc.) from being printed *after* the burnout line.
4. Compute relative timestamp `(now_us() - start) / 1000` in milliseconds.
5. `printf` the line.
6. Unlock `print_mtx`.

Important: `sim_mtx` is locked **inside** the `print_mtx` critical section. We always take locks in the same order across the project, so no inversion can deadlock. (`print_mtx` → `sim_mtx`, never the reverse.)

```c
void force_log(t_coder *c, const char *msg)
```

Same as `log_state` but **without** the stop check. This is what the monitor uses to print `burned out`, because that one line must be printed even though `stop` is already 1.

```c
int precise_sleep(t_coder *c, long us)
```

A `usleep`-based sleep that **wakes up every 500 µs** to check whether the simulation has been stopped. That way, if a coder is mid-compile when burnout happens, they get out within a millisecond instead of running the full compile time.

---

### 4.4 `heap.c` — the priority queue

We must implement a real binary min-heap (the subject forbids using a standard library queue). The heap key is **smaller = higher priority**.

```c
static void swap_req(t_request *a, t_request *b)
```

Three-line value swap.

```c
static int req_less(const t_request *a, const t_request *b)
```

The ordering predicate the heap uses everywhere. Returns true when `a` should sit above `b`. Primary order: smaller key wins (= older FIFO arrival or nearer EDF deadline). When keys tie, the lower `id` wins.

**Why the tie-breaker matters.** The subject explicitly requires a fully deterministic EDF policy "even in edge cases" where two deadlines collide. The most common collision is T=0: `stamp_starts` writes the same `sim->start` into every coder's `last_compile`, so every EDF key is identical. Without a tie-breaker the heap would leave requests in thread-scheduler order — nondeterministic across runs. Falling back to "lower id wins" guarantees a reproducible startup pattern.

```c
static void bubble_up(t_heap *h, int i)
```

When you insert at position `i` (the end), compare with the parent at `(i-1)/2` using `req_less`. If we outrank the parent, swap and move up. Loop until you're at the root or the heap property holds. Classic binary heap insertion.

```c
static void sift_down(t_heap *h, int i)
```

When you remove the root, you put the last element at position 0 and then push it down: at each step, find the smaller of its two children (via `req_less`), swap if needed, continue. Classic binary heap deletion.

```c
void heap_push(t_heap *h, int id, long key)
```

Adds a new request and bubbles it up. If the heap is full (size 2), it silently drops — for our use case this can't happen because only two neighbors ever push to a given heap.

```c
int heap_top(t_heap *h)
```

Returns the **id** of the current top, or `-1` if empty. The caller uses this to ask "am I at the front of the queue?".

```c
void heap_pop(t_heap *h)
```

Swaps top with last, decreases size, sifts down. Classic pattern.

For our problem the heap has at most 2 elements so bubble/sift do almost nothing, but the algorithm is general and correct.

---

### 4.5 `init.c` — building the simulation

```c
static int init_dongles(t_sim *sim)
```

Allocates `n` dongles, `memset`s them to zero (this is why `last_release = 0` and `queue.size = 0` work out of the box), sets `id` and `available = 1`, initializes the mutex and the condition variable.

```c
static void assign_neighbors(t_sim *sim, int i)
```

**This is the deadlock fix.** For coder at index `i`:

- `left = i`, `right = (i + 1) % n`.
- If `left < right`: `low = dongle[left]`, `high = dongle[right]`.
- Else (only for the last coder, where wrap-around makes `right < left`): `low = dongle[right]`, `high = dongle[left]`.

So every coder grabs the **lower-numbered** dongle first. This breaks the circular wait (more on that in section 6).

```c
static int init_coders(t_sim *sim)
```

Same pattern as `init_dongles`, plus the call to `assign_neighbors`.

```c
int sim_init(t_sim *sim)
```

Top-level init: mutexes/cond, dongles, coders, the array of pthread handles. Returns `ERR` on any malloc failure.

```c
void sim_destroy(t_sim *sim)
```

Destroys every mutex/cond, frees every malloc'd array. The `while (sim->dongles && ...)` guards are there so that partial init failures don't crash the cleanup.

---

### 4.6 `dongle.c` — the heart of the project

This is where deadlock prevention, cooldown, FIFO/EDF and the heap all meet. Read this section twice.

```c
static long pick_key(t_coder *c)
```

Builds the sort key for the request:

- Under **EDF**: `last_compile + t_burnout * 1000` — the absolute moment in microseconds at which this coder would burn out. Earlier deadline → smaller key → higher priority.
- Under **FIFO**: `now_us()` — the moment of request. Older request → smaller key → higher priority.

```c
static void register_request(t_dongle *d, int id, long key)
```

Locks the dongle, pushes the request into its heap, broadcasts so any existing waiter re-evaluates "am I still top of the heap?", unlocks.

```c
static int try_take(t_dongle *d, t_coder *c)
```

Called while holding `d->mtx`. Returns 1 only if **all three** are true:

1. Coder is at the top of the heap.
2. Dongle is `available`.
3. We are past the cooldown window (`now >= last_release + cooldown`).

If yes, pops the heap and sets `available = 0`. Otherwise returns 0 — caller must wait.

```c
static void compute_abs(struct timespec *ts, long delta_us)
```

Helper that turns a "wait this many microseconds from now" into the **absolute** time required by `pthread_cond_timedwait`. Adds the delta to current `gettimeofday`, then normalizes seconds/nanoseconds.

```c
static void wait_choice(t_dongle *d, t_coder *c)
```

Smart wait. Two cases:

- If I am the top of the heap **and** the dongle is `available` but still cooling down → use `pthread_cond_timedwait` to wake exactly when the cooldown ends.
- Otherwise → plain `pthread_cond_wait`; someone else will signal us when they release.

```c
static int wait_for_dongle(t_dongle *d, t_coder *c)
```

The main acquisition loop. Locks the mutex, then loops:

1. If `sim_stopped`, unlock and return `ERR`.
2. Try to take. If success, unlock and return `OK`.
3. Otherwise, `wait_choice` (releases the mutex while sleeping).
4. Repeat.

```c
static int release_one(t_dongle *d)
```

Locks, sets `available = 1` and `last_release = now`, broadcasts so any waiter retries, unlocks. Returns `ERR` so callers can write `return (release_one(c->low));` after a failure.

```c
int acquire_dongles(t_coder *c)
```

The whole acquisition sequence:

1. Build the key for the **low** dongle.
2. Register on the low dongle's heap.
3. Wait for `low`. If sim stopped → bail (nothing held yet).
4. Print `has taken a dongle`. If logging fails (sim stopped) → release `low`, bail.
5. Refresh the key, register on the **high** dongle's heap.
6. Wait for `high`. If sim stopped → release `low`, bail.
7. Print `has taken a dongle`. If logging fails → release both, bail.
8. Return `OK`.

**Why sequential registration?** If a coder pre-registered on both heaps at the start, they'd sit at the top of the high dongle's heap while still waiting on the low one. That blocks the high dongle for everyone else even though it's physically free. Registering only when actually ready to wait keeps the heap an honest reflection of who's currently competing.

```c
void release_dongles(t_coder *c)
```

Releases both. Calls `release_one` twice; the return values are discarded.

---

### 4.7 `coder.c` — the coder routine

```c
static int do_compile(t_coder *c)
```

1. Take the coder's lock, update `last_compile = now_us()`, release lock. (The lock is needed because the monitor reads this field.)
2. Print `is compiling`. If it fails (sim stopped) → release dongles, return `ERR`.
3. `precise_sleep` for `t_compile` ms. If it fails → release dongles, return `ERR`.
4. Take the coder's lock, `compiles++`, release lock.
5. Release both dongles.

Note: `last_compile` is updated **at the start** of the compile, not the end. That matches the subject's definition of the burnout deadline.

```c
static int do_debug(t_coder *c)
static int do_refactor(t_coder *c)
```

Just `log_state` + `precise_sleep`. No dongle work.

```c
static void wait_for_start(t_coder *c)
```

The start barrier. Increments `ready`, then waits on `sim_cond` until `go` is set by the main thread. This guarantees no coder runs before the simulation has officially started.

```c
static int stagger_start(t_coder *c)
```

If we have more than one coder and this coder is even-id or the last coder, sleep for half a compile time. This breaks the symmetric start where every coder would race for dongles at exactly T=0 — without it, on tight timings a long sequential "I hold A, I want B" chain can form and one coder burns out at the chain tail.

```c
void *coder_routine(void *arg)
```

The function each coder thread runs:

```c
wait_for_start(c);
if (stagger_start(c)) return (NULL);
while (!sim_stopped(c->sim)) {
    if (acquire_dongles(c)) return (NULL);
    if (do_compile(c))       return (NULL);
    if (do_debug(c) || do_refactor(c)) return (NULL);
}
```

A single tight loop with early-out on any error.

---

### 4.8 `monitor.c` — the watcher

```c
void wake_all(t_sim *sim)
```

Broadcasts on every condition variable that any thread might be sleeping on. After this call, every coder gets a chance to re-check `sim_stopped` and exit.

```c
static void mark_stop(t_sim *sim)
```

Sets `sim->stop = 1` under the lock. Two-line helper, used by both burnout and "all done" paths.

```c
static int check_burnout(t_sim *sim, int i)
```

For coder `i`:

1. Read `last_compile` under that coder's mutex.
2. Compute `deadline = last_compile + t_burnout * 1000`.
3. If `now_us() >= deadline`: mark stop, `force_log("burned out")`, wake everyone, return 1.

Otherwise return 0.

```c
static int check_all_done(t_sim *sim)
```

Walks every coder, checks `compiles >= n_required`. If all are done, mark stop, wake everyone, return 1.

```c
static void monitor_wait_start(t_sim *sim)
```

Same barrier as `wait_for_start` but **without** incrementing `ready`. The monitor doesn't count toward the ready count; it just waits for `go = 1`.

```c
void *monitor_routine(void *arg)
```

The watcher loop: every 500 µs, scan every coder for burnout, then check completion. Polling at 500 µs guarantees burnouts are detected well within the mandated 10 ms.

---

### 4.9 `main.c` — wiring everything

```c
static int spawn_threads(t_sim *sim)
```

Creates the `n_coders` coder threads, then the monitor thread. Increments `started`/`mon_started` so cleanup knows what was actually created.

```c
static void stamp_starts(t_sim *sim)
```

Once we know the simulation's official start time, write it into every coder's `last_compile`. Without this, the monitor would see `last_compile = 0` and immediately think every coder has been idle since 1970.

```c
static void start_simulation(t_sim *sim)
```

1. Spin (with `usleep(500)`) until `ready >= n_coders`.
2. Record `sim->start`.
3. Stamp every coder's `last_compile`.
4. Set `go = 1` and broadcast — all coders and the monitor wake up.
5. Unlock.

```c
static void join_all(t_sim *sim)
```

`pthread_join` every started coder, then the monitor.

```c
static void abort_threads(t_sim *sim)
```

If thread creation fails, we still have to release any coders that already reached `wait_for_start`. This forces `stop = 1`, `go = 1`, broadcasts everything.

```c
int main(int ac, char **av)
```

`memset` the sim struct (so all the `sim->...` fields start at zero), parse args, init, run, destroy. Returns 0 on success, 1 on failure.

---

### 4.10 `Makefile`

```
NAME    = codexion
SRCS    = main.c parsing.c init.c heap.c dongle.c coder.c monitor.c utils.c
OBJS    = $(SRCS:.c=.o)
HEADER  = codexion.h
CC      = cc
CFLAGS  = -Wall -Wextra -Werror -pthread
```

The flags are the ones mandated by the subject. `-pthread` is what makes `pthread_*` actually link.

The pattern rule `%.o: %.c $(HEADER)` declares the header dependency so editing it triggers a full rebuild.

Five mandatory targets: `all`, `$(NAME)`, `clean`, `fclean`, `re`. `.PHONY` keeps `make` from getting confused if a file named `all` ever shows up.

---

## 5. Full execution trace

Walk through `./codexion 3 800 200 200 200 5 50 fifo`:

```
T+0    main parses args, allocates 3 dongles + 3 coders.
T+0    main spawns 3 coder threads + 1 monitor thread.
T+0    Each coder calls wait_for_start, increments ready, sleeps on sim_cond.
T+0    Monitor calls monitor_wait_start, sleeps on sim_cond.
T+0    main's start_simulation sees ready == 3, sets start=T0,
       stamps last_compile=T0 on every coder, sets go=1, broadcasts.
T+0    All 4 threads wake. Coders enter the main loop.

Coder 1 wants dongles {0, 1}. low=0, high=1.
Coder 2 wants dongles {1, 2}. low=1, high=2.
Coder 3 wants dongles {0, 2}. low=0, high=2.  <-- the wrap-around coder

All three call acquire_dongles ~simultaneously.

Coder 1 registers on heap[0] then heap[1].
Coder 2 registers on heap[1] then heap[2].
Coder 3 registers on heap[0] then heap[2].

For FIFO the heap is sorted by arrival timestamp. Whichever coder
registered first on each dongle is "top".

Say coder 1 is first on heap[0] -> takes dongle 0.
Coder 3 sees heap[0] top != 3 -> waits on dongle 0's cond.

Coder 1 then waits for dongle 1.
If coder 1 is top on heap[1] -> takes it.
Coder 2 sees heap[1] top != 2 -> waits on dongle 1's cond.

Coder 1: "has taken a dongle" x2, "is compiling" at T+1ms or so.
Coder 1 sleeps 200ms (compile).

Meanwhile, coder 2 has dongle 2 already? No, coder 2 also waits for
dongle 1 first (low<high), so coder 2 is just blocked. Coder 3 is blocked.

T+201   Coder 1 finishes compile, increments compiles to 1, releases
        dongles 0 and 1 (last_release = now, broadcast cond).
T+201   Coder 1: "is debugging", sleeps 200ms.

Now dongles 0 and 1 are released but in cooldown for 50ms.

T+251   (cooldown ends on both 0 and 1)
        Coder 3 (top of heap[0] now) takes dongle 0.
        Coder 2 (top of heap[1] now) takes dongle 1.
        Coder 3 then waits for dongle 2 (which nobody has) -> takes it.
        Coder 2 also wants dongle 2... wait. Coder 3 is faster -> coder 2
        waits.

Hmm wait — coder 3 also wants dongle 2 (high). Coder 2 ALSO wants
dongle 2 (high). One of them is top of heap[2].

Whoever registered first on heap[2] wins. Both registered at T+0, so
order depends on thread scheduling — but only ONE of them is top.
The other waits.

... and so on, looping until each coder has compiled 5 times.

T+~3000 Monitor's check_all_done sees compiles == 5 for all -> mark stop,
        wake_all, return.
T+~3000 Each coder wakes, sees sim_stopped, exits its routine.
T+~3000 main's join_all returns. sim_destroy frees everything. Exit 0.
```

If you instead pass tight numbers like `./codexion 3 400 200 200 200 100 50 fifo`, one coder will eventually miss its 400 ms deadline. The monitor's `check_burnout` catches it, prints `<ts> X burned out`, wakes everyone, and the program exits.

---

## 6. Defense Q&A

These are the questions evaluators (and Deepthought-style robo-evaluators) like to ask. Answers in your own words are best — these are templates.

### "Why do you need pthread_mutex_t for the dongle?"

Because two coder threads could both try to take the same dongle at the same time. Without the mutex, both could read `available = 1`, both decide to take it, both set `available = 0`. Two coders would hold the same dongle — physically impossible. The mutex makes the read/check/set sequence **atomic**.

### "Why not just use atomic variables, no mutex?"

Because the operation isn't a single read or write. We check `heap_top == my_id && available && cooldown_done`, then pop the heap and set available. That's many variables. Only a mutex can group them into a single critical section.

### "Why a condition variable instead of looping with `usleep`?"

Two reasons:

- **CPU cost**. A `cond_wait` thread uses zero CPU. A `usleep` loop wakes up every few microseconds for nothing.
- **Latency**. With `cond_wait`, the moment another coder releases the dongle, the waiter is woken instantly. With `usleep`, the waiter might sit for up to one full sleep interval before noticing.

### "Why do you call `pthread_cond_wait` inside a `while`, not an `if`?"

For three reasons: spurious wakeups, another waiter racing me, or the condition flipping back to false. Always re-check after waking up.

### "What is a deadlock and how do you prevent one here?"

Deadlock = a cycle of threads each waiting for a resource the next thread holds. Coffman's four conditions must all hold: mutual exclusion, hold-and-wait, no preemption, circular wait.

I break **circular wait** with a **resource hierarchy**: every coder always grabs the dongle with the smaller ID first. The last coder (id N) shares dongle 0 with coder 1, so it also grabs 0 first. No cycle is possible.

### "What about starvation? Is your code starvation-free?"

Under EDF, yes — as long as the parameters are feasible. The heap key under EDF is `last_compile + t_burnout`, so a coder that has been idle the longest has the smallest key and wins the next contest. No coder can be passed forever.

Under FIFO it's first-come-first-served, which is also not starvation-prone in this 2-neighbor setting.

### "Walk me through how a dongle gets released and the next coder picks it up."

1. The current owner calls `release_dongles`, which calls `release_one` on each dongle.
2. `release_one` locks the dongle, sets `available = 1`, sets `last_release = now`, **broadcasts** the dongle's cond, unlocks.
3. Every waiter on that cond wakes up.
4. In `wait_for_dongle`, each woken thread re-checks `try_take`: am I top of the heap, is it available, am I past cooldown?
5. Only the heap's top wins. The others see "not top" and go back to `cond_wait`.
6. The winner pops itself from the heap, sets `available = 0`, unlocks, returns.

### "What if a coder is top of the heap but the dongle is in cooldown?"

`wait_choice` notices that case: available = 1 AND I'm top AND `now < last_release + cooldown`. It uses `pthread_cond_timedwait` with an absolute timeout = end of cooldown. The thread wakes exactly when the cooldown ends and tries `try_take` again.

### "How is the burnout detected and printed in time?"

A dedicated monitor thread polls every 500 µs. For each coder, it reads `last_compile` (under that coder's mutex) and compares `now - last_compile` to `t_burnout`. As soon as it's over, it:

1. Sets `sim->stop = 1` under `sim_mtx`.
2. Calls `force_log("burned out")` — `force_log` skips the stop check so the line is always printed.
3. Calls `wake_all` to broadcast every condition variable so no coder is stuck inside `cond_wait`.

Polling every 500 µs is well below the 10 ms tolerance.

### "How do you prevent two log lines from interleaving?"

Every log call holds `print_mtx` around the `printf`. Two coders calling `log_state` at the same time serialize on that mutex.

### "What's the order in which you lock mutexes? Could there be a mutex-level deadlock?"

The disciplines are:

- `print_mtx` is always taken **before** `sim_mtx`.
- `sim_mtx` is taken **before** any `coder.mtx` (only happens in `start_simulation`).
- Dongles are taken in **ascending id order** (resource hierarchy).

No code path reverses any of these orderings, so the locks form a DAG, not a cycle — mutex deadlock is impossible.

### "Why do you keep `started` and `mon_started` in the sim struct?"

So that if `pthread_create` fails halfway, `join_all` knows to join only the threads that actually started. Otherwise we'd `pthread_join` on uninitialized handles, which is undefined behavior.

### "What happens if there's only 1 coder?"

There's only 1 dongle. `assign_neighbors` sets `low = high = dongle[0]`. The coder takes `low` successfully, then tries to take `high` — same dongle, already unavailable — and blocks. The monitor eventually detects burnout and prints the line. So the 1-coder case always burns out. That matches the subject (a single coder cannot get *two* dongles).

### "Why a heap if it only has 2 elements?"

The subject explicitly requires a priority queue you wrote yourself. The heap is implemented as a general binary heap that happens to be sized for 2. Same algorithm works for any size if you bump the array dimension.

### "What memory do you allocate, and how do you make sure nothing leaks?"

Three `malloc`s: `sim->dongles`, `sim->coders`, `sim->threads`. All three are freed in `sim_destroy`, which is called in every exit path of `main`. Run `valgrind --leak-check=full ./codexion ...` to verify.

---

## 7. Common traps & how to debug

### "My program hangs"

Probably a deadlock — a `pthread_cond_wait` no one ever signals. Common causes:

- You set `stop = 1` but forgot to `broadcast` the condition variables. Coders are stuck. Always call `wake_all` after setting stop.
- You took two locks in different orders. Check the lock-order section in this guide.

Debug tool: compile with `-g`, run with `gdb`, then `thread apply all bt` when it hangs.

### "Coders burn out way too early"

If you see a burnout at timestamp 0 or 1, you forgot to stamp `last_compile = sim->start` before broadcasting `go`. The monitor reads `last_compile = 0` and decides everyone is way past the deadline. The fix is `stamp_starts(sim)` in `main.c`.

If you see a burnout right at the edge of the deadline (e.g. `400 X burned out` with `t_burnout = 400`) and it happens randomly across runs of the same parameters, you're hitting the classic wait-chain problem: every coder grabs their low at T=0 and then queues up serially on each others' highs. The fix is the half-compile-time `stagger_start` on even-id (and last) coders in `coder.c`.

### "Two log lines appear on the same physical line"

You're either printing without `print_mtx` (search every `printf`) or `printf` flushed across a newline. Make sure every `printf` ends with `\n` and is wrapped in `print_mtx`.

### "Compiler complains about `_POSIX_C_SOURCE` or `struct timespec`"

`-pthread` and the existing includes are enough on Linux. On macOS or weird Linuxes you may need `-D_POSIX_C_SOURCE=200809L`. Add it to `CFLAGS` if needed.

### "Norminette complains"

Most likely:

- A function over 25 lines → extract a helper.
- A variable declared and assigned on the same line → split.
- More than 5 local variables → group some into a struct or extract a helper.
- A line over 80 chars → wrap.

The codebase was written with those rules in mind, but tighten anything that fails when you run `norminette *.c *.h`.

### "Helgrind / TSan reports a race"

Either a missing mutex around a shared read/write, or you released a mutex too early. Re-walk the critical section. The most common offender is reading `c->last_compile` without taking `c->mtx`.

---

That's everything. Read the guide once end-to-end before the defense; you'll find that every "why" the evaluator can ask is already answered here. Good luck.
