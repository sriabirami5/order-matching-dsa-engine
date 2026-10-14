#ifndef STRUCTURES_H
#define STRUCTURES_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

/* ========================================================================= */
/* CONFIGURABLE CONSTANTS                                                    */
/* ========================================================================= */
#define DEFAULT_BTREE_DEGREE    3       /* Minimum degree 't' for B-Trees   */
#define HASH_TABLE_CAPACITY     1024    /* Buckets for Order ID lookup table*/

/* ========================================================================= */
/* 1. ORDER STRUCTURE                                                        */
/* ========================================================================= */
typedef enum {
    ORDER_BUY = 0,
    ORDER_SELL = 1
} OrderType;

/* Represents an individual limit order in the exchange */
typedef struct Order {
    long order_id;                  /* Unique identifier for the order        */
    OrderType type;                 /* ORDER_BUY or ORDER_SELL                */
    long price;                     /* Fixed-point integer price (e.g. cents) */
    long original_qty;              /* Initial order quantity                 */
    long remaining_qty;             /* Remaining quantity left to be matched  */
    long arrival_sequence;          /* Sequence counter for arrival order     */
    long long expiration_time;      /* Absolute expiration epoch/monotonic ms */
    int is_active;                  /* 1 = active/resting, 0 = filled/canceled*/
    struct Order *next;             /* Pointer to next order in FIFO queue    */
} Order;

/* ========================================================================= */
/* 2. PRICE-LEVEL FIFO QUEUE STRUCTURE                                       */
/* ========================================================================= */
/* Groups all orders having the exact same price in a FIFO linked list       */
typedef struct PriceLevel {
    long price;                     /* The price of this level                */
    Order *head;                    /* Pointer to the oldest order (FIFO head)*/
    Order *tail;                    /* Pointer to newest order (FIFO tail)    */
    int order_count;                /* Total active orders at this price      */
    long total_volume;              /* Sum of remaining quantities at level   */
} PriceLevel;

/* ========================================================================= */
/* 3. B-TREE STRUCTURES                                                      */
/* ========================================================================= */
/* Node in the B-Tree indexing price levels                                   */
typedef struct BTreeNode {
    long *keys;                     /* Array of price keys                    */
    PriceLevel **levels;            /* Array of pointers to PriceLevel objects*/
    struct BTreeNode **children;    /* Array of pointers to child nodes       */
    int num_keys;                   /* Current number of keys in this node    */
    int is_leaf;                    /* 1 if node is a leaf, 0 otherwise       */
} BTreeNode;

/* B-Tree structure storing the root and minimum degree                      */
typedef struct BTree {
    BTreeNode *root;                /* Pointer to root node                   */
    int min_degree;                 /* Minimum degree 't' (each node has >=t-1*/
                                    /* keys, except root, and <= 2t-1 keys)   */
} BTree;

/* ========================================================================= */
/* 4. LEFTIST HEAP STRUCTURES (ORDER EXPIRATION / TTL)                       */
/* ========================================================================= */
/* Node in the Leftist Heap, organized as a Min-Heap by expiration timestamp */
typedef struct HeapNode {
    long long expiration_time;      /* Priority key: earliest expiration ms   */
    long order_id;                  /* Target order ID                        */
    int npl;                        /* Null Path Length (distance to null)    */
    struct HeapNode *left;          /* Left child pointer                     */
    struct HeapNode *right;         /* Right child pointer                    */
} HeapNode;

/* Leftist Heap wrapper */
typedef struct LeftistHeap {
    HeapNode *root;                 /* Pointer to root of min-heap            */
} LeftistHeap;

/* ========================================================================= */
/* 5. ORDER LOOKUP HASH TABLE                                                */
/* ========================================================================= */
/* Hash bucket entry linking an Order ID to its Order and PriceLevel objects */
typedef struct HashEntry {
    long order_id;
    Order *order;
    PriceLevel *level;
    struct HashEntry *next;
} HashEntry;

/* Hash table for O(1) average lookup and cancellation by Order ID           */
typedef struct OrderLookupTable {
    HashEntry **buckets;
    int capacity;
    int size;
} OrderLookupTable;

/* ========================================================================= */
/* FUNCTION PROTOTYPES - ORDER HELPERS                                       */
/* ========================================================================= */
Order* order_create(long id, OrderType type, long price, long qty,
                    long seq, long long expiration_time);
void order_destroy(Order *order);

/* ========================================================================= */
/* FUNCTION PROTOTYPES - FIFO QUEUE                                          */
/* ========================================================================= */
PriceLevel* price_level_create(long price);
void price_level_destroy(PriceLevel *level);
void fifo_enqueue(PriceLevel *level, Order *order);
Order* fifo_dequeue(PriceLevel *level);
Order* fifo_peek(PriceLevel *level);
Order* fifo_extract_order(PriceLevel *level, long order_id);
int fifo_remove_order(PriceLevel *level, long order_id);
void price_level_reduce_qty(PriceLevel *level, Order *order, long matched_qty);
int fifo_run_self_test(void);

/* ========================================================================= */
/* FUNCTION PROTOTYPES - B-TREE                                              */
/* ========================================================================= */
BTree* btree_create(int min_degree);
void btree_destroy(BTree *tree);
PriceLevel* btree_search(BTree *tree, long price);
void btree_insert(BTree *tree, PriceLevel *level);
PriceLevel* btree_get_min(BTree *tree);
PriceLevel* btree_get_max(BTree *tree);
void btree_delete_price(BTree *tree, long price);
int btree_is_empty(BTree *tree);
void btree_traverse_ascending(BTree *tree, void (*visit)(PriceLevel*, void*), void *user_data);
void btree_traverse_descending(BTree *tree, void (*visit)(PriceLevel*, void*), void *user_data);
int btree_run_self_test(void);

/* ========================================================================= */
/* FUNCTION PROTOTYPES - LEFTIST HEAP                                        */
/* ========================================================================= */
LeftistHeap* heap_create(void);
void heap_destroy(LeftistHeap *heap);
HeapNode* heap_merge(HeapNode *h1, HeapNode *h2);
void heap_insert(LeftistHeap *heap, long long expiration_time, long order_id);
int heap_peek(LeftistHeap *heap, long long *out_expiration_time, long *out_order_id);
int heap_pop(LeftistHeap *heap, long long *out_expiration_time, long *out_order_id);
int heap_run_self_test(void);

/* ========================================================================= */
/* FUNCTION PROTOTYPES - ORDER LOOKUP TABLE                                  */
/* ========================================================================= */
OrderLookupTable* lookup_create(int capacity);
void lookup_destroy(OrderLookupTable *table);
void lookup_insert(OrderLookupTable *table, Order *order, PriceLevel *level);
HashEntry* lookup_find(OrderLookupTable *table, long order_id);
void lookup_remove(OrderLookupTable *table, long order_id);

#endif /* STRUCTURES_H */
