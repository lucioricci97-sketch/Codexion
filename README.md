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

`n_coders` and `n_required` must be positive integers. Timing values are expressed in milliseconds and may be zero. `scheduler` must be either `fifo` or `edf`.

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

The resource hierarchy prevents deadlocks but does not, by itself, guarantee fair access to the dongles. To ensure deterministic arbitration, every dongle owns a small binary min-heap (maximum size 2, since only its two neighboring coders can ever compete for it).

When a coder wants to compile, it **does not register on both dongles immediately**. Instead, the acquisition is sequential:

1. Register on the lower-numbered dongle's queue.
2. Wait until that dongle is acquired.
3. Register on the higher-numbered dongle's queue.
4. Wait until the second dongle is acquired.

This design prevents a coder from occupying the front of two queues simultaneously while still waiting for the first resource, allowing other coders to continue making progress whenever possible.

The priority key depends on the selected scheduler:

- **FIFO:** the key is the timestamp at which the request is registered. Earlier requests have higher priority.
- **EDF:** the key is the coder's burnout deadline:

```text
deadline = last_compile + time_to_burnout
```

The coder with the earliest deadline receives the highest priority.

Whenever two requests have identical keys (a common situation immediately after the simulation starts), the heap uses the lower coder ID as a deterministic tie-breaker. This guarantees reproducible scheduling even in edge cases.

### Cooldown handling

After a dongle is released, it cannot be re-acquired until `dongle_cooldown` milliseconds have elapsed. The dongle records its `last_release` timestamp. A waiter who is top of the heap but inside the cooldown window does not busy-wait: it computes the absolute end of the cooldown and uses `pthread_cond_timedwait` to sleep precisely until then.

### Precise burnout detection

A dedicated **monitor thread** polls every coder roughly every 500 microseconds, comparing `now - last_compile_start` against `time_to_burnout`. When a burnout is detected it:

1. Sets the global `stop` flag (under `sim_mtx`).
2. Prints the `<ts> <id> burned out` message immediately, bypassing the normal "is the simulation still running" check.
3. Broadcasts every condition variable so that no coder is left blocked inside `pthread_cond_wait`.

The 500-microsecond poll keeps detection well under the 10 ms requirement.

### Log serialization

All output is serialized through a dedicated `print_mtx` mutex so that log messages never interleave.

Before printing a normal state message, `log_state()` briefly locks `sim_mtx` while still holding `print_mtx` to read the global `stop` flag. If the simulation has already ended, the function aborts without printing anything. This guarantees that no normal state (`is compiling`, `is debugging`, etc.) can appear after a burnout has been reported.

The monitor thread uses a dedicated `force_log()` function that intentionally bypasses this stop check, ensuring that the mandatory `burned out` message is always printed exactly once.
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

Immediately after creation, every coder thread increments the shared `ready` counter and waits on `sim.sim_cond`. The monitor thread also waits on the same condition variable, although it does not contribute to the `ready` count.

The main thread waits until every coder has reached this synchronization point. Only then does it:

1. Record the official simulation start timestamp.
2. Initialize every coder's `last_compile` timestamp to that same value.
3. Set `go = 1`.
4. Broadcast `sim.sim_cond`.

This guarantees that every thread begins executing from the same reference time and prevents the monitor from detecting an immediate burnout before the simulation has officially started.

### Stop signaling

When the simulation must end (burnout or all coders reached `n_required` compiles), the monitor:

1. Sets `stop = 1` under `sim_mtx`.
2. Broadcasts the global `sim_cond` and every per-dongle `dongle.cond` through `wake_all()`.

Any coder blocked in `pthread_cond_wait` wakes up, sees the stop flag, and unwinds cleanly — no thread is ever left stuck, no memory is leaked.
