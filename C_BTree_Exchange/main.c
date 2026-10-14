#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================= */
/* CONFIGURABLE SETTINGS                                                     */
/* ========================================================================= */
#define BTREE_MIN_DEGREE    3       /* Configurable B-Tree degree (default: 3) */
#define SIM_PROGRESS_STEP   1000    /* Progress logging interval for simulation*/

/* ========================================================================= */
/* CLI MENU DISPLAY                                                          */
/* ========================================================================= */
static void print_banner(void) {
    printf("\n========================================\n");
    printf("       C-BTREE EXCHANGE ENGINE          \n");
    printf("       Order Matching Simulator         \n");
    printf("========================================\n");
}

static void print_menu(void) {
    print_banner();
    printf("  1. Generate Order Feed\n");
    printf("  2. Process Order Feed\n");
    printf("  3. Submit Order Manually\n");
    printf("  4. View BUY Order Book\n");
    printf("  5. View SELL Order Book\n");
    printf("  6. Search Order by ID\n");
    printf("  7. Cancel an Order\n");
    printf("  8. View Trade History\n");
    printf("  9. View Market Statistics\n");
    printf(" 10. Run Full Simulation\n");
    printf("  0. Exit\n");
    printf("========================================\n");
    printf("Enter your choice: ");
    fflush(stdout);
}

/* Helper to read a line safely from stdin */
static int read_line(char *buf, size_t size) {
    if (fgets(buf, (int)size, stdin) == NULL) {
        return 0;
    }
    buf[strcspn(buf, "\r\n")] = '\0';
    return 1;
}

/* ========================================================================= */
/* MAIN PROCESSING LOOP                                                      */
/* ========================================================================= */
int main(int argc, char *argv[]) {
    if (argc > 1) {
        if (strcmp(argv[1], "--test-btree") == 0) {
            return btree_run_self_test() ? 0 : 1;
        } else if (strcmp(argv[1], "--test-fifo") == 0) {
            return fifo_run_self_test() ? 0 : 1;
        } else if (strcmp(argv[1], "--test-heap") == 0) {
            return heap_run_self_test() ? 0 : 1;
        } else if (strcmp(argv[1], "--test-matching") == 0) {
            return matching_engine_run_self_test() ? 0 : 1;
        } else if (strcmp(argv[1], "--test-expiration") == 0) {
            return expiration_run_self_test() ? 0 : 1;
        } else if (strcmp(argv[1], "--test-all") == 0) {
            int btree_ok = btree_run_self_test();
            int fifo_ok = fifo_run_self_test();
            int heap_ok = heap_run_self_test();
            int match_ok = matching_engine_run_self_test();
            int exp_ok = expiration_run_self_test();
            return (btree_ok && fifo_ok && heap_ok && match_ok && exp_ok) ? 0 : 1;
        }
    }

    printf("[INFO] Initializing C-BTree Exchange Engine...\n");

    /* Initialize the central order book with the configured B-Tree degree */
    OrderBook *book = orderbook_create(BTREE_MIN_DEGREE);
    if (!book) {
        fprintf(stderr, "[ERROR] Failed to allocate Order Book.\n");
        return 1;
    }

    printf("[INFO] Order book ready with B-Tree degree t = %d.\n", BTREE_MIN_DEGREE);

    int running = 1;
    char buffer[128];
    static long manual_order_id_counter = 5001;

    while (running) {
        print_menu();

        if (!read_line(buffer, sizeof(buffer))) {
            break;
        }

        if (strlen(buffer) == 0) {
            continue;
        }

        char *endptr;
        long choice = strtol(buffer, &endptr, 10);
        if (*endptr != '\0') {
            printf("[WARN] Invalid input. Please enter a number (0 - 10).\n");
            continue;
        }

        switch (choice) {
            case 1:
                generate_order_feed(ORDERS_FEED_FILENAME);
                break;

            case 2:
                process_order_feed(book, ORDERS_FEED_FILENAME);
                break;

            case 3: {
                printf("\n--- Submit Order Manually ---\n");
                printf("Enter Order Type (BUY or SELL): ");
                fflush(stdout);
                if (!read_line(buffer, sizeof(buffer)) || strlen(buffer) == 0) {
                    printf("[WARN] Aborted manual order entry.\n");
                    break;
                }

                OrderType type = ORDER_BUY;
                if (buffer[0] == 's' || buffer[0] == 'S') {
                    type = ORDER_SELL;
                } else if (buffer[0] != 'b' && buffer[0] != 'B') {
                    printf("[WARN] Invalid type. Must be BUY or SELL.\n");
                    break;
                }

                printf("Enter Price (e.g. 6205000): ");
                fflush(stdout);
                if (!read_line(buffer, sizeof(buffer))) break;
                long price = strtol(buffer, NULL, 10);
                if (price <= 0) {
                    printf("[WARN] Price must be a positive integer.\n");
                    break;
                }

                printf("Enter Quantity (e.g. 10): ");
                fflush(stdout);
                if (!read_line(buffer, sizeof(buffer))) break;
                long qty = strtol(buffer, NULL, 10);
                if (qty <= 0) {
                    printf("[WARN] Quantity must be a positive integer.\n");
                    break;
                }

                printf("Enter TTL in milliseconds (default 10000, 0 = no expiry): ");
                fflush(stdout);
                long long ttl = 10000LL;
                if (read_line(buffer, sizeof(buffer)) && strlen(buffer) > 0) {
                    ttl = strtoll(buffer, NULL, 10);
                    if (ttl < 0) ttl = 0;
                }

                printf("Enter Order ID (press Enter to auto-generate #%ld): ", manual_order_id_counter);
                fflush(stdout);
                long id = manual_order_id_counter++;
                if (read_line(buffer, sizeof(buffer)) && strlen(buffer) > 0) {
                    long custom_id = strtol(buffer, NULL, 10);
                    if (custom_id > 0) id = custom_id;
                }

                orderbook_submit_order(book, id, type, price, qty, ttl);
                break;
            }

            case 4: {
                printf("Show detailed individual orders? (y/n): ");
                fflush(stdout);
                int detailed = 0;
                if (read_line(buffer, sizeof(buffer))) {
                    if (buffer[0] == 'y' || buffer[0] == 'Y') detailed = 1;
                }
                orderbook_display_bids(book, detailed);
                break;
            }

            case 5: {
                printf("Show detailed individual orders? (y/n): ");
                fflush(stdout);
                int detailed = 0;
                if (read_line(buffer, sizeof(buffer))) {
                    if (buffer[0] == 'y' || buffer[0] == 'Y') detailed = 1;
                }
                orderbook_display_asks(book, detailed);
                break;
            }

            case 6: {
                printf("Enter Order ID to search: ");
                fflush(stdout);
                if (read_line(buffer, sizeof(buffer))) {
                    long id = strtol(buffer, NULL, 10);
                    if (id <= 0) {
                        printf("[WARN] Invalid Order ID format.\n");
                        break;
                    }
                    HashEntry *entry = lookup_find(book->lookup_table, id);
                    if (entry && entry->order) {
                        Order *o = entry->order;
                        printf("[INFO] Order #%ld: Type=%s, Price=%ld, Qty=%ld/%ld, Active=%d, Exp=%lld ms\n",
                               o->order_id, (o->type == ORDER_BUY ? "BUY" : "SELL"),
                               o->price, o->remaining_qty, o->original_qty, o->is_active, o->expiration_time);
                    } else {
                        printf("[WARN] Order ID %ld not found.\n", id);
                    }
                }
                break;
            }

            case 7: {
                printf("Enter Order ID to cancel: ");
                fflush(stdout);
                if (read_line(buffer, sizeof(buffer))) {
                    long id = strtol(buffer, NULL, 10);
                    if (id <= 0) {
                        printf("[WARN] Invalid Order ID format.\n");
                        break;
                    }
                    printf("Are you sure you want to cancel order #%ld? (y/n): ", id);
                    fflush(stdout);
                    if (read_line(buffer, sizeof(buffer)) && (buffer[0] == 'y' || buffer[0] == 'Y')) {
                        orderbook_cancel_order(book, id);
                    } else {
                        printf("[INFO] Cancellation aborted.\n");
                    }
                }
                break;
            }

            case 8:
                orderbook_display_trade_history();
                break;

            case 9:
                orderbook_display_stats(book);
                break;

            case 10:
                run_full_simulation(book, ORDERS_FEED_FILENAME, SIM_PROGRESS_STEP);
                break;

            case 0:
                printf("\n[INFO] Shutting down exchange engine...\n");
                running = 0;
                break;

            default:
                printf("[WARN] Invalid menu choice: %ld. Please enter a number between 0 and 10.\n", choice);
                break;
        }
    }

    /* Clean up all dynamically allocated memory */
    orderbook_destroy(book);
    printf("[INFO] All memory freed. Exchange engine terminated cleanly.\n");

    return 0;
}
