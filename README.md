*This project has been created as part of the 42 curriculum by luricci.*

# Codexion

## Description

Codexion is a multithreaded simulation inspired by the Dining Philosophers problem. A number of *coders* sit around a circular co-working hub, sharing a limited pool of *USB dongles*. A coder needs **two** dongles plugged in at once (one in each hand) to compile their quantum code. After compiling they debug, then refactor, then loop again.

There is exactly one dongle between each pair of coders, so each coder shares one dongle with their left neighbor and one with their right neighbor. If a coder fails to start a new compile within `time_to_burnout` milliseconds, they burn out and the simulation stops.

The project is a hands-on exercise in:

- **POSIX threads** (`pthread_create`, `pthread_join`)
- **Mutexes** (`pthread_mutex_t`) to protect shared state
- **Condition variables** (`pthread_cond_t`) to make threads wait without burning CPU
- **Fair scheduling** of a contested resource (FIFO or EDF)
- **Deadlock avoidance** in a classic dining-philosophers setup

## Instructions

### Compilation

```bash
make
```

The Makefile compiles every `.c` file with `-Wall -Wextra -Werror -pthread` and links into the `codexion` executable. Other rules: `clean`, `fclean`, `re`.

### Execution

```
./codexion <n_coders> <t_burnout> <t_compile> <t_debug> <t_refactor> <n_required> <t_cooldown> <scheduler>
```

All values except `scheduler` are positive integers. Times are in milliseconds. `scheduler` is exactly `fifo` or `edf`.

**Examples**

```bash
# 5 coders, FIFO scheduling — should not burn out
./codexion 5 800 200 200 200 10 50 fifo

# 5 coders, EDF scheduling
./codexion 5 800 200 200 200 10 50 edf

# 1 coder — will always burn out (cannot get two dongles)
./codexion 1 800 200 200 200 10 50 fifo
```

Log format (timestamps in ms relative to simulation start):

```
0 1 has taken a dongle
1 1 has taken a dongle
1 1 is compiling
201 1 is debugging
401 1 is refactoring
...
```

## Resources

**Classic references**

- `man pthread_create`, `man pthread_mutex_init`, `man pthread_cond_wait`, `man pthread_cond_timedwait`, `man gettimeofday`
- *The Linux Programming Interface*, Michael Kerrisk — chapters on POSIX threads and synchronization
- *Operating System Concepts*, Silberschatz et al. — Dining Philosophers, deadlocks, classical synchronization problems
- [Dining Philosophers problem](https://en.wikipedia.org/wiki/Dining_philosophers_problem)
- [Min-heap (binary heap) data structure](https://en.wikipedia.org/wiki/Binary_heap)

**AI usage**

AI was used as a study and review aid, not as a code generator. Specifically:

- To clarify the semantics of `pthread_cond_timedwait` and the way the absolute `struct timespec` argument must be built from `gettimeofday`.
- To review the design decisions for deadlock avoidance (resource hierarchy vs. try-and-release) and explain the tradeoffs.
- To walk through every concept (mutexes, condition variables, the heap data structure, the FIFO/EDF arbitration policies) before any code was written, so that each line of the final implementation is something I can explain and defend during evaluation.

All code in this repository was written by hand after that review.

---

## Blocking cases handled

### Deadlock prevention (Coffman's four conditions)

A deadlock requires all four of these conditions to hold at once:

1. **Mutual exclusion** — each dongle can only be held by one coder at a time.
2. **Hold and wait** — a coder may hold one dongle while waiting for another.
3. **No preemption** — a dongle cannot be forcibly taken from a coder.
4. **Circular wait** — there is a cycle of coders each waiting for the next one's dongle.

Conditions (1)–(3) are intrinsic to the problem and cannot be removed. This project breaks the **circular wait** condition using a **resource hierarchy**: each coder always tries to acquire the lower-numbered dongle first and the higher-numbered dongle second (see `init.c`, `assign_neighbors`). This is the classic fix for the Dining Philosophers problem and makes a circular dependency cycle impossible.

### Starvation prevention

The classic resource hierarchy alone is deadlock-free but not necessarily *fair* — a busy coder could permanently lock out a neighbor. The subject therefore requires explicit scheduler arbitration. Each dongle owns its own small priority queue (a binary min-heap of size at most 2, since only two neighbors ever compete for the same dongle). When a coder wants both dongles, they register a request on each heap *before* attempting to take anything:

- Under `fifo`, the key is the timestamp of the request (older request wins).
- Under `edf`, the key is the burnout deadline `last_compile_start + time_to_burnout` (the most endangered coder wins).

When two requests carry the same key (the common case at T=0, where every coder's `last_compile` is the simulation start), the heap falls back to a deterministic tie-breaker: the lower coder `id` wins. This satisfies the subject's requirement of a fully deterministic EDF policy even in edge cases.

A waiting coder only acquires a dongle when it is at the top of the heap, so no coder can be indefinitely overtaken under EDF as long as the parameters are feasible.

### Cooldown handling

After a dongle is released, it cannot be re-acquired until `dongle_cooldown` milliseconds have elapsed. The dongle records its `last_release` timestamp. A waiter who is top of the heap but inside the cooldown window does not busy-wait: it computes the absolute end of the cooldown and uses `pthread_cond_timedwait` to sleep precisely until then.

### Precise burnout detection

A dedicated **monitor thread** polls every coder roughly every 500 microseconds, comparing `now - last_compile_start` against `time_to_burnout`. When a burnout is detected it:

1. Sets the global `stop` flag (under `sim_mtx`).
2. Prints the `<ts> <id> burned out` message immediately, bypassing the normal "is the simulation still running" check.
3. Broadcasts every condition variable so that no coder is left blocked inside `pthread_cond_wait`.

The 500-microsecond poll keeps detection well under the 10 ms requirement.

### Log serialization

A single `print_mtx` mutex is held around the `printf` call. While that mutex is held the code also reads the `stop` flag, so that no state line (`is compiling`, `is debugging`, ...) can sneak out after a `burned out` line. The monitor uses a separate `force_log` path that ignores the `stop` flag, since the burnout message itself must always be printed.

---

## Thread synchronization mechanisms

### Primitives used

- `pthread_mutex_t` — protects every piece of shared state:
  - `dongle.mtx` guards `available`, `last_release`, and the per-dongle heap.
  - `coder.mtx` guards `last_compile` and `compiles`.
  - `sim.sim_mtx` guards `stop`, `ready`, and `go`.
  - `sim.print_mtx` serializes output lines.
- `pthread_cond_t` — replaces busy-waiting with sleep/wake:
  - One per dongle (`dongle.cond`) — signaled on release or new request.
  - One global (`sim.sim_cond`) — used as a start-of-simulation barrier and to wake everyone on stop.
- `pthread_cond_timedwait` — used when a coder is top of a dongle's heap but inside the cooldown window. Sleeps exactly until cooldown end (or until another signal arrives).

### How race conditions are prevented

Every shared variable has a clearly assigned mutex; that mutex is acquired before every read and every write. For example:

- A coder updates `last_compile` only while holding its own `coder.mtx`; the monitor reads it under the same mutex. No torn reads of a 64-bit timestamp.
- A coder updates the dongle's `available` / `last_release` / heap only while holding the `dongle.mtx`. Two coders trying to take the same dongle can never both succeed.

### Lock-order discipline

Any code path that needs multiple mutexes always takes them in a fixed order:

1. `print_mtx` is always taken before `sim_mtx`.
2. `sim_mtx` is always taken before any `coder.mtx`.
3. Dongles are acquired in ascending id order (the resource hierarchy described above).

Because every path respects this order, no inversion is possible, so no mutex-level deadlock can occur.

### Start barrier

Right after creation, each coder thread and the monitor thread block on `sim.sim_cond` until the main thread signals `go = 1`. The main thread waits until every coder has reported `ready`, then stamps every `last_compile` to the simulation start time and broadcasts the start signal. This guarantees that no coder is judged for burnout before time 0.

### Stop signaling

When the simulation must end (burnout or all coders reached `n_required` compiles), the monitor:

1. Sets `stop = 1` under `sim_mtx`.
2. Broadcasts `sim_cond` and every `dongle.cond` (`wake_all` in `monitor.c`).

Any coder blocked in `pthread_cond_wait` wakes up, sees the stop flag, and unwinds cleanly — no thread is ever left stuck, no memory is leaked.
