# M151M Project 3 (v2): Out-of-Order RV32I Processor

This repository contains a cycle-level out-of-order RISC-V (RV32I) simulator written in C++.

The simulator includes:
- Out-of-order pipeline structures (`RS`, `ROB`, `RAT`, `LSQ`)
- Functional units (`ALU`, `BRU`, `LSU`, `SFU`)
- Split instruction/data caches
- Backing memory model
- ISA tests and benchmark programs

## Repository layout

- `src/` main implementation
- `common/` shared utilities/simulator infrastructure
- `tests/` ISA test binaries and test Makefile
- `benchmarks/` benchmark binaries

## Build

From project root:

    make -j4

Debug build (enables trace via `DEBUG_LEVEL`):

    make clean
    make DEBUG=3 -j4

## Run

Run one program with stats:

    ./tinyrv -s tests/rv32ui-p-sub.hex

Run benchmark:

    ./tinyrv -s benchmarks/towers.hex

## Test

Run full ISA test suite:

    make tests

If successful, each tests/program ends with:

- `PASSED!`

Run full ISA benchmarks:

    make benchmarks

If successful, each benchmarks/program ends with:

- `PASSED!`



## Performance stats (`-s`)

The simulator prints:
- overall `instrs` and `cycles`
- LSQ stats (loads/stores/forwarding/alias stalls)
- I-cache and D-cache hit-rate summaries
- branch predictor summary (if enabled)

## Cache configuration

Caches are constructed with explicit parameters:
- capacity (bytes)
- associativity (`num_ways`)
- replacement policy (`LRU` / `FIFO`)

Current defaults are passed from `Core` using values from `config.h`.

## Development notes

- Use Linux (Ubuntu 18.04+ recommended).
- The project builds with modern C++ toolchains (GNU g++).
- Keep changes focused, readable, and educational.

## Guidelines
* Only modify following files with **TODO** entries: ooo.cpp, LSQ.cpp, ROB.cpp, RS.h, and cache_repl.h.
We will replace the other files during evaluation.
* Do not remove an existing file from the project.
* Do not change the Makefile. Do not add a new file. 

## Grading
* simulator compiles successfully: 1 pts
* Out-of-order processor execute successfully: 9 pts.
* Your cache replacement policy execute successfully: 5 pts. See the *Cache Replacement* section for details.

Based on the coverage of test suites, you will get partial scores.

***Please do not procrastinate.***

## Cache Replacement
* A default LRU policy is provided in cache_repl.h.
* To recieve credits for cache, you need to implement your own cache replacement policy. If you choose to do that, implements class *MyReplPolicy* in cache_repl.h and then uncomment the line #define MyReplPolicy LRUReplPolicy.
* Do not modify any other files.

## Extra Credit
* Extra credit will be provided if your cache replacement policy outperforms the default LRU policy on average for the required test cases.
* Do not modify any other files.
* A leaderboard is also available on Gradescope. Good luck on getting the highest cache performance!

## References

- RISC-V ISA Spec: https://riscv.org/technical/specifications/
- Class materials
