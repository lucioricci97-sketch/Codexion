# Codexion — Technical Guide

## 1. Project Overview

Codexion is a multithreaded concurrency simulation modeling resource contention between worker threads (coders) competing for shared resources (dongles).

The system evaluates:

- deadlock prevention
- starvation mitigation
- scheduling correctness (FIFO / EDF)
- synchronization under strict timing constraints
- deterministic execution behavior

---

## 2. Global Architecture

The system is composed of:

- **Coder threads (N)**: active workers executing tasks
- **Dongles (N)**: shared resources between adjacent coders
- **Monitor thread**: detects termination conditions
- **Main controller**: initializes and synchronizes execution

Topology: circular graph  
Each dongle is shared between exactly two coders.

---

## 3. Thread Lifecycle

Each coder executes the following loop:

1. Request first dongle (lower index)
2. Acquire first dongle
3. Request second dongle (higher index)
4. Acquire second dongle
5. Execute workload (compile phase)
6. Release both dongles
7. Enter cooldown phase
8. Repeat until stop condition

Invariant:
- Acquisition order is always increasing index → prevents circular wait

---

## 4. Synchronization Model

### 4.1 Start Barrier

All threads synchronize before execution begins.

Process:
1. Each coder increments `ready`
2. Threads block on global condition variable
3. Main thread waits until `ready == n_coders`
4. Main thread initializes timestamps
5. Broadcast releases all threads simultaneously

Guarantee:
- all `last_compile` start from identical reference time

---

### 4.2 Shutdown Model

Termination occurs when:

- a coder burns out, OR
- all coders complete required cycles

Mechanism:
- `sim->stop` set under `sim_mtx`
- all condition variables broadcast
- all blocked threads released

---

## 5. Scheduling System

### 5.1 FIFO

Priority key:
- request timestamp

Guarantee:
- first-come-first-served ordering per dongle

---

### 5.2 EDF (Earliest Deadline First)

Priority key:

```c
deadline = last_compile + t_burnout
```

Properties:

- dynamic priority evolution
- coders near burnout gain priority
- applied locally per dongle heap (not global scheduler)

Note:
- starvation is mitigated, not strictly eliminated

---

## 6. Resource Arbitration (Dongles)

Each dongle maintains a binary min-heap.

Heap properties:

- stores (coder_id, priority_key)
- ordered by:
  1. priority key (min first)
  2. coder_id (tie-breaker)

Constraint:
- heap size ≤ 2 (due to topology)
- implementation remains generic

---

## 7. Deadlock Prevention

Deadlock is prevented via:

### Strict ordering rule
- all coders acquire dongles in increasing index order

This removes:
- circular wait condition (Coffman condition violation)

Guarantee:
- deadlock-free execution

---

## 8. Timing System

### 8.1 Time Model

- timestamps in microseconds
- burnout evaluated via absolute time difference

---

### 8.2 Sleep Model

- implemented using interruptible polling sleep
- resolution ~500µs

Purpose:
- allow fast stop response
- avoid blocking shutdown threads

---

## 9. Burnout Detection

A coder burns out if:

```c
now >= last_compile + t_burnout
```

On detection:

- `sim->stop = 1` (protected by mutex)
- forced log emitted
- all threads woken

---

## 10. Cooldown System

After each execution cycle:

- coder enters cooldown phase
- implemented via timed wait

Purpose:
- reduces contention spikes
- stabilizes scheduling fairness

---

## 11. Logging System

All logs are protected by `print_mtx`.

Rules:

- no interleaved output
- no logs after termination (except forced monitor logs)

### Logging behavior:

- `log_state()`:
  - checks `sim->stop`
  - suppresses output after termination

- `force_log()`:
  - bypasses stop check
  - used by monitor only

Lock discipline:
- consistent ordering prevents inversion

---

## 12. Sequential Acquisition Optimization

Coders do NOT acquire both resources simultaneously.

Process:

- acquire first dongle
- only then request second dongle

Benefits:

- avoids dual queue blocking
- improves throughput
- reduces contention stalls

---

## 13. Staggered Startup

Even-indexed coders are delayed at start.

Effect:

- reduces initial contention burst
- stabilizes early scheduling behavior

---

## 14. Correctness Guarantees

The system guarantees:

### Safety
- no deadlocks (ordering rule)

### Liveness
- threads always progress if resources available

### Determinism
- heap tie-breaking ensures reproducible order

### Bounded detection
- burnout detected within timing constraints

---

## 15. Design Summary

Codexion is designed to ensure:

- deterministic concurrency behavior
- predictable scheduling under load
- safe termination under all conditions
- fair resource arbitration
- strict synchronization correctness