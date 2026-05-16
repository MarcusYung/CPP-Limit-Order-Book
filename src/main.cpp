// src/main.cpp
#include "orderbook.h"
#include <iostream>
#include <cassert>
#include <stdexcept>

using namespace MySTLOB;

void separator(const std::string& title) {
    std::cout << "\n========================================\n";
    std::cout << "  " << title << "\n";
    std::cout << "========================================\n";
}

int main() {

    separator("TEST 1: Resting orders (no match)");
    {
        OrderBook book;
        book.add_order(1, 100, 99,  Side::BUY);
        book.add_order(2, 150, 98,  Side::BUY);
        book.add_order(3, 200, 97,  Side::BUY);
        book.add_order(4, 100, 101, Side::SELL);
        book.add_order(5, 150, 102, Side::SELL);
        book.add_order(6, 200, 103, Side::SELL);
        book.printBook();

        assert(book.bestBid() == 99);
        assert(book.bestAsk() == 101);
        assert(book.hasOrder(1) && book.hasOrder(6));
        std::cout << "[PASS]\n";
    }

    // --------------------------------------------------
    // TEST 2: Full fill — incoming exactly matches resting
    // --------------------------------------------------
    separator("TEST 2: Full fill");
    {
        OrderBook book;
        book.add_order(1, 100, 101, Side::SELL);
        std::cout << "Before:\n";
        book.printBook();

        book.add_order(2, 100, 101, Side::BUY);
        std::cout << "\nAfter buy 100 @ 101:\n";
        book.printBook();

        assert(!book.bestBid().has_value());   // incoming fully filled, never rested
        assert(!book.bestAsk().has_value());   // resting order consumed
        assert(!book.hasOrder(1));
        assert(!book.hasOrder(2));
        std::cout << "[PASS]\n";
    }

    // --------------------------------------------------
    // TEST 3: Partial fill — incoming is smaller than resting
    // --------------------------------------------------
    separator("TEST 3: Partial fill (incoming smaller than resting)");
    {
        OrderBook book;
        book.add_order(1, 200, 101, Side::SELL); // resting sell 200 @ 101
        book.add_order(2,  50, 101, Side::BUY);  // buy 50 @ 101 — partial fill
        book.printBook();

        assert(book.bestAsk() == 101);  // level still exists
        assert(book.hasOrder(1));       // resting order partially filled, still alive
        assert(!book.hasOrder(2));      // incoming fully consumed, never rested
        std::cout << "[PASS]\n";
    }

    // --------------------------------------------------
    // TEST 4: Multi-level sweep
    // --------------------------------------------------
    separator("TEST 4: Multi-level sweep");
    {
        OrderBook book;
        book.add_order(1, 100, 101, Side::SELL);
        book.add_order(2, 100, 102, Side::SELL);
        book.add_order(3, 100, 103, Side::SELL);
        std::cout << "Before sweep:\n";
        book.printBook();

        // Buy 250 @ 103 — sweeps 101 fully, 102 fully, 103 partially (50 left)
        book.add_order(4, 250, 103, Side::BUY);
        std::cout << "\nAfter buy 250 @ 103:\n";
        book.printBook();

        assert(!book.hasOrder(1));
        assert(!book.hasOrder(2));
        assert(!book.hasOrder(4));        // fully consumed, never rested
        assert(book.hasOrder(3));         // 103 level partially filled, still alive
        assert(book.bestAsk() == 103);    // 103 level remains with 50 quantity
        std::cout << "[PASS]\n";
    }

    // --------------------------------------------------
    // TEST 5: Cancel order
    // --------------------------------------------------
    separator("TEST 5: Cancel order");
    {
        OrderBook book;
        book.add_order(1, 100, 99, Side::BUY);
        book.add_order(2, 100, 98, Side::BUY);
        std::cout << "Before cancel:\n";
        book.printBook();

        bool cancelled = book.cancel_order(1);
        std::cout << "\nAfter cancelling order 1:\n";
        book.printBook();

        assert(cancelled == true);
        assert(!book.hasOrder(1));
        assert(book.bestBid() == 98);

        bool bad_cancel = book.cancel_order(999); // non-existent order
        assert(bad_cancel == false);
        std::cout << "[PASS]\n";
    }

    // --------------------------------------------------
    // TEST 6: Duplicate order ID rejection
    // --------------------------------------------------
    separator("TEST 6: Duplicate order ID rejection");
    {
        OrderBook book;
        book.add_order(1, 100, 99, Side::BUY);
        try {
            book.add_order(1, 50, 100, Side::SELL);
            std::cout << "[FAIL] Should have thrown\n";
            return 1;
        } catch (const std::runtime_error& e) {
            std::cout << "Caught expected exception: \"" << e.what() << "\"\n";
            std::cout << "[PASS]\n";
        }
    }

    std::cout << "\n========================================\n";
    std::cout << "  All tests passed!\n";
    std::cout << "========================================\n";
    return 0;
}