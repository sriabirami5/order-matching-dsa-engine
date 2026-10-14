#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <time.h>
#endif

/* ========================================================================= */
/* MONOTONIC CLOCK HELPER (MILLISECONDS)                                     */
/* ========================================================================= */
long long get_current_time_ms(void) {
#if defined(_WIN32) || defined(_WIN64)
    return (long long)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return ((long long)ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000LL);
    }
    return (long long)(time(NULL) * 1000LL);
#endif
}

/* ========================================================================= */
/* ORDER BOOK LIFECYCLE                                                      */
/* ========================================================================= */
OrderBook* orderbook_create(int btree_degree) {
    OrderBook *book = (OrderBook*)malloc(sizeof(OrderBook));
    if (!book) {
        fprintf(stderr, "Error: Failed to allocate OrderBook.\n");
        return NULL;
    }

    book->buy_tree = btree_create(btree_degree);
    book->sell_tree = btree_create(btree_degree);
    book->lookup_table = lookup_create(HASH_TABLE_CAPACITY);
    book->expiration_heap = heap_create();
    book->arrival_sequence = 0;
    book->current_trade_id = 0;

    memset(&book->stats, 0, sizeof(EngineStats));

    /* Open trades log for writing executed transactions for this session */
    book->trade_log_file = fopen(TRADES_LOG_FILENAME, "w");
    if (!book->trade_log_file) {
        fprintf(stderr, "[WARN] Could not open %s for recording trades.\n", TRADES_LOG_FILENAME);
    }

    return book;
}

void orderbook_destroy(OrderBook *book) {
    if (!book) return;

    if (book->buy_tree) btree_destroy(book->buy_tree);
    if (book->sell_tree) btree_destroy(book->sell_tree);
    if (book->lookup_table) lookup_destroy(book->lookup_table);
    if (book->expiration_heap) heap_destroy(book->expiration_heap);

    if (book->trade_log_file) {
        fclose(book->trade_log_file);
        book->trade_log_file = NULL;
    }

    free(book);
}

/* ========================================================================= */
/* ORDER SUBMISSION AND MATCHING ENGINE (STAGE 4)                            */
/* ========================================================================= */
int orderbook_submit_order(OrderBook *book, long id, OrderType type,
                           long price, long qty, long long ttl_ms) {
    if (!book) return 0;

    /* Process any pending order expirations before matching */
    orderbook_process_expirations(book);

    /* 1. Validation */
    if (id <= 0 || price <= 0 || qty <= 0) {
        book->stats.total_orders_processed++;
        book->stats.rejected_orders++;
        printf("[WARN] Invalid order parameters (ID: %ld, Price: %ld, Qty: %ld)\n", id, price, qty);
        return 0;
    }

    /* Check for duplicate active Order ID */
    if (lookup_find(book->lookup_table, id) != NULL) {
        book->stats.total_orders_processed++;
        book->stats.rejected_orders++;
        printf("[WARN] Duplicate Order ID #%ld rejected.\n", id);
        return 0;
    }

    long seq = ++book->arrival_sequence;
    long long now_ms = get_current_time_ms();
    long long exp_time = (ttl_ms > 0) ? (now_ms + ttl_ms) : 0LL;

    Order *incoming = order_create(id, type, price, qty, seq, exp_time);
    if (!incoming) return 0;

    book->stats.total_orders_processed++;

    /* 2. BUY Matching Logic: Match against lowest available SELL (Ask) */
    if (incoming->type == ORDER_BUY) {
        while (incoming->remaining_qty > 0) {
            PriceLevel *best_ask = btree_get_min(book->sell_tree);
            if (!best_ask || incoming->price < best_ask->price) {
                break; /* No compatible seller or book empty */
            }

            long exec_price = best_ask->price; /* Trade executes at resting price */

            while (incoming->remaining_qty > 0 && best_ask->head != NULL) {
                Order *resting = fifo_peek(best_ask);
                long trade_qty = (incoming->remaining_qty < resting->remaining_qty)
                                 ? incoming->remaining_qty
                                 : resting->remaining_qty;

                long trade_id = ++book->current_trade_id;
                long long trade_time = get_current_time_ms();

                /* Log trade event to console */
                printf("[TRADE] BUY #%ld matched SELL #%ld | Price: %ld | Qty: %ld\n",
                       incoming->order_id, resting->order_id, exec_price, trade_qty);

                /* Persist trade to trades.log */
                if (book->trade_log_file) {
                    fprintf(book->trade_log_file, "%ld,%ld,%ld,%ld,%ld,%lld\n",
                            trade_id, incoming->order_id, resting->order_id,
                            exec_price, trade_qty, trade_time);
                    fflush(book->trade_log_file);
                }

                book->stats.total_trades_executed++;
                book->stats.total_volume_traded += trade_qty;

                incoming->remaining_qty -= trade_qty;
                price_level_reduce_qty(best_ask, resting, trade_qty);

                if (resting->remaining_qty == 0) {
                    resting->is_active = 0;
                    lookup_remove(book->lookup_table, resting->order_id);
                    Order *popped = fifo_dequeue(best_ask);
                    order_destroy(popped);
                } else {
                    book->stats.partially_filled_orders++;
                    printf("[INFO] Order #%ld partially filled (Remaining: %ld)\n",
                           resting->order_id, resting->remaining_qty);
                }
            }

            /* If the entire price level was exhausted, remove it from the B-Tree */
            if (best_ask->order_count == 0) {
                btree_delete_price(book->sell_tree, exec_price);
            }
        }

        /* If incoming BUY order still has unfilled quantity, place on BUY book */
        if (incoming->remaining_qty > 0) {
            if (incoming->remaining_qty < incoming->original_qty) {
                book->stats.partially_filled_orders++;
                printf("[INFO] Incoming BUY #%ld partially filled (Remaining: %ld), added to book.\n",
                       incoming->order_id, incoming->remaining_qty);
            } else {
                printf("[INFO] Order accepted: BUY #%ld | Price: %ld | Qty: %ld\n",
                       incoming->order_id, incoming->price, incoming->remaining_qty);
            }

            PriceLevel *level = btree_search(book->buy_tree, incoming->price);
            if (!level) {
                level = price_level_create(incoming->price);
                btree_insert(book->buy_tree, level);
            }
            fifo_enqueue(level, incoming);
            lookup_insert(book->lookup_table, incoming, level);
            if (incoming->expiration_time > 0) {
                heap_insert(book->expiration_heap, incoming->expiration_time, incoming->order_id);
            }
        } else {
            incoming->is_active = 0;
            order_destroy(incoming);
        }
    }
    /* 3. SELL Matching Logic: Match against highest available BUY (Bid) */
    else {
        while (incoming->remaining_qty > 0) {
            PriceLevel *best_bid = btree_get_max(book->buy_tree);
            if (!best_bid || incoming->price > best_bid->price) {
                break; /* No compatible buyer or book empty */
            }

            long exec_price = best_bid->price; /* Trade executes at resting price */

            while (incoming->remaining_qty > 0 && best_bid->head != NULL) {
                Order *resting = fifo_peek(best_bid);
                long trade_qty = (incoming->remaining_qty < resting->remaining_qty)
                                 ? incoming->remaining_qty
                                 : resting->remaining_qty;

                long trade_id = ++book->current_trade_id;
                long long trade_time = get_current_time_ms();

                /* Log trade event to console */
                printf("[TRADE] BUY #%ld matched SELL #%ld | Price: %ld | Qty: %ld\n",
                       resting->order_id, incoming->order_id, exec_price, trade_qty);

                /* Persist trade to trades.log */
                if (book->trade_log_file) {
                    fprintf(book->trade_log_file, "%ld,%ld,%ld,%ld,%ld,%lld\n",
                            trade_id, resting->order_id, incoming->order_id,
                            exec_price, trade_qty, trade_time);
                    fflush(book->trade_log_file);
                }

                book->stats.total_trades_executed++;
                book->stats.total_volume_traded += trade_qty;

                incoming->remaining_qty -= trade_qty;
                price_level_reduce_qty(best_bid, resting, trade_qty);

                if (resting->remaining_qty == 0) {
                    resting->is_active = 0;
                    lookup_remove(book->lookup_table, resting->order_id);
                    Order *popped = fifo_dequeue(best_bid);
                    order_destroy(popped);
                } else {
                    book->stats.partially_filled_orders++;
                    printf("[INFO] Order #%ld partially filled (Remaining: %ld)\n",
                           resting->order_id, resting->remaining_qty);
                }
            }

            /* If the entire price level was exhausted, remove it from the B-Tree */
            if (best_bid->order_count == 0) {
                btree_delete_price(book->buy_tree, exec_price);
            }
        }

        /* If incoming SELL order still has unfilled quantity, place on SELL book */
        if (incoming->remaining_qty > 0) {
            if (incoming->remaining_qty < incoming->original_qty) {
                book->stats.partially_filled_orders++;
                printf("[INFO] Incoming SELL #%ld partially filled (Remaining: %ld), added to book.\n",
                       incoming->order_id, incoming->remaining_qty);
            } else {
                printf("[INFO] Order accepted: SELL #%ld | Price: %ld | Qty: %ld\n",
                       incoming->order_id, incoming->price, incoming->remaining_qty);
            }

            PriceLevel *level = btree_search(book->sell_tree, incoming->price);
            if (!level) {
                level = price_level_create(incoming->price);
                btree_insert(book->sell_tree, level);
            }
            fifo_enqueue(level, incoming);
            lookup_insert(book->lookup_table, incoming, level);
            if (incoming->expiration_time > 0) {
                heap_insert(book->expiration_heap, incoming->expiration_time, incoming->order_id);
            }
        } else {
            incoming->is_active = 0;
            order_destroy(incoming);
        }
    }

    book->stats.successful_orders++;
    return 1;
}

int orderbook_cancel_order(OrderBook *book, long order_id) {
    if (!book) return 0;

    HashEntry *entry = lookup_find(book->lookup_table, order_id);
    if (!entry || !entry->order || entry->order->is_active == 0) {
        printf("[WARN] Order #%ld not found or already inactive.\n", order_id);
        return 0;
    }

    Order *o = entry->order;
    PriceLevel *level = entry->level;
    long price = o->price;
    OrderType type = o->type;

    /* Mark inactive and remove from FIFO queue */
    o->is_active = 0;
    fifo_remove_order(level, order_id);

    /* If the price level is now empty, prune it from the B-Tree */
    if (level->order_count == 0) {
        if (type == ORDER_BUY) {
            btree_delete_price(book->buy_tree, price);
        } else {
            btree_delete_price(book->sell_tree, price);
        }
    }

    /* Remove from hash table lookup */
    lookup_remove(book->lookup_table, order_id);

    book->stats.total_canceled_orders++;
    printf("[INFO] Order #%ld canceled successfully.\n", order_id);
    return 1;
}

void orderbook_process_expirations(OrderBook *book) {
    if (!book || !book->expiration_heap) return;

    long long now_ms = get_current_time_ms();
    long long exp_time = 0;
    long order_id = 0;

    while (heap_peek(book->expiration_heap, &exp_time, &order_id)) {
        if (exp_time > now_ms) {
            /* Earliest order in min-heap has not yet expired */
            break;
        }

        /* Pop the expired order record */
        heap_pop(book->expiration_heap, &exp_time, &order_id);

        /* Lazy invalidation check: is the order still active in the lookup table? */
        HashEntry *entry = lookup_find(book->lookup_table, order_id);
        if (!entry || !entry->order || entry->order->is_active == 0) {
            /* Stale heap entry: order was already matched or canceled */
            continue;
        }

        Order *o = entry->order;
        PriceLevel *level = entry->level;
        long price = o->price;
        OrderType type = o->type;

        /* Mark inactive and remove from FIFO queue */
        o->is_active = 0;
        fifo_remove_order(level, order_id);

        /* If price level is now empty, prune from B-Tree */
        if (level->order_count == 0) {
            if (type == ORDER_BUY) {
                btree_delete_price(book->buy_tree, price);
            } else {
                btree_delete_price(book->sell_tree, price);
            }
        }

        lookup_remove(book->lookup_table, order_id);
        book->stats.total_expired_orders++;
        printf("[WARN] Order #%ld expired (TTL reached at %lld ms).\n", order_id, exp_time);
    }
}

static void print_level_row(PriceLevel *level, void *user_data) {
    if (!level) return;
    int detailed = *((int*)user_data);
    printf("%-11ld %-17ld %-6d\n", level->price, level->total_volume, level->order_count);
    if (detailed && level->head) {
        Order *curr = level->head;
        while (curr) {
            printf("  └─> Order #%ld | Qty: %ld/%ld | TTL: %lld ms | Seq: %ld\n",
                   curr->order_id, curr->remaining_qty, curr->original_qty,
                   curr->expiration_time, curr->arrival_sequence);
            curr = curr->next;
        }
    }
}

static void count_orders_visitor(PriceLevel *level, void *user_data) {
    if (!level) return;
    long *count = (long*)user_data;
    *count += level->order_count;
}

void orderbook_display_bids(OrderBook *book, int detailed) {
    if (!book) return;
    printf("\n================ BUY ORDER BOOK ================\n");
    printf("Price       Total Quantity    Orders\n");
    if (btree_is_empty(book->buy_tree)) {
        printf("(BUY book is currently empty)\n");
        return;
    }
    btree_traverse_descending(book->buy_tree, print_level_row, &detailed);
}

void orderbook_display_asks(OrderBook *book, int detailed) {
    if (!book) return;
    printf("\n================ SELL ORDER BOOK ===============\n");
    printf("Price       Total Quantity    Orders\n");
    if (btree_is_empty(book->sell_tree)) {
        printf("(SELL book is currently empty)\n");
        return;
    }
    btree_traverse_ascending(book->sell_tree, print_level_row, &detailed);
}

void orderbook_display_stats(OrderBook *book) {
    if (!book) return;

    long open_buy_orders = 0;
    long open_sell_orders = 0;
    btree_traverse_descending(book->buy_tree, count_orders_visitor, &open_buy_orders);
    btree_traverse_ascending(book->sell_tree, count_orders_visitor, &open_sell_orders);

    PriceLevel *best_bid = btree_get_max(book->buy_tree);
    PriceLevel *best_ask = btree_get_min(book->sell_tree);

    double throughput = 0.0;
    if (book->stats.elapsed_time_seconds > 0.00001) {
        throughput = (double)book->stats.total_orders_processed / book->stats.elapsed_time_seconds;
    }

    printf("\n========================================\n");
    printf("           MARKET STATISTICS            \n");
    printf("========================================\n");
    printf("Orders processed       : %ld\n", book->stats.total_orders_processed);
    printf("Successful orders      : %ld\n", book->stats.successful_orders);
    printf("Rejected orders        : %ld\n", book->stats.rejected_orders);
    printf("Executed trades        : %ld\n", book->stats.total_trades_executed);
    printf("Partially filled orders: %ld\n", book->stats.partially_filled_orders);
    printf("Canceled orders        : %ld\n", book->stats.total_canceled_orders);
    printf("Expired orders         : %ld\n", book->stats.total_expired_orders);
    printf("Open BUY orders        : %ld\n", open_buy_orders);
    printf("Open SELL orders       : %ld\n", open_sell_orders);

    if (best_bid) {
        printf("Best Bid               : %ld\n", best_bid->price);
    } else {
        printf("Best Bid               : None\n");
    }

    if (best_ask) {
        printf("Best Ask               : %ld\n", best_ask->price);
    } else {
        printf("Best Ask               : None\n");
    }

    printf("Total traded quantity  : %ld\n", book->stats.total_volume_traded);
    printf("Elapsed time           : %.4f seconds\n", book->stats.elapsed_time_seconds);
    printf("Measured throughput    : %.2f orders/sec\n", throughput);
    printf("========================================\n");
}

void orderbook_display_trade_history(void) {
    FILE *f = fopen(TRADES_LOG_FILENAME, "r");
    if (!f) {
        printf("[WARN] No trade history found (%s is empty or not created yet).\n", TRADES_LOG_FILENAME);
        return;
    }
    printf("\nTrade ID | Buy ID | Sell ID | Price      | Quantity\n");
    printf("------------------------------------------------\n");
    char line[256];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (strlen(line) == 0) continue;

        /* Parse trade entry: TradeID,BuyID,SellID,Price,Qty,Timestamp */
        long trade_id = 0, buy_id = 0, sell_id = 0, price = 0, qty = 0;
        if (sscanf(line, "%ld,%ld,%ld,%ld,%ld", &trade_id, &buy_id, &sell_id, &price, &qty) >= 5) {
            printf("%-8ld | %-6ld | %-7ld | %-10ld | %-8ld\n",
                   trade_id, buy_id, sell_id, price, qty);
            count++;
        }
    }
    if (count == 0) {
        printf("(No trades recorded yet in %s)\n", TRADES_LOG_FILENAME);
    }
    fclose(f);
}

int process_order_feed(OrderBook *book, const char *filename) {
    if (!book || !filename) return 0;
    FILE *f = fopen(filename, "r");
    if (!f) {
        printf("[ERROR] Unable to open order feed file '%s'.\n", filename);
        return 0;
    }

    printf("\n[INFO] Processing orders from '%s'...\n", filename);
    long long t_start = get_current_time_ms();

    char line[256];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) == 0) continue;

        long id = 0, price = 0, qty = 0;
        long long ttl = 0;
        char type_str[16] = {0};

        /* Format: OrderID,Type,Price,Quantity,TTL_Milliseconds */
        if (sscanf(line, "%ld,%15[^,],%ld,%ld,%lld", &id, type_str, &price, &qty, &ttl) >= 4) {
            OrderType type = (strcmp(type_str, "SELL") == 0) ? ORDER_SELL : ORDER_BUY;
            orderbook_submit_order(book, id, type, price, qty, ttl);
            count++;
        }
    }

    long long t_end = get_current_time_ms();
    double elapsed = (double)(t_end - t_start) / 1000.0;
    if (elapsed < 0.0001) elapsed = 0.0001;
    book->stats.elapsed_time_seconds += elapsed;

    printf("\n[INFO] Order feed processing completed. Total processed: %d orders in %.4f seconds.\n",
           count, elapsed);
    fclose(f);
    return count;
}

int run_full_simulation(OrderBook *book, const char *filename, int progress_interval) {
    if (!book) return 0;
    const char *target = (filename && strlen(filename) > 0) ? filename : ORDERS_FEED_FILENAME;

    /* 1. Check whether feed file exists */
    FILE *check = fopen(target, "r");
    if (!check) {
        printf("[WARN] Feed file '%s' not found.\n", target);
        printf("Would you like to generate a new simulated order feed? (y/n): ");
        fflush(stdout);
        char resp[16];
        if (fgets(resp, sizeof(resp), stdin) && (resp[0] == 'y' || resp[0] == 'Y')) {
            generate_order_feed(target);
        } else {
            printf("[INFO] Simulation aborted.\n");
            return 0;
        }
    } else {
        fclose(check);
    }

    printf("\n========================================\n");
    printf("         RUNNING FULL SIMULATION        \n");
    printf("========================================\n");

    FILE *f = fopen(target, "r");
    if (!f) {
        printf("[ERROR] Unable to open '%s'.\n", target);
        return 0;
    }

    long long t_start = get_current_time_ms();
    char line[256];
    int count = 0;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) == 0) continue;

        long id = 0, price = 0, qty = 0;
        long long ttl = 0;
        char type_str[16] = {0};

        if (sscanf(line, "%ld,%15[^,],%ld,%ld,%lld", &id, type_str, &price, &qty, &ttl) >= 4) {
            OrderType type = (strcmp(type_str, "SELL") == 0) ? ORDER_SELL : ORDER_BUY;
            orderbook_submit_order(book, id, type, price, qty, ttl);
            count++;

            /* Periodic progress update */
            if (progress_interval > 0 && count % progress_interval == 0) {
                printf("[STATS] Orders processed: %d | Trades: %ld | Traded Vol: %ld\n",
                       count, book->stats.total_trades_executed, book->stats.total_volume_traded);
            }
        }
    }

    long long t_end = get_current_time_ms();
    double elapsed = (double)(t_end - t_start) / 1000.0;
    if (elapsed < 0.0001) elapsed = 0.0001;
    book->stats.elapsed_time_seconds += elapsed;

    fclose(f);

    printf("\n[INFO] Simulation finished! Processed %d orders in %.4f seconds.\n", count, elapsed);
    orderbook_display_stats(book);
    return count;
}

/* ========================================================================= */
/* MATCHING ENGINE SELF-TEST SUITE (STAGE 4)                                 */
/* ========================================================================= */
int matching_engine_run_self_test(void) {
    printf("\n--- Running Matching Engine (Stage 4) Verification Tests ---\n");

    OrderBook *test_book = orderbook_create(3);
    if (!test_book) {
        printf("[FAIL] Failed to allocate test OrderBook.\n");
        return 0;
    }
    if (test_book->trade_log_file) {
        fclose(test_book->trade_log_file);
        test_book->trade_log_file = NULL; /* Avoid polluting trades.log during self-test */
    }
    printf("[PASS] OrderBook initialized for matching tests.\n");

    /* Test 1: Submit resting SELL order */
    orderbook_submit_order(test_book, 9001, ORDER_SELL, 6204000L, 10, 10000LL);
    PriceLevel *ask_level = btree_search(test_book->sell_tree, 6204000L);
    if (!ask_level || ask_level->total_volume != 10) {
        printf("[FAIL] Resting SELL order 9001 was not properly placed on SELL book.\n");
        orderbook_destroy(test_book);
        return 0;
    }
    printf("[PASS] Resting SELL order #9001 placed on order book (Price: 6204000, Qty: 10).\n");

    /* Test 2: Submit non-crossing BUY order (Price 6203000 < 6204000) */
    orderbook_submit_order(test_book, 9002, ORDER_BUY, 6203000L, 5, 10000LL);
    if (test_book->stats.total_trades_executed != 0) {
        printf("[FAIL] Non-crossing BUY order should not trigger a trade.\n");
        orderbook_destroy(test_book);
        return 0;
    }
    printf("[PASS] Non-crossing BUY order #9002 placed on BUY book without false trade.\n");

    /* Test 3: Submit aggressive BUY order crossing resting SELL (Partial Fill) */
    orderbook_submit_order(test_book, 9003, ORDER_BUY, 6205000L, 4, 10000LL);
    if (test_book->stats.total_trades_executed != 1 || test_book->stats.total_volume_traded != 4) {
        printf("[FAIL] Trade count or traded volume mismatch on partial fill.\n");
        orderbook_destroy(test_book);
        return 0;
    }
    ask_level = btree_search(test_book->sell_tree, 6204000L);
    if (!ask_level || ask_level->total_volume != 6) {
        printf("[FAIL] Resting SELL 9001 remaining volume should be 6, got %ld.\n",
               ask_level ? ask_level->total_volume : -1);
        orderbook_destroy(test_book);
        return 0;
    }
    printf("[PASS] Partial fill verified: Traded 4 units at resting price 6204000. Remaining resting qty: 6.\n");

    /* Test 4: Consume remainder of resting SELL and clear price level */
    orderbook_submit_order(test_book, 9004, ORDER_BUY, 6204000L, 6, 10000LL);
    if (test_book->stats.total_trades_executed != 2 || test_book->stats.total_volume_traded != 10) {
        printf("[FAIL] Remainder fill trade mismatch.\n");
        orderbook_destroy(test_book);
        return 0;
    }
    if (btree_search(test_book->sell_tree, 6204000L) != NULL) {
        printf("[FAIL] Fully filled price level 6204000 should have been deleted from SELL B-Tree.\n");
        orderbook_destroy(test_book);
        return 0;
    }
    printf("[PASS] Remainder fill verified: Exhausted price level was pruned from SELL B-Tree.\n");

    /* Test 5: Verify Price-Time FIFO Priority with multiple orders at same price */
    orderbook_submit_order(test_book, 9005, ORDER_SELL, 6208000L, 10, 10000LL); /* Arrived 1st */
    orderbook_submit_order(test_book, 9006, ORDER_SELL, 6208000L, 5, 10000LL);  /* Arrived 2nd */

    /* Incoming BUY for 12 units should consume all 10 from 9005, and 2 from 9006 */
    orderbook_submit_order(test_book, 9007, ORDER_BUY, 6208000L, 12, 10000LL);
    HashEntry *entry9005 = lookup_find(test_book->lookup_table, 9005);
    HashEntry *entry9006 = lookup_find(test_book->lookup_table, 9006);

    if (entry9005 != NULL) {
        printf("[FAIL] Order 9005 should be fully filled and removed from lookup table.\n");
        orderbook_destroy(test_book);
        return 0;
    }
    if (!entry9006 || !entry9006->order || entry9006->order->remaining_qty != 3) {
        printf("[FAIL] Order 9006 should have 3 units remaining, got %ld.\n",
               entry9006 && entry9006->order ? entry9006->order->remaining_qty : -1);
        orderbook_destroy(test_book);
        return 0;
    }
    printf("[PASS] FIFO Priority verified: Order #9005 fully filled first; Order #9006 partially filled (3 remaining).\n");

    orderbook_destroy(test_book);
    printf("[PASS] Matching test OrderBook freed cleanly.\n");
    printf("--- All Stage 4 Matching Engine Tests Passed! ---\n\n");
    return 1;
}

/* ========================================================================= */
/* EXPIRATION & CANCELLATION SELF-TEST SUITE (STAGE 5 & 6)                   */
/* ========================================================================= */
static void platform_sleep_ms(int ms) {
#if defined(_WIN32) || defined(_WIN64)
    Sleep((DWORD)ms);
#else
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&req, NULL);
#endif
}

int expiration_run_self_test(void) {
    printf("\n--- Running Order Expiration & Cancellation (Stage 5 & 6) Tests ---\n");

    OrderBook *test_book = orderbook_create(3);
    if (!test_book) {
        printf("[FAIL] Failed to allocate test OrderBook.\n");
        return 0;
    }
    if (test_book->trade_log_file) {
        fclose(test_book->trade_log_file);
        test_book->trade_log_file = NULL;
    }
    printf("[PASS] OrderBook initialized for expiration and cancellation tests.\n");

    /* 1. Place Order with very short TTL (50 ms) */
    orderbook_submit_order(test_book, 8001, ORDER_BUY, 6205000L, 10, 50LL);

    /* 2. Place Order with long TTL (50,000 ms) */
    orderbook_submit_order(test_book, 8002, ORDER_BUY, 6204000L, 8, 50000LL);

    if (lookup_find(test_book->lookup_table, 8001) == NULL ||
        lookup_find(test_book->lookup_table, 8002) == NULL) {
        printf("[FAIL] Orders 8001 and 8002 should both be active in book.\n");
        orderbook_destroy(test_book);
        return 0;
    }
    printf("[PASS] Submited Order #8001 (TTL: 50 ms) and Order #8002 (TTL: 50000 ms).\n");

    /* 3. Wait for Order 8001 to expire */
    printf("[INFO] Sleeping 80 ms to allow Order #8001 TTL to expire...\n");
    platform_sleep_ms(80);

    /* 4. Trigger expiration processing */
    orderbook_process_expirations(test_book);

    if (lookup_find(test_book->lookup_table, 8001) != NULL) {
        printf("[FAIL] Order 8001 should have been evicted due to TTL expiration.\n");
        orderbook_destroy(test_book);
        return 0;
    }
    if (lookup_find(test_book->lookup_table, 8002) == NULL) {
        printf("[FAIL] Order 8002 should still be active.\n");
        orderbook_destroy(test_book);
        return 0;
    }
    printf("[PASS] Order #8001 successfully expired via Leftist Heap. Order #8002 remains active.\n");

    /* 5. Test Cancellation on active Order 8002 */
    int canceled = orderbook_cancel_order(test_book, 8002);
    if (!canceled || lookup_find(test_book->lookup_table, 8002) != NULL) {
        printf("[FAIL] Cancellation of Order 8002 failed.\n");
        orderbook_destroy(test_book);
        return 0;
    }
    printf("[PASS] Order #8002 canceled successfully and unlinked from lookup table.\n");

    /* 6. Verify B-Tree price level clean-up */
    if (!btree_is_empty(test_book->buy_tree)) {
        printf("[FAIL] BUY B-Tree should be empty after expiration and cancellation.\n");
        orderbook_destroy(test_book);
        return 0;
    }
    printf("[PASS] BUY B-Tree is completely empty after empty price level pruning.\n");

    /* 7. Attempt canceling non-existent / already-canceled order */
    if (orderbook_cancel_order(test_book, 8002) != 0) {
        printf("[FAIL] Canceling already inactive order should safely return 0.\n");
        orderbook_destroy(test_book);
        return 0;
    }
    printf("[PASS] Safe handling of duplicate/stale cancellation verified.\n");

    orderbook_destroy(test_book);
    printf("[PASS] OrderBook destroyed cleanly.\n");
    printf("--- All Stage 5 & 6 Expiration & Cancellation Tests Passed! ---\n\n");
    return 1;
}
