# ⚡ C-BTree Exchange Engine

> **A High-Frequency Order Matching Engine built from scratch in pure C11 using custom Data Structures and Algorithms (DSA).**

---

## 📌 Table of Contents
- [Overview](#-overview)
- [Key Features](#-key-features)
- [Data Structures & Architecture](#-data-structures--architecture)
  - [1. B-Tree (Price Indexing)](#1-b-tree-price-indexing)
  - [2. FIFO Queue (Price-Time Priority)](#2-fifo-queue-price-time-priority)
  - [3. Leftist Min-Heap (Order Expiration / TTL)](#3-leftist-min-heap-order-expiration--ttl)
  - [4. Hash Table (O(1) Cancellation & Lookup)](#4-hash-table-o1-cancellation--lookup)
- [Project File Structure](#-project-file-structure)
- [How Order Matching Works](#-how-order-matching-works)
- [Build & Run](#-build--run)
  - [Linux / macOS / WSL (Ubuntu)](#linux--macos--wsl-ubuntu)
  - [Windows (MSYS2 / MinGW)](#windows-msys2--mingw)
- [Automated DSA Verification Suites](#-automated-dsa-verification-suites)
- [Interactive CLI Walkthrough](#-interactive-cli-walkthrough)
- [College Viva & Interview FAQ](#-college-viva--interview-faq)

---

## 📖 Overview

The **C-BTree Exchange Engine** is an educational, production-modeled limit order book and continuous matching engine. It demonstrates how modern financial exchanges (such as NASDAQ or the London Stock Exchange) match buy and sell orders at sub-millisecond latencies using manual, pointer-level data structures without third-party libraries.

### Core Invariants Maintained:
- **Price-Time Priority (FIFO)**: The most aggressive price executes first. For orders sharing identical prices, earlier arrivals execute first.
- **Fixed-Point Integer Pricing**: Prices are stored in integer ticks (e.g., `6205000` = \$62.05000) to eliminate floating-point rounding errors.
- **Zero External Dependencies**: Pure standard C11 (`malloc`, `free`, pointers, structs).

---

## ✨ Key Features

- **Blazing Fast Matching**: Matches crossing limit orders continuously in $O(\log N)$ time.
- **Resting Price Execution**: Incoming market-taker orders execute at the resting maker's price (price improvement).
- **Automated Book Pruning**: Price levels are created on-the-fly and automatically pruned from the B-Tree when exhausted.
- **TTL Expiration Handling**: Proactive order eviction based on real-time monotonic timestamps using a Leftist Min-Heap.
- **Instant Cancellation**: Immediate $O(1)$ order cancellation and queue extraction via hash table indexing.
- **Interactive CLI & Feed Simulation**: Switch between batch-processing simulated streams or manually submitting live orders.

---

## 🏛 Data Structures & Architecture

```
                  ┌──────────────────────────────────────────────┐
                  │                 OrderBook                    │
                  └──────┬──────────────────┬──────────────┬─────┘
                         │                  │              │
           ┌─────────────┴────────┐  ┌──────┴──────┐  ┌────┴───────────────┐
           │   BUY B-Tree (Bids)  │  │ SELL B-Tree │  │ OrderLookupTable   │
           │  (Highest Price 1st) │  │(Lowest 1st) │  │  (O(1) Hash Table) │
           └─────────────┬────────┘  └──────┬──────┘  └────────────────────┘
                         │                  │
                         ▼                  ▼
                  ┌──────────────┐   ┌──────────────┐
                  │  PriceLevel  │   │  PriceLevel  │
                  ├──────────────┤   ├──────────────┤
                  │ Head ──► [Order 1] ──► [Order 2] (FIFO)
                  │ Tail ──────────────────────┘
                  └──────────────┘
                         │
                         ▼
                  ┌──────────────────────────────┐
                  │   LeftistHeap (Min-Heap)     │
                  │   Earliest Expiration (TTL)  │
                  └──────────────────────────────┘
```

### 1. B-Tree (Price Indexing)
- **Why a B-Tree?** Unlike binary search trees that can degenerate into linked lists, B-Trees guarantee balanced $O(\log N)$ search, insertion, and deletion.
- **Implementation Details**:
  - Dynamically allocated arrays for keys, levels, and child pointers sized according to minimum degree $t$ ($2t - 1$ keys, $2t$ children).
  - Proactive child splitting (`btree_split_child`) during insertion to eliminate recursive back-tracking.
  - Sibling borrowing, merging, and root contraction during deletion.
  - **Bids (BUY)** are traversed in **descending order** (best buyer first).
  - **Asks (SELL)** are traversed in **ascending order** (best seller first).

### 2. FIFO Queue (Price-Time Priority)
- Each unique price in the B-Tree points to a `PriceLevel` containing a singly-linked list of resting orders.
- Maintained with `head` and `tail` pointers for $O(1)$ append (`fifo_enqueue`) and $O(1)$ match consumption (`fifo_dequeue`).
- Supports random unlinking by order ID for instantaneous cancellations.

### 3. Leftist Min-Heap (Order Expiration / TTL)
- Orders with an expiration timestamp are registered into a **Leftist Min-Heap**.
- **Null Path Length (NPL)**: The shortest distance to a leaf node satisfies $\text{npl}(\text{left}) \ge \text{npl}(\text{right})$.
- All operations (insert, pop) reduce to a single elegant recursive function: **`heap_merge`**.
- Features **Lazy Invalidation**: If an order was already filled or canceled before its TTL expired, stale heap entries are discarded safely without errors.

### 4. Hash Table (O(1) Cancellation & Lookup)
- Separate-chaining hash table mapping `order_id` $\to$ (`Order*`, `PriceLevel*`).
- Allows immediate lookup of active orders without traversing the B-Trees.

---

## 📂 Project File Structure

```text
C_BTree_Exchange/
├── main.c           # Main processing loop, CLI menu (options 0-10), and signal cleanup
├── structures.h     # Struct declarations for B-Tree, PriceLevel, LeftistHeap, Order
├── structures.c     # Implementations of B-Tree, FIFO list, Leftist Heap, and Hash Table
├── engine.h         # Order book definitions and matching engine prototypes
├── engine.c         # Matching algorithms, cancellation, expiration, and statistics
├── data_gen.c       # Configurable simulated order feed generator
├── orders_feed.txt  # Input order feed stream (OrderID,Type,Price,Quantity,TTL)
└── trades.log       # Executed transaction audit log
```

---

## ⚙ How Order Matching Works

1. **Validation**: Check for positive price, quantity, and duplicate order IDs.
2. **Proactive Expiration**: The Leftist Heap checks if any resting orders have expired based on current monotonic clock time.
3. **Crossing Condition**:
   - For an incoming **BUY**: Checks lowest available Ask. If $\text{BuyPrice} \ge \text{BestAskPrice}$, trade executes.
   - For an incoming **SELL**: Checks highest available Bid. If $\text{SellPrice} \le \text{BestBidPrice}$, trade executes.
4. **Execution**:
   - Executes at the **resting order's price**.
   - Traded volume: $\min(\text{incoming\_qty}, \text{resting\_qty})$.
   - Deducts traded quantity from FIFO queue and updates level aggregate volume.
   - Writes executed transaction to `trades.log`.
5. **Pruning & Resting**:
   - If a resting order is filled, it is freed. If an entire price level is emptied, it is pruned from the B-Tree.
   - If the incoming order has remaining quantity after exhausting crossing levels, the remainder is placed onto its book.

---

## 🚀 Build & Run

### Linux / macOS / WSL (Ubuntu)

```bash
# Clone the repository
git clone https://github.com/sriabirami5/order-matching-dsa-engine.git
cd order-matching-dsa-engine/C_BTree_Exchange

# Compile with strict C11 standard and optimization
gcc -std=c11 -Wall -Wextra -pedantic -O2 main.c structures.c engine.c data_gen.c -o exchange

# Run interactive exchange
./exchange
```

### Windows (MSYS2 / MinGW)

```powershell
cd C_BTree_Exchange
gcc -std=c11 -Wall -Wextra -pedantic -O2 main.c structures.c engine.c data_gen.c -o exchange.exe
.\exchange.exe
```

---

## 🧪 Automated DSA Verification Suites

The codebase includes self-testing verification suites that can be run directly from the command line:

```bash
# Run all verification suites
./exchange --test-all
```

Or test individual modules:
- `./exchange --test-btree` : Verifies node splitting, traversals, min/max search, and deletion.
- `./exchange --test-fifo` : Verifies strict FIFO ordering, partial reduction, and $O(1)$ extraction.
- `./exchange --test-heap` : Verifies min-heap merging, NPL property, and timestamp eviction.
- `./exchange --test-matching` : Verifies crossing execution, price improvement, and remainder insertion.
- `./exchange --test-expiration` : Verifies monotonic clock expiration and cancellation.

---

## 🖥 Interactive CLI Walkthrough

```text
========================================
       C-BTREE EXCHANGE ENGINE          
       Order Matching Simulator         
========================================
  1. Generate Order Feed
  2. Process Order Feed
  3. Submit Order Manually
  4. View BUY Order Book
  5. View SELL Order Book
  6. Search Order by ID
  7. Cancel an Order
  8. View Trade History
  9. View Market Statistics
 10. Run Full Simulation
  0. Exit
========================================
Enter your choice:
```

### Quick Live Demonstration Steps:
1. **Submit a resting SELL order**: Select `3`, enter `SELL`, Price `6205000`, Qty `10`.
2. **Submit a crossing BUY order**: Select `3`, enter `BUY`, Price `6205000`, Qty `4`.
3. **Watch the live trade output**:
   ```text
   [TRADE] BUY #5002 matched SELL #5001 | Price: 6205000 | Qty: 4
   [INFO] Order #5001 partially filled (Remaining: 6)
   ```
4. **Inspect the Order Book**: Select `5` to view the updated SELL book showing `6` units remaining.
5. **View Trades**: Select `8` to view the formatted execution audit log.

---

## 🎓 College Viva & Interview FAQ

<details>
<summary><b>1. Why use a B-Tree instead of a Red-Black Tree or AVL Tree?</b></summary>
While Red-Black trees and AVL trees provide $O(\log N)$ operations, each node contains only a single element, resulting in frequent memory allocations, deep pointer chasing, and poor CPU cache locality. A B-Tree contains multiple keys per node ($2t - 1$), resulting in a shallower tree height, fewer cache misses, and predictable, proactive node splitting.
</details>

<details>
<summary><b>2. Why use a Leftist Heap instead of a standard Binary Heap for order expiration?</b></summary>
A binary heap implemented over a flat array cannot be merged efficiently ($O(N)$ merge). A Leftist Heap is a pointer-based tree structure that supports merging in $O(\log N)$ time. In an exchange, merging expiration heaps from different market segments or sub-books is extremely fast.
</details>

<details>
<summary><b>3. What is the Null Path Length (NPL) in a Leftist Heap?</b></summary>
The Null Path Length of node $X$ is the length of the shortest path from $X$ to a node with fewer than two children. The Leftist property guarantees that $\text{npl}(\text{left}) \ge \text{npl}(\text{right})$, ensuring that the shortest path to an empty child is always down the right subtree.
</details>

<details>
<summary><b>4. How is duplicate order ID submission handled?</b></summary>
When an order is submitted, the engine queries the separate-chaining Hash Table (`lookup_find`). If the ID already exists and is active, the order is rejected immediately with a `[WARN] Duplicate Order ID` message without corrupting book state.
</details>

<details>
<summary><b>5. How is memory managed when an order is partially filled vs fully filled?</b></summary>
When partially filled, the order remains in the FIFO queue and its `remaining_qty` is decremented in-place along with `level->total_volume`. When `remaining_qty == 0`, the order is dequeued, unlinked from the hash table, and freed via `order_destroy()`. If the price level order count reaches 0, the price key is pruned from the B-Tree.
</details>

---

## 📜 License
This project is open-source and intended for educational and academic use.
