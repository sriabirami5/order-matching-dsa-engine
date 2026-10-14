#ifndef ENGINE_H
#define ENGINE_H

#include "structures.h"
#include <stdio.h>
#include <stdbool.h>

/* ========================================================================= */
/* ENGINE CONFIGURATION & CONSTANTS                                          */
/* ========================================================================= */
#define TRADES_LOG_FILENAME     "trades.log"
#define ORDERS_FEED_FILENAME    "orders_feed.txt"

/* Order Book Performance & Engine Statistics */
typedef struct EngineStats {
    long total_orders_processed;
    long successful_orders;
    long rejected_orders;
    long total_trades_executed;
    long partially_filled_orders;
    long total_canceled_orders;
    long total_expired_orders;
    long total_volume_traded;
    double elapsed_time_seconds;
} EngineStats;

/* The Central Order Book */
typedef struct OrderBook {
    BTree *buy_tree;                /* B-Tree indexing BUY price levels (Bids) */
    BTree *sell_tree;               /* B-Tree indexing SELL price levels (Asks)*/
    OrderLookupTable *lookup_table; /* Hash table for fast O(1) order lookup   */
    LeftistHeap *expiration_heap;   /* Min-heap tracking order TTL expirations */
    long arrival_sequence;          /* Monotonically increasing sequence ID    */
    long current_trade_id;          /* Sequential trade counter                */
    EngineStats stats;              /* Engine performance and counter stats    */
    FILE *trade_log_file;           /* File pointer for logging trades         */
} OrderBook;

/* ========================================================================= */
/* ENGINE LIFECYCLE & CORE APIS                                              */
/* ========================================================================= */
OrderBook* orderbook_create(int btree_degree);
void orderbook_destroy(OrderBook *book);

/* Order submission and matching */
int orderbook_submit_order(OrderBook *book, long id, OrderType type,
                           long price, long qty, long long ttl_ms);

/* Order cancellation and expiration */
int orderbook_cancel_order(OrderBook *book, long order_id);
void orderbook_process_expirations(OrderBook *book);

/* Order book visualization */
void orderbook_display_bids(OrderBook *book, int detailed);
void orderbook_display_asks(OrderBook *book, int detailed);
void orderbook_display_stats(OrderBook *book);
void orderbook_display_trade_history(void);

/* Feed processing and full simulation */
int process_order_feed(OrderBook *book, const char *filename);
int run_full_simulation(OrderBook *book, const char *filename, int progress_interval);

/* Simulated order feed generator (implemented in data_gen.c) */
int generate_order_feed(const char *filename);

/* Self-test suites for matching engine (Stage 4) and expiration/cancellation (Stage 5 & 6) */
int matching_engine_run_self_test(void);
int expiration_run_self_test(void);

/* Monotonic time helper (Windows & POSIX compatible) */
long long get_current_time_ms(void);

#endif /* ENGINE_H */
