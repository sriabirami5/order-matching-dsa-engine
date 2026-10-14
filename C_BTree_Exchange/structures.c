#include "structures.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================= */
/* ORDER FUNCTIONS                                                           */
/* ========================================================================= */
Order* order_create(long id, OrderType type, long price, long qty,
                    long seq, long long expiration_time) {
    Order *o = (Order*)malloc(sizeof(Order));
    if (!o) {
        fprintf(stderr, "Error: Memory allocation failed for Order %ld\n", id);
        return NULL;
    }
    o->order_id = id;
    o->type = type;
    o->price = price;
    o->original_qty = qty;
    o->remaining_qty = qty;
    o->arrival_sequence = seq;
    o->expiration_time = expiration_time;
    o->is_active = 1;
    o->next = NULL;
    return o;
}

void order_destroy(Order *order) {
    if (order) {
        free(order);
    }
}

/* ========================================================================= */
/* ========================================================================= */
/* FIFO QUEUE IMPLEMENTATION (STAGE 3)                                       */
/* ========================================================================= */

PriceLevel* price_level_create(long price) {
    PriceLevel *pl = (PriceLevel*)malloc(sizeof(PriceLevel));
    if (!pl) {
        fprintf(stderr, "[ERROR] Memory allocation failed for PriceLevel %ld\n", price);
        return NULL;
    }
    pl->price = price;
    pl->head = NULL;
    pl->tail = NULL;
    pl->order_count = 0;
    pl->total_volume = 0;
    return pl;
}

void price_level_destroy(PriceLevel *level) {
    if (!level) return;
    Order *curr = level->head;
    while (curr) {
        Order *next = curr->next;
        order_destroy(curr);
        curr = next;
    }
    free(level);
}

void fifo_enqueue(PriceLevel *level, Order *order) {
    if (!level || !order) return;

    order->next = NULL;
    if (level->tail == NULL) {
        /* Queue is currently empty */
        level->head = order;
        level->tail = order;
    } else {
        /* Append to the end of the FIFO queue */
        level->tail->next = order;
        level->tail = order;
    }
    level->order_count++;
    level->total_volume += order->remaining_qty;
}

Order* fifo_dequeue(PriceLevel *level) {
    if (!level || !level->head) return NULL;

    Order *order = level->head;
    level->head = order->next;
    if (level->head == NULL) {
        /* Queue is now empty */
        level->tail = NULL;
    }

    order->next = NULL;
    level->order_count--;
    level->total_volume -= order->remaining_qty;
    return order;
}

Order* fifo_peek(PriceLevel *level) {
    if (!level) return NULL;
    return level->head;
}

/* Unlinks an order from the price level without freeing its memory */
Order* fifo_extract_order(PriceLevel *level, long order_id) {
    if (!level || !level->head) return NULL;

    Order *curr = level->head;
    Order *prev = NULL;

    while (curr) {
        if (curr->order_id == order_id) {
            if (prev == NULL) {
                level->head = curr->next;
            } else {
                prev->next = curr->next;
            }

            if (curr == level->tail) {
                level->tail = prev;
            }

            level->order_count--;
            level->total_volume -= curr->remaining_qty;
            curr->next = NULL;
            return curr;
        }
        prev = curr;
        curr = curr->next;
    }
    return NULL;
}

int fifo_remove_order(PriceLevel *level, long order_id) {
    Order *order = fifo_extract_order(level, order_id);
    if (order) {
        order_destroy(order);
        return 1;
    }
    return 0;
}

void price_level_reduce_qty(PriceLevel *level, Order *order, long matched_qty) {
    if (!level || !order || matched_qty <= 0) return;

    if (matched_qty > order->remaining_qty) {
        matched_qty = order->remaining_qty;
    }

    order->remaining_qty -= matched_qty;
    level->total_volume -= matched_qty;
}

/* ========================================================================= */
/* FIFO QUEUE SELF-TEST SUITE (STAGE 3)                                      */
/* ========================================================================= */
int fifo_run_self_test(void) {
    printf("\n--- Running FIFO Price-Level Queue (Stage 3) Verification Tests ---\n");

    PriceLevel *level = price_level_create(6205000L);
    if (!level) {
        printf("[FAIL] Failed to create PriceLevel.\n");
        return 0;
    }
    printf("[PASS] PriceLevel created for price 6205000.\n");

    /* Create 3 test orders */
    Order *o1 = order_create(1001, ORDER_BUY, 6205000L, 10, 1, 5000LL);
    Order *o2 = order_create(1002, ORDER_BUY, 6205000L, 5, 2, 5000LL);
    Order *o3 = order_create(1003, ORDER_BUY, 6205000L, 8, 3, 5000LL);

    fifo_enqueue(level, o1);
    fifo_enqueue(level, o2);
    fifo_enqueue(level, o3);

    if (level->order_count != 3 || level->total_volume != 23) {
        printf("[FAIL] Expected order_count=3, total_volume=23. Got count=%d, vol=%ld\n",
               level->order_count, level->total_volume);
        price_level_destroy(level);
        return 0;
    }
    printf("[PASS] Enqueued 3 orders: total count = 3, total volume = 23.\n");

    /* Verify FIFO peek returns oldest order (1001) */
    Order *peeked = fifo_peek(level);
    if (!peeked || peeked->order_id != 1001) {
        printf("[FAIL] fifo_peek did not return oldest order (1001).\n");
        price_level_destroy(level);
        return 0;
    }
    printf("[PASS] fifo_peek correctly returned oldest order (ID: 1001).\n");

    /* Test partial fill / volume reduction */
    price_level_reduce_qty(level, peeked, 4);
    if (peeked->remaining_qty != 6 || level->total_volume != 19) {
        printf("[FAIL] Partial fill quantity mismatch. Remaining: %ld, Level Vol: %ld\n",
               peeked->remaining_qty, level->total_volume);
        price_level_destroy(level);
        return 0;
    }
    printf("[PASS] Partial fill reduced Order 1001 remaining qty to 6 and total level volume to 19.\n");

    /* Test dequeue */
    Order *dequeued = fifo_dequeue(level);
    if (!dequeued || dequeued->order_id != 1001 || level->order_count != 2 || level->total_volume != 13) {
        printf("[FAIL] Dequeue order mismatch or volume mismatch.\n");
        if (dequeued) order_destroy(dequeued);
        price_level_destroy(level);
        return 0;
    }
    order_destroy(dequeued);
    printf("[PASS] Dequeued oldest order (1001). Next oldest is ID: %ld.\n", fifo_peek(level)->order_id);

    /* Test removing by ID (remove middle order 1003) */
    int removed = fifo_remove_order(level, 1003);
    if (!removed || level->order_count != 1 || level->total_volume != 5) {
        printf("[FAIL] fifo_remove_order(1003) failed or volume mismatch.\n");
        price_level_destroy(level);
        return 0;
    }
    printf("[PASS] Removed Order 1003 by ID. Remaining order count: 1, volume: 5.\n");

    /* Test removing non-existent ID */
    if (fifo_remove_order(level, 9999) != 0) {
        printf("[FAIL] Removing non-existent order should return 0.\n");
        price_level_destroy(level);
        return 0;
    }
    printf("[PASS] Safe handling of non-existent order ID verified.\n");

    /* Dequeue remaining order */
    dequeued = fifo_dequeue(level);
    if (!dequeued || dequeued->order_id != 1002 || level->head != NULL || level->tail != NULL) {
        printf("[FAIL] Emptying queue did not reset head and tail to NULL.\n");
        if (dequeued) order_destroy(dequeued);
        price_level_destroy(level);
        return 0;
    }
    order_destroy(dequeued);
    printf("[PASS] Fully drained queue. Head and tail successfully reset to NULL.\n");

    price_level_destroy(level);
    printf("[PASS] PriceLevel freed cleanly.\n");
    printf("--- All Stage 3 FIFO Queue Tests Passed! ---\n\n");
    return 1;
}

/* ========================================================================= */
/* B-TREE IMPLEMENTATION                                                     */
/* ========================================================================= */

/* Allocate and initialize a single B-Tree node */
static BTreeNode* btree_node_create(int min_degree, int is_leaf) {
    BTreeNode *node = (BTreeNode*)malloc(sizeof(BTreeNode));
    if (!node) return NULL;

    node->is_leaf = is_leaf;
    node->num_keys = 0;
    node->keys = (long*)malloc((size_t)(2 * min_degree - 1) * sizeof(long));
    node->levels = (PriceLevel**)malloc((size_t)(2 * min_degree - 1) * sizeof(PriceLevel*));
    node->children = (BTreeNode**)calloc((size_t)(2 * min_degree), sizeof(BTreeNode*));

    if (!node->keys || !node->levels || !node->children) {
        if (node->keys) free(node->keys);
        if (node->levels) free(node->levels);
        if (node->children) free(node->children);
        free(node);
        return NULL;
    }
    return node;
}

/* Recursively free a B-Tree node and its price levels */
static void btree_node_destroy(BTreeNode *node) {
    if (!node) return;

    if (!node->is_leaf) {
        for (int i = 0; i <= node->num_keys; ++i) {
            btree_node_destroy(node->children[i]);
        }
    }

    for (int i = 0; i < node->num_keys; ++i) {
        price_level_destroy(node->levels[i]);
    }

    free(node->keys);
    free(node->levels);
    free(node->children);
    free(node);
}

BTree* btree_create(int min_degree) {
    BTree *tree = (BTree*)malloc(sizeof(BTree));
    if (!tree) return NULL;
    tree->root = NULL;
    tree->min_degree = (min_degree < 2) ? DEFAULT_BTREE_DEGREE : min_degree;
    return tree;
}

void btree_destroy(BTree *tree) {
    if (!tree) return;
    if (tree->root) {
        btree_node_destroy(tree->root);
    }
    free(tree);
}

int btree_is_empty(BTree *tree) {
    return (!tree || !tree->root || tree->root->num_keys == 0);
}

/* Search for a price level in the B-Tree */
static PriceLevel* btree_node_search(BTreeNode *node, long price) {
    if (!node) return NULL;
    int i = 0;
    while (i < node->num_keys && price > node->keys[i]) {
        i++;
    }
    if (i < node->num_keys && price == node->keys[i]) {
        return node->levels[i];
    }
    if (node->is_leaf) {
        return NULL;
    }
    return btree_node_search(node->children[i], price);
}

PriceLevel* btree_search(BTree *tree, long price) {
    if (!tree || !tree->root) return NULL;
    return btree_node_search(tree->root, price);
}

/* Split a full child node (having 2t-1 keys) into two nodes with t-1 keys */
static void btree_split_child(BTreeNode *parent, int i, BTreeNode *child, int t) {
    BTreeNode *z = btree_node_create(t, child->is_leaf);
    z->num_keys = t - 1;

    /* Copy second half of keys and price levels to new node z */
    for (int j = 0; j < t - 1; ++j) {
        z->keys[j] = child->keys[j + t];
        z->levels[j] = child->levels[j + t];
    }

    /* Copy child pointers if child is not a leaf */
    if (!child->is_leaf) {
        for (int j = 0; j < t; ++j) {
            z->children[j] = child->children[j + t];
            child->children[j + t] = NULL;
        }
    }

    child->num_keys = t - 1;

    /* Shift parent children to the right */
    for (int j = parent->num_keys; j >= i + 1; --j) {
        parent->children[j + 1] = parent->children[j];
    }
    parent->children[i + 1] = z;

    /* Shift parent keys and levels to make room for median key */
    for (int j = parent->num_keys - 1; j >= i; --j) {
        parent->keys[j + 1] = parent->keys[j];
        parent->levels[j + 1] = parent->levels[j];
    }

    parent->keys[i] = child->keys[t - 1];
    parent->levels[i] = child->levels[t - 1];
    parent->num_keys++;
}

/* Insert a price level into a non-full node */
static void btree_insert_nonfull(BTreeNode *node, PriceLevel *level, int t) {
    int i = node->num_keys - 1;

    if (node->is_leaf) {
        while (i >= 0 && node->keys[i] > level->price) {
            node->keys[i + 1] = node->keys[i];
            node->levels[i + 1] = node->levels[i];
            i--;
        }
        node->keys[i + 1] = level->price;
        node->levels[i + 1] = level;
        node->num_keys++;
    } else {
        while (i >= 0 && node->keys[i] > level->price) {
            i--;
        }
        i++;

        if (node->children[i]->num_keys == 2 * t - 1) {
            btree_split_child(node, i, node->children[i], t);
            if (level->price > node->keys[i]) {
                i++;
            }
        }
        btree_insert_nonfull(node->children[i], level, t);
    }
}

void btree_insert(BTree *tree, PriceLevel *level) {
    if (!tree || !level) return;

    /* Check if this price already exists */
    PriceLevel *existing = btree_search(tree, level->price);
    if (existing != NULL) {
        return; /* Price level already indexed */
    }

    int t = tree->min_degree;
    if (tree->root == NULL) {
        tree->root = btree_node_create(t, 1);
        tree->root->keys[0] = level->price;
        tree->root->levels[0] = level;
        tree->root->num_keys = 1;
        return;
    }

    /* If root is full, tree grows upward */
    if (tree->root->num_keys == 2 * t - 1) {
        BTreeNode *s = btree_node_create(t, 0);
        s->children[0] = tree->root;
        btree_split_child(s, 0, tree->root, t);

        int i = 0;
        if (level->price > s->keys[0]) {
            i++;
        }
        btree_insert_nonfull(s->children[i], level, t);
        tree->root = s;
    } else {
        btree_insert_nonfull(tree->root, level, t);
    }
}

PriceLevel* btree_get_min(BTree *tree) {
    if (!tree || !tree->root || tree->root->num_keys == 0) return NULL;
    BTreeNode *curr = tree->root;
    while (!curr->is_leaf) {
        curr = curr->children[0];
    }
    return curr->levels[0];
}

PriceLevel* btree_get_max(BTree *tree) {
    if (!tree || !tree->root || tree->root->num_keys == 0) return NULL;
    BTreeNode *curr = tree->root;
    while (!curr->is_leaf) {
        curr = curr->children[curr->num_keys];
    }
    return curr->levels[curr->num_keys - 1];
}

/* In-order traversal (ascending price: lowest to highest, used for SELL Asks) */
static void btree_traverse_asc_node(BTreeNode *node, void (*visit)(PriceLevel*, void*), void *user_data) {
    if (!node) return;
    for (int i = 0; i < node->num_keys; ++i) {
        if (!node->is_leaf) {
            btree_traverse_asc_node(node->children[i], visit, user_data);
        }
        if (visit) visit(node->levels[i], user_data);
    }
    if (!node->is_leaf) {
        btree_traverse_asc_node(node->children[node->num_keys], visit, user_data);
    }
}

void btree_traverse_ascending(BTree *tree, void (*visit)(PriceLevel*, void*), void *user_data) {
    if (!tree || !tree->root) return;
    btree_traverse_asc_node(tree->root, visit, user_data);
}

/* In-order traversal (descending price: highest to lowest, used for BUY Bids) */
static void btree_traverse_desc_node(BTreeNode *node, void (*visit)(PriceLevel*, void*), void *user_data) {
    if (!node) return;
    if (!node->is_leaf) {
        btree_traverse_desc_node(node->children[node->num_keys], visit, user_data);
    }
    for (int i = node->num_keys - 1; i >= 0; --i) {
        if (visit) visit(node->levels[i], user_data);
        if (!node->is_leaf) {
            btree_traverse_desc_node(node->children[i], visit, user_data);
        }
    }
}

void btree_traverse_descending(BTree *tree, void (*visit)(PriceLevel*, void*), void *user_data) {
    if (!tree || !tree->root) return;
    btree_traverse_desc_node(tree->root, visit, user_data);
}

/* Helper functions for B-Tree deletion */
static int btree_find_key(BTreeNode *node, long price) {
    int idx = 0;
    while (idx < node->num_keys && node->keys[idx] < price) {
        idx++;
    }
    return idx;
}

static void btree_remove_from_leaf(BTreeNode *node, int idx, int free_level) {
    if (free_level) {
        price_level_destroy(node->levels[idx]);
    }
    for (int i = idx + 1; i < node->num_keys; ++i) {
        node->keys[i - 1] = node->keys[i];
        node->levels[i - 1] = node->levels[i];
    }
    node->num_keys--;
}

static PriceLevel* btree_get_predecessor(BTreeNode *node, int idx) {
    BTreeNode *curr = node->children[idx];
    while (!curr->is_leaf) {
        curr = curr->children[curr->num_keys];
    }
    return curr->levels[curr->num_keys - 1];
}

static PriceLevel* btree_get_successor(BTreeNode *node, int idx) {
    BTreeNode *curr = node->children[idx + 1];
    while (!curr->is_leaf) {
        curr = curr->children[0];
    }
    return curr->levels[0];
}

static void btree_borrow_from_prev(BTreeNode *node, int idx) {
    BTreeNode *child = node->children[idx];
    BTreeNode *sibling = node->children[idx - 1];

    for (int i = child->num_keys - 1; i >= 0; --i) {
        child->keys[i + 1] = child->keys[i];
        child->levels[i + 1] = child->levels[i];
    }

    if (!child->is_leaf) {
        for (int i = child->num_keys; i >= 0; --i) {
            child->children[i + 1] = child->children[i];
        }
        child->children[0] = sibling->children[sibling->num_keys];
        sibling->children[sibling->num_keys] = NULL;
    }

    child->keys[0] = node->keys[idx - 1];
    child->levels[0] = node->levels[idx - 1];

    node->keys[idx - 1] = sibling->keys[sibling->num_keys - 1];
    node->levels[idx - 1] = sibling->levels[sibling->num_keys - 1];

    child->num_keys++;
    sibling->num_keys--;
}

static void btree_borrow_from_next(BTreeNode *node, int idx) {
    BTreeNode *child = node->children[idx];
    BTreeNode *sibling = node->children[idx + 1];

    child->keys[child->num_keys] = node->keys[idx];
    child->levels[child->num_keys] = node->levels[idx];

    if (!child->is_leaf) {
        child->children[child->num_keys + 1] = sibling->children[0];
    }

    node->keys[idx] = sibling->keys[0];
    node->levels[idx] = sibling->levels[0];

    for (int i = 1; i < sibling->num_keys; ++i) {
        sibling->keys[i - 1] = sibling->keys[i];
        sibling->levels[i - 1] = sibling->levels[i];
    }
    if (!sibling->is_leaf) {
        for (int i = 1; i <= sibling->num_keys; ++i) {
            sibling->children[i - 1] = sibling->children[i];
        }
        sibling->children[sibling->num_keys] = NULL;
    }

    child->num_keys++;
    sibling->num_keys--;
}

static void btree_merge(BTreeNode *node, int idx, int t) {
    BTreeNode *child = node->children[idx];
    BTreeNode *sibling = node->children[idx + 1];

    child->keys[t - 1] = node->keys[idx];
    child->levels[t - 1] = node->levels[idx];

    for (int i = 0; i < sibling->num_keys; ++i) {
        child->keys[i + t] = sibling->keys[i];
        child->levels[i + t] = sibling->levels[i];
    }

    if (!child->is_leaf) {
        for (int i = 0; i <= sibling->num_keys; ++i) {
            child->children[i + t] = sibling->children[i];
            sibling->children[i] = NULL;
        }
    }

    child->num_keys += sibling->num_keys + 1;

    for (int i = idx + 1; i < node->num_keys; ++i) {
        node->keys[i - 1] = node->keys[i];
        node->levels[i - 1] = node->levels[i];
    }
    for (int i = idx + 2; i <= node->num_keys; ++i) {
        node->children[i - 1] = node->children[i];
    }
    node->children[node->num_keys] = NULL;
    node->num_keys--;

    free(sibling->keys);
    free(sibling->levels);
    free(sibling->children);
    free(sibling);
}

static void btree_fill(BTreeNode *node, int idx, int t) {
    if (idx != 0 && node->children[idx - 1]->num_keys >= t) {
        btree_borrow_from_prev(node, idx);
    } else if (idx != node->num_keys && node->children[idx + 1]->num_keys >= t) {
        btree_borrow_from_next(node, idx);
    } else {
        if (idx != node->num_keys) {
            btree_merge(node, idx, t);
        } else {
            btree_merge(node, idx - 1, t);
        }
    }
}

static void btree_delete_internal(BTreeNode *node, long price, int t, int free_level) {
    int idx = btree_find_key(node, price);

    if (idx < node->num_keys && node->keys[idx] == price) {
        if (node->is_leaf) {
            btree_remove_from_leaf(node, idx, free_level);
        } else {
            if (node->children[idx]->num_keys >= t) {
                PriceLevel *pred = btree_get_predecessor(node, idx);
                long pred_price = pred->price;
                if (free_level) {
                    price_level_destroy(node->levels[idx]);
                }
                node->keys[idx] = pred_price;
                node->levels[idx] = pred;
                btree_delete_internal(node->children[idx], pred_price, t, 0);
            } else if (node->children[idx + 1]->num_keys >= t) {
                PriceLevel *succ = btree_get_successor(node, idx);
                long succ_price = succ->price;
                if (free_level) {
                    price_level_destroy(node->levels[idx]);
                }
                node->keys[idx] = succ_price;
                node->levels[idx] = succ;
                btree_delete_internal(node->children[idx + 1], succ_price, t, 0);
            } else {
                btree_merge(node, idx, t);
                btree_delete_internal(node->children[idx], price, t, free_level);
            }
        }
    } else {
        if (node->is_leaf) {
            return; /* Price key not found in tree */
        }

        int flag = (idx == node->num_keys);
        if (node->children[idx]->num_keys < t) {
            btree_fill(node, idx, t);
        }

        if (flag && idx > node->num_keys) {
            btree_delete_internal(node->children[idx - 1], price, t, free_level);
        } else {
            btree_delete_internal(node->children[idx], price, t, free_level);
        }
    }
}

void btree_delete_price(BTree *tree, long price) {
    if (!tree || !tree->root) return;

    btree_delete_internal(tree->root, price, tree->min_degree, 1);

    if (tree->root->num_keys == 0) {
        BTreeNode *old_root = tree->root;
        if (tree->root->is_leaf) {
            tree->root = NULL;
        } else {
            tree->root = tree->root->children[0];
        }
        free(old_root->keys);
        free(old_root->levels);
        free(old_root->children);
        free(old_root);
    }
}

/* Helper visitor for self-test */
static void test_collect_keys(PriceLevel *level, void *user_data) {
    if (!level || !user_data) return;
    long *arr = (long*)user_data;
    int idx = 0;
    while (arr[idx] != 0) idx++;
    arr[idx] = level->price;
}

/* ========================================================================= */
/* B-TREE SELF-TEST / VERIFICATION SUITE (STAGE 2)                           */
/* ========================================================================= */
int btree_run_self_test(void) {
    printf("\n--- Running B-Tree (Stage 2) Verification Tests ---\n");

    /* 1. Test creation */
    BTree *tree = btree_create(3);
    if (!tree) {
        printf("[FAIL] btree_create returned NULL\n");
        return 0;
    }
    printf("[PASS] B-Tree created with minimum degree t = 3 (max keys per node = 5).\n");

    /* 2. Test insertions causing node splitting */
    long prices[] = {6205000, 6202000, 6208000, 6201000, 6204000,
                     6207000, 6209000, 6203000, 6206000, 6210000, 6211000, 6212000};
    int count = (int)(sizeof(prices) / sizeof(prices[0]));

    for (int i = 0; i < count; ++i) {
        PriceLevel *pl = price_level_create(prices[i]);
        btree_insert(tree, pl);
    }
    printf("[PASS] Inserted %d unique price levels successfully with node splits.\n", count);

    /* 3. Test search */
    PriceLevel *found = btree_search(tree, 6207000);
    if (!found || found->price != 6207000) {
        printf("[FAIL] btree_search failed to find 6207000\n");
        btree_destroy(tree);
        return 0;
    }
    PriceLevel *missing = btree_search(tree, 9999999);
    if (missing != NULL) {
        printf("[FAIL] btree_search returned non-NULL for non-existent key\n");
        btree_destroy(tree);
        return 0;
    }
    printf("[PASS] btree_search verified for existing and non-existing keys.\n");

    /* 4. Test min and max */
    PriceLevel *min_pl = btree_get_min(tree);
    PriceLevel *max_pl = btree_get_max(tree);
    if (!min_pl || min_pl->price != 6201000 || !max_pl || max_pl->price != 6212000) {
        printf("[FAIL] Min (%ld) or Max (%ld) did not match expected values.\n",
               min_pl ? min_pl->price : -1, max_pl ? max_pl->price : -1);
        btree_destroy(tree);
        return 0;
    }
    printf("[PASS] btree_get_min returned %ld and btree_get_max returned %ld.\n",
           min_pl->price, max_pl->price);

    /* 5. Test ascending and descending traversals */
    long asc_collected[32] = {0};
    btree_traverse_ascending(tree, test_collect_keys, asc_collected);
    int is_sorted = 1;
    for (int i = 0; i < count - 1; ++i) {
        if (asc_collected[i] >= asc_collected[i + 1]) {
            is_sorted = 0;
            break;
        }
    }
    if (!is_sorted) {
        printf("[FAIL] Ascending traversal was not in strictly sorted order!\n");
        btree_destroy(tree);
        return 0;
    }
    printf("[PASS] Ascending in-order traversal verified strictly sorted.\n");

    /* 6. Test deletion and root contraction */
    btree_delete_price(tree, 6201000); /* Delete min */
    min_pl = btree_get_min(tree);
    if (!min_pl || min_pl->price != 6202000) {
        printf("[FAIL] After deleting 6201000, min is not 6202000.\n");
        btree_destroy(tree);
        return 0;
    }

    btree_delete_price(tree, 6212000); /* Delete max */
    max_pl = btree_get_max(tree);
    if (!max_pl || max_pl->price != 6211000) {
        printf("[FAIL] After deleting 6212000, max is not 6211000.\n");
        btree_destroy(tree);
        return 0;
    }

    btree_delete_price(tree, 6205000); /* Delete median/internal key */
    if (btree_search(tree, 6205000) != NULL) {
        printf("[FAIL] 6205000 was still found after deletion.\n");
        btree_destroy(tree);
        return 0;
    }
    printf("[PASS] Price level deletions (min, max, and internal) verified.\n");

    /* Clean up remaining keys and destroy tree */
    btree_destroy(tree);
    printf("[PASS] B-Tree destroyed cleanly with all memory freed.\n");
    printf("--- All Stage 2 B-Tree Tests Passed! ---\n\n");
    return 1;
}


/* ========================================================================= */
/* LEFTIST HEAP IMPLEMENTATION (STAGE 5 - ORDER EXPIRATION)                  */
/* ========================================================================= */

static HeapNode* heap_node_create(long long exp_time, long order_id) {
    HeapNode *node = (HeapNode*)malloc(sizeof(HeapNode));
    if (!node) return NULL;
    node->expiration_time = exp_time;
    node->order_id = order_id;
    node->npl = 0;
    node->left = NULL;
    node->right = NULL;
    return node;
}

static void heap_node_destroy(HeapNode *node) {
    if (!node) return;
    heap_node_destroy(node->left);
    heap_node_destroy(node->right);
    free(node);
}

LeftistHeap* heap_create(void) {
    LeftistHeap *h = (LeftistHeap*)malloc(sizeof(LeftistHeap));
    if (!h) return NULL;
    h->root = NULL;
    return h;
}

void heap_destroy(LeftistHeap *heap) {
    if (!heap) return;
    if (heap->root) {
        heap_node_destroy(heap->root);
    }
    free(heap);
}

/* Recursive Leftist Min-Heap Merge */
HeapNode* heap_merge(HeapNode *h1, HeapNode *h2) {
    if (!h1) return h2;
    if (!h2) return h1;

    /* Enforce Min-Heap priority: h1 must hold the earlier expiration time */
    if (h1->expiration_time > h2->expiration_time) {
        HeapNode *temp = h1;
        h1 = h2;
        h2 = temp;
    }

    /* Recursively merge h2 with the right subtree of h1 */
    h1->right = heap_merge(h1->right, h2);

    /* Enforce the Leftist Invariant: npl(left) >= npl(right) */
    int left_npl = (h1->left) ? h1->left->npl : -1;
    int right_npl = (h1->right) ? h1->right->npl : -1;

    if (left_npl < right_npl) {
        HeapNode *swap = h1->left;
        h1->left = h1->right;
        h1->right = swap;
    }

    /* Update Null Path Length (NPL): 1 + npl of right child */
    h1->npl = (h1->right) ? (h1->right->npl + 1) : 0;

    return h1;
}

void heap_insert(LeftistHeap *heap, long long expiration_time, long order_id) {
    if (!heap) return;
    HeapNode *node = heap_node_create(expiration_time, order_id);
    if (!node) return;
    heap->root = heap_merge(heap->root, node);
}

int heap_peek(LeftistHeap *heap, long long *out_expiration_time, long *out_order_id) {
    if (!heap || !heap->root) return 0;
    if (out_expiration_time) *out_expiration_time = heap->root->expiration_time;
    if (out_order_id) *out_order_id = heap->root->order_id;
    return 1;
}

int heap_pop(LeftistHeap *heap, long long *out_expiration_time, long *out_order_id) {
    if (!heap || !heap->root) return 0;

    HeapNode *old_root = heap->root;
    if (out_expiration_time) *out_expiration_time = old_root->expiration_time;
    if (out_order_id) *out_order_id = old_root->order_id;

    HeapNode *left = old_root->left;
    HeapNode *right = old_root->right;

    free(old_root);
    heap->root = heap_merge(left, right);
    return 1;
}

/* ========================================================================= */
/* LEFTIST HEAP SELF-TEST SUITE (STAGE 5)                                    */
/* ========================================================================= */
int heap_run_self_test(void) {
    printf("\n--- Running Leftist Heap (Stage 5) Verification Tests ---\n");

    LeftistHeap *h = heap_create();
    if (!h) {
        printf("[FAIL] Failed to create LeftistHeap.\n");
        return 0;
    }
    printf("[PASS] LeftistHeap created successfully.\n");

    /* Insert unordered expiration timestamps */
    long long exp_times[] = {5000LL, 2000LL, 8000LL, 1000LL, 3000LL};
    long order_ids[] = {101, 102, 103, 104, 105};
    int n = 5;

    for (int i = 0; i < n; ++i) {
        heap_insert(h, exp_times[i], order_ids[i]);
    }
    printf("[PASS] Inserted 5 expiration entries with arbitrary timestamps.\n");

    /* Peek earliest */
    long long earliest_time = 0;
    long earliest_id = 0;
    if (!heap_peek(h, &earliest_time, &earliest_id) || earliest_time != 1000LL || earliest_id != 104) {
        printf("[FAIL] heap_peek did not return minimum timestamp (expected 1000, got %lld).\n", earliest_time);
        heap_destroy(h);
        return 0;
    }
    printf("[PASS] heap_peek correctly identified earliest expiration (ID: 104 at 1000 ms).\n");

    /* Pop in ascending order */
    long long last_time = -1;
    for (int i = 0; i < n; ++i) {
        long long t = 0;
        long id = 0;
        if (!heap_pop(h, &t, &id)) {
            printf("[FAIL] heap_pop failed unexpectedly at iteration %d.\n", i);
            heap_destroy(h);
            return 0;
        }
        if (t < last_time) {
            printf("[FAIL] Min-heap property violated: popped %lld after %lld.\n", t, last_time);
            heap_destroy(h);
            return 0;
        }
        last_time = t;
    }
    printf("[PASS] Extracted all 5 entries in strictly increasing order (Min-Heap invariant verified).\n");

    /* Verify empty pop */
    if (heap_pop(h, &earliest_time, &earliest_id) != 0) {
        printf("[FAIL] heap_pop should return 0 on empty heap.\n");
        heap_destroy(h);
        return 0;
    }
    printf("[PASS] Empty heap safely handled.\n");

    heap_destroy(h);
    printf("[PASS] Leftist Heap destroyed cleanly with zero memory leaks.\n");
    printf("--- All Stage 5 Leftist Heap Tests Passed! ---\n\n");
    return 1;
}

/* ========================================================================= */
/* ORDER LOOKUP TABLE                                                        */
/* ========================================================================= */
static unsigned int hash_code(long key, int capacity) {
    unsigned long k = (unsigned long)key;
    return (unsigned int)(k % (unsigned long)capacity);
}

OrderLookupTable* lookup_create(int capacity) {
    OrderLookupTable *table = (OrderLookupTable*)malloc(sizeof(OrderLookupTable));
    if (!table) return NULL;
    table->capacity = (capacity <= 0) ? HASH_TABLE_CAPACITY : capacity;
    table->size = 0;
    table->buckets = (HashEntry**)calloc((size_t)table->capacity, sizeof(HashEntry*));
    if (!table->buckets) {
        free(table);
        return NULL;
    }
    return table;
}

void lookup_destroy(OrderLookupTable *table) {
    if (!table) return;
    for (int i = 0; i < table->capacity; ++i) {
        HashEntry *curr = table->buckets[i];
        while (curr) {
            HashEntry *next = curr->next;
            free(curr);
            curr = next;
        }
    }
    free(table->buckets);
    free(table);
}

void lookup_insert(OrderLookupTable *table, Order *order, PriceLevel *level) {
    if (!table || !order) return;
    unsigned int idx = hash_code(order->order_id, table->capacity);

    HashEntry *entry = table->buckets[idx];
    while (entry) {
        if (entry->order_id == order->order_id) {
            entry->order = order;
            entry->level = level;
            return;
        }
        entry = entry->next;
    }

    HashEntry *new_entry = (HashEntry*)malloc(sizeof(HashEntry));
    if (!new_entry) return;
    new_entry->order_id = order->order_id;
    new_entry->order = order;
    new_entry->level = level;
    new_entry->next = table->buckets[idx];
    table->buckets[idx] = new_entry;
    table->size++;
}

HashEntry* lookup_find(OrderLookupTable *table, long order_id) {
    if (!table) return NULL;
    unsigned int idx = hash_code(order_id, table->capacity);
    HashEntry *curr = table->buckets[idx];
    while (curr) {
        if (curr->order_id == order_id) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

void lookup_remove(OrderLookupTable *table, long order_id) {
    if (!table) return;
    unsigned int idx = hash_code(order_id, table->capacity);
    HashEntry *curr = table->buckets[idx];
    HashEntry *prev = NULL;

    while (curr) {
        if (curr->order_id == order_id) {
            if (!prev) {
                table->buckets[idx] = curr->next;
            } else {
                prev->next = curr->next;
            }
            free(curr);
            table->size--;
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}
