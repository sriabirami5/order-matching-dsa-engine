Order Matching Engine (C_BTree_Exchange)
========================================

A limit order matching engine written in C for a DSA mini-project. It simulates an exchange order book that matches buy and sell orders based on Price-Time Priority using manual data structures, without any external libraries.

Features
--------
- Price-Time Priority: Orders are prioritized by best price first. When prices are identical, older orders are executed first.
- Custom Data Structures: Uses manual pointer-based implementations for a B-Tree, FIFO linked list, Leftist Heap, and Hash Table.
- Fixed-Point Arithmetic: Prices are stored as integer values (such as 6205000 for 62.05) to avoid floating-point inaccuracies.
- Order Expiration (TTL): Orders can have a time-to-live in milliseconds; expired orders are automatically tracked and removed.
- Fast Cancellation: Active orders can be searched and canceled instantly by Order ID using a hash table.
- Interactive CLI: Menu-driven console interface supporting both automated feed processing and manual order entry.
- Audit Logging: Executed trades are logged to trades.log with timestamp, matched order IDs, price, and traded quantity.

Data Structures Used
--------------------
- B-Tree (Price level indexing): Keeps price levels sorted in O(log N) time. Buy orders are stored descending (highest bid first) and Sell orders ascending (lowest ask first). Prevents unbalanced tree degradation.
- FIFO Linked List (Price level queue): Each price node in the B-Tree points to a linked list with head and tail pointers. Gives O(1) enqueue for new orders and O(1) dequeue when matching.
- Leftist Min-Heap (Order expiration / TTL): Organizes orders by expiration timestamp. Finds the earliest expiring order at the root in O(1) and supports fast merges in O(log N).
- Hash Table (Order lookup and cancel): Separate chaining hash table that maps order ID to its order pointer. Avoids searching through the entire B-Tree when looking up or canceling an order.

Project Structure
-----------------
The project files:

C_BTree_Exchange/
  main.c           : CLI menu, user input handling, and program loop
  structures.h     : Struct declarations for Order, B-Tree, FIFO queue, and Leftist Heap
  structures.c     : Implementations of B-Tree, queue, heap, and hash table
  engine.h         : Function prototypes for order matching, book lifecycle, and stats
  engine.c         : Order matching logic, trade logging, cancellation, and expiration
  data_gen.c       : Synthetic order generator for test feeds
  orders_feed.txt  : Input feed of orders (OrderID, Type, Price, Quantity, TTL)
  trades.log       : Output log file recording executed trades
  setup.bat        : One-click setup and launcher script for Windows

Compilation and Execution
-------------------------
Windows (Quick One-Click):
  Double-click setup.bat.
  - If GCC is installed, it compiles the code and opens the program.
  - If GCC is not installed on that PC, it runs the pre-compiled exchange.exe directly.

Windows (Manual Compilation with MinGW):
  gcc -std=c11 -Wall -Wextra -pedantic -O2 main.c structures.c engine.c data_gen.c -o exchange.exe
  .\exchange.exe

Linux or WSL (Ubuntu):
  gcc -std=c11 -Wall -Wextra -pedantic -O2 main.c structures.c engine.c data_gen.c -o exchange
  ./exchange

Automated Tests
---------------
The engine includes built-in unit tests to verify the data structures and matching logic:

  ./exchange --test-all
  ./exchange --test-btree
  ./exchange --test-fifo
  ./exchange --test-heap
  ./exchange --test-matching
  ./exchange --test-expiration

How Matching Works
------------------
1. Validation: Checks for valid positive price, quantity, and ensures the Order ID is not already active.
2. TTL Expiration Sweep: Before matching, the engine checks the Leftist Heap against the current monotonic clock and removes any expired resting orders.
3. Price Crossing Check:
   - For an incoming BUY: Checks the lowest available ask (min of SELL B-Tree). If buy price is greater than or equal to sell price, a match occurs.
   - For an incoming SELL: Checks the highest available bid (max of BUY B-Tree). If sell price is less than or equal to buy price, a match occurs.
4. Execution:
   - The trade executes at the resting order price (the price already posted in the book).
   - Quantity traded is the minimum of incoming quantity and resting quantity.
   - The traded quantity is deducted from both orders.
   - The executed trade is appended to trades.log.
5. Book Update:
   - If a resting order reaches 0 quantity, it is dequeued and freed.
   - If all orders at that price level are filled, the price level is deleted from the B-Tree.
   - If the incoming order still has remaining quantity after checking all crossing prices, the unfilled remainder is inserted into its respective B-Tree book.

Menu Options
------------
When launched, the program displays an interactive menu:

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

- Option 1: Generates 50 simulated orders into orders_feed.txt.
- Option 2: Reads and processes orders sequentially from orders_feed.txt.
- Option 3: Allows entering custom orders by entering Type (BUY/SELL), Price, Quantity, and TTL.
- Option 4 and 5: Displays active buy (descending) and sell (ascending) price levels, with an option to see individual order details.
- Option 6: Looks up any active order by its ID.
- Option 7: Cancels an open order by ID and removes it from the book.
- Option 8: Prints the table of executed trades from trades.log.
- Option 9: Displays runtime metrics (total orders, trades, throughput, volume, open orders).
- Option 10: Runs the complete simulation from start to finish and prints final statistics.
- Option 0: Frees all allocated memory and exits cleanly.

Example Manual Test
-------------------
1. Choose 3 (Submit Order Manually)
   - Type: SELL, Price: 6205000, Quantity: 10, TTL: 10000, ID: 5001
   - Result: Order accepted into SELL book.
2. Choose 3 (Submit Order Manually)
   - Type: BUY, Price: 6205000, Quantity: 4, TTL: 10000, ID: 5002
   - Result: Trade executed! BUY 5002 matches SELL 5001 for 4 units at 6205000. SELL 5001 now has 6 units remaining.
3. Choose 5 to view the SELL book (shows price 6205000 with 6 units).
4. Choose 8 to view the trade in the trade history table.
