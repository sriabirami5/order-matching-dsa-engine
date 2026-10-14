#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================= */
/* CONFIGURABLE SETTINGS (Easily editable by students)                       */
/* ========================================================================= */
#define CONFIG_NUM_ORDERS       50          /* Total orders to generate      */
#define CONFIG_MIN_PRICE        6200000L    /* Minimum price (fixed-point)   */
#define CONFIG_MAX_PRICE        6210000L    /* Maximum price (fixed-point)   */
#define CONFIG_PRICE_STEP       500L        /* Price tick size / granularity */
#define CONFIG_MIN_QTY          1L          /* Minimum quantity per order    */
#define CONFIG_MAX_QTY          25L         /* Maximum quantity per order    */
#define CONFIG_MIN_TTL_MS       2000LL      /* Minimum TTL in milliseconds   */
#define CONFIG_MAX_TTL_MS       15000LL     /* Maximum TTL in milliseconds   */
#define CONFIG_START_ORDER_ID   1001L       /* Initial Order ID              */
#define CONFIG_RANDOM_SEED      42          /* Seed for reproducible feeds   */

/* ========================================================================= */
/* RANDOM NUMBER GENERATION HELPERS                                          */
/* ========================================================================= */
static long random_in_range(long min_val, long max_val) {
    if (min_val >= max_val) return min_val;
    return min_val + (rand() % (max_val - min_val + 1));
}

/* ========================================================================= */
/* ORDER FEED GENERATOR FUNCTION                                             */
/* ========================================================================= */
int generate_order_feed(const char *filename) {
    const char *target = (filename && strlen(filename) > 0) ? filename : ORDERS_FEED_FILENAME;

    /* Step 1: Check if the file already exists and ask for confirmation */
    FILE *check_file = fopen(target, "r");
    if (check_file != NULL) {
        fclose(check_file);
        printf("[WARN] File '%s' already exists.\n", target);
        printf("Do you want to overwrite it? (y/n): ");
        fflush(stdout);

        char response[16];
        if (fgets(response, sizeof(response), stdin) == NULL) {
            printf("[WARN] Aborted: Could not read input.\n");
            return 0;
        }

        if (response[0] != 'y' && response[0] != 'Y') {
            printf("[INFO] Operation canceled. Existing feed file preserved.\n");
            return 0;
        }
    }

    /* Step 2: Open target file for writing */
    FILE *out = fopen(target, "w");
    if (!out) {
        fprintf(stderr, "[ERROR] Unable to open '%s' for writing.\n", target);
        return 0;
    }

    /* Step 3: Seed random number generator */
    srand(CONFIG_RANDOM_SEED);

    printf("\n[INFO] Generating %d simulated orders into '%s'...\n", CONFIG_NUM_ORDERS, target);

    long current_id = CONFIG_START_ORDER_ID;
    int orders_written = 0;

    for (int i = 0; i < CONFIG_NUM_ORDERS; ++i) {
        /* Determine order type: 0 = BUY, 1 = SELL */
        int is_buy = (rand() % 2 == 0);
        const char *type_str = is_buy ? "BUY" : "SELL";

        /* Generate price with realistic tick granularity */
        long price_steps = (CONFIG_MAX_PRICE - CONFIG_MIN_PRICE) / CONFIG_PRICE_STEP;
        long step_offset = (rand() % (price_steps + 1)) * CONFIG_PRICE_STEP;
        long price = CONFIG_MIN_PRICE + step_offset;

        /* Generate quantity and TTL */
        long qty = random_in_range(CONFIG_MIN_QTY, CONFIG_MAX_QTY);
        long long ttl = (long long)random_in_range((long)CONFIG_MIN_TTL_MS, (long)CONFIG_MAX_TTL_MS);

        /* Write record in format: OrderID,Type,Price,Quantity,TTL_Milliseconds */
        int res = fprintf(out, "%ld,%s,%ld,%ld,%lld\n",
                          current_id, type_str, price, qty, ttl);
        if (res < 0) {
            fprintf(stderr, "[ERROR] Writing order %ld to file failed.\n", current_id);
            break;
        }

        current_id++;
        orders_written++;
    }

    fclose(out);
    printf("[INFO] Order feed generated successfully (%d orders in '%s').\n\n", orders_written, target);
    return orders_written;
}
