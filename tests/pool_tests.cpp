/////////////////////////////////////////////////////////////////////////////////////
//
// Created by Ricardo Romero on 26/01/23.
// Copyright (c) 2023 Ricardo Romero.  All rights reserved.
//



#include "../allocator/allocator.hpp"
#include "../allocator/pool_concept.hpp"
#include "../allocator/pool_reporter.hpp"
#include <catch2/benchmark/catch_benchmark_all.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <iostream>
#include <random>
#include <vector>


TEST_CASE("Initialize Memory pool", "")
{
    using namespace Catch::Matchers;
    constexpr auto intSize = sizeof(int);

    SECTION("Throws chunk fitting ")
    {
        CHECK_THROWS_WITH((pool::memory_pool<int>(intSize * 8, 5)), ContainsSubstring("must fit"));
    }
    SECTION("Throws sizeof(chunk) < sizeof(void *)")
    {
        CHECK_THROWS_WITH((pool::memory_pool<int>(intSize * 8, 2)), ContainsSubstring("at least"));
    }
}

TEST_CASE("Memory free inside block")
{
    using namespace Catch::Matchers;
    pool::memory_pool<int> pool(4096, 8);
    int *i0 = new int;
    CHECK_THROWS_WITH(pool.release(i0), ContainsSubstring("does not belong"));
    delete i0;
}

TEST_CASE("Memory data integrity and release")
{
    pool::memory_pool<int> pool(4096, 8);
    int *i0 = pool.alloc();

    REQUIRE(i0 != nullptr);
    *i0 = 0x6989aabb;

    int *i1 = i0;
    CHECK(*i1 == 0x6989aabb);

    CHECK_NOTHROW(pool.release(i0));
    CHECK(i0 == nullptr);
}

TEST_CASE("Arguments passed to object via alloc")
{
    struct args
    {
        explicit args(uint64_t i0, uint64_t i1, uint64_t i2, std::string s) :
            _i0 { i0 },
            _i1 { i1 },
            _i2 { i2 },
            _s { std::move(s) }
        {
        }

        uint64_t _i0;
        uint64_t _i1;
        uint64_t _i2;

        std::string _s;
    };

    pool::memory_pool<args> pool(4096, 8);
    args *a0 = pool.alloc(0x45ull, 0x32ull, 0x10ull, "test string");

    REQUIRE(a0->_i0 == 0x45ull);
    REQUIRE(a0->_i1 == 0x32ull);
    REQUIRE(a0->_i2 == 0x10ull);
    REQUIRE(a0->_s == "test string");

    args *a1 = pool.alloc(0x4454ull, 0x31232ull, 0x123320ull, "test second string");

    CHECK(a1 != a0);

    REQUIRE(a1->_i0 == 0x4454ull);
    REQUIRE(a1->_i1 == 0x31232ull);
    REQUIRE(a1->_i2 == 0x123320ull);
    REQUIRE(a1->_s == "test second string");

    CHECK_NOTHROW(pool.release(a0));
    CHECK_NOTHROW(pool.release(a1));
    CHECK(a0 == nullptr);
    CHECK(a1 == nullptr);
}

TEST_CASE("Check block count and value integrity across multiple allocations and blocks")
{
    pool::memory_pool<uint64_t> pool(4096, 8);

    std::vector<std::pair<uint64_t *, uint64_t>> addressMap;
    for (uint64_t a = 0; a < 2048; ++a)
    {
        uint64_t *ptr = pool.alloc(a);
        CHECK(*ptr == a);
        addressMap.emplace_back(ptr, a);

        for (const auto &[p, v] : addressMap)
        {
            // CHECK THAT VALUES HAVE NOT BEEN OVERWRITTEN
            REQUIRE(*p == v);
        }
    }
    REQUIRE(pool.block_count() == 4);

    // Release the first 512 values
    for (int i = 0; i < 512; ++i)
    {
        auto iter     = addressMap.begin();
        auto *pointer = iter->first;
        CHECK_NOTHROW(pool.release(pointer));
        addressMap.erase(addressMap.begin());
    }
    REQUIRE(pool.block_count() == 3);

    for (auto &[p, v] : addressMap)
    {
        // CHECK THAT VALUES HAVE NOT BEEN OVERWRITTEN
        REQUIRE(*p == v);

        auto key = p;

        pool.release(key);
    }
}

TEST_CASE("Information integrity", "[integrity]")
{
    pool::memory_pool<uint64_t> pool(4096, 8);

    auto avai_space = 4096ull;
    auto used_space = 0ull;

    auto avai_chunks = 512ull;
    auto used_chunks = 0ull;

    std::vector<uint64_t *> p64;
    for (uint64_t a = 0; a < 512; ++a)
    {
        uint64_t *ptr = pool.alloc(a);
        p64.emplace_back(ptr);
        CHECK(*ptr == a);

        avai_space -= 8;
        used_space += 8;

        --avai_chunks;
        ++used_chunks;

        CHECK(pool.available_chunks_in_block(ptr) == avai_chunks);
        CHECK(pool.used_chunks_in_block(ptr) == used_chunks);
        CHECK(pool.available_space_in_block(ptr) == avai_space);
        CHECK(pool.used_space_in_block(ptr) == used_space);
    }
    REQUIRE(pool.block_count() == 1);

    for (auto &p : p64)
        pool.release(p);
}

TEST_CASE("Free list integrity")
{
    constexpr size_t chunkSize = 8;
    // These parameters make 8 chunks
    pool::memory_pool<uint8_t> pool(4096 * 5, chunkSize);

    uint8_t *beg = pool.block_address(nullptr);

    constexpr size_t elements = 4096 * 5 / chunkSize;
    std::array<uint8_t *, elements + 1> addresses {};

    for (size_t i = 0; i < elements; ++i)
        addresses[i] = beg + (chunkSize * i);
    addresses[elements] = nullptr;

    auto freeList = pool.dump_free_list(addresses[0]);

    SECTION("Empty list with no previous allocations made")
    {
        // These will check the free list integrity of an empty list without any allocations previously made
        // If allocations were made, there are no guarantees this check will pass the test
        auto index = 0ull;
        for (const auto &[free, next] : freeList)
        {
            REQUIRE(free == addresses[index]);
            REQUIRE(next == addresses[++index]);
        }
    }

    SECTION("Sequential allocation")
    {
        // Allocate
        std::vector<uint8_t *> p8;
        for (size_t i = 0; i < elements; ++i)
        {
            // The allocator allocates sequentially not randomly so these check is valid
            auto *p = pool.alloc();
            p8.emplace_back(p);
            REQUIRE(p == addresses[i]);
        }

        freeList = pool.dump_free_list(addresses[0]);
        REQUIRE(freeList.empty());

        for (auto &p : p8)
            pool.release(p);
    }

    std::random_device rd;
    std::mt19937 mt(rd());
    std::uniform_int_distribution<size_t> index(0, elements - 1);

    SECTION("Free list integrity. one-element released")
    {
        std::vector<uint8_t *> p8;
        for (size_t i = 0; i < elements; ++i)
        {
            // The allocator allocates sequentially not randomly so this check is valid
            auto *p = pool.alloc();
            p8.emplace_back(p);
        }

        for (int i = 0; i < 1024; ++i)
        {
            const auto delIndex = index(mt);

            // On other circumstances, this should be considered unsafe because the original pointer will still have the address.
            // however, once released the value at the address is going to be set to the freelist, making the original pointer unsafe to use

            auto *prevRelease  = addresses[delIndex];
            auto *checkAddress = prevRelease;
            pool.release(prevRelease);

            freeList = pool.dump_free_list(addresses[0]);

            REQUIRE(freeList.size() == 1);
            REQUIRE(prevRelease == nullptr);
            REQUIRE(freeList[0].first == checkAddress);
            REQUIRE(freeList[0].second == nullptr);

            // Reallocate so the pool can have only one chunk available once release is called
            REQUIRE(checkAddress == pool.alloc());
        }

        for (auto &p : p8)
            pool.release(p);
    }

    SECTION("Integrity of the freelist with multiple releases")
    {

        for (int i = 0; i < 3; ++i)
        {
            // Reallocate all
            for (size_t n = 0; n < elements; ++n)
                [[maybe_unused]] auto *p = pool.alloc();

            // Generate a random path of indexes in which we include the indexes from 0 to 7 without repeating ourselves
            std::array<size_t, elements> path {};
            for (size_t k = 0; k < elements; ++k)
                path[k] = k;
            std::shuffle(path.begin(), path.end(), mt);

            size_t at = 1;
            for (const auto &indexPath : path)
            {
                auto *freePtr = addresses[indexPath];
                pool.release(freePtr);
                freeList = pool.dump_free_list(addresses[0]);

                REQUIRE(freeList.size() == at);

                for (size_t k = 0; k < at; ++k)
                {
                    REQUIRE(freeList[k].first == addresses[path[at - 1 - k]]);
                    if (k == at - 1)
                        REQUIRE(freeList[k].second == nullptr);
                    else
                        REQUIRE(freeList[k].second == addresses[path[at - 2 - k]]);
                }
                ++at;
            }
        }
    }
}

TEST_CASE("Multiple pools")
{
    pool::memory_pool<size_t> pool(4096, 1024);

    size_t *_1_pool0 = pool.alloc<size_t>(4);
    size_t *_2_pool0 = pool.alloc<size_t>(44);
    size_t *_3_pool0 = pool.alloc<size_t>(434);
    size_t *_4_pool0 = pool.alloc<size_t>(453764);
    size_t *_1_pool1 = pool.alloc<size_t>(4537664);
    size_t *_2_pool1 = pool.alloc<size_t>(4537661224);
    size_t *_3_pool1 = pool.alloc<size_t>(453766124);
    size_t *_4_pool1 = pool.alloc<size_t>(45376614);
    size_t *_1_pool2 = pool.alloc<size_t>(453764);
    size_t *_2_pool2 = pool.alloc<size_t>(4534);
    size_t *_3_pool2 = pool.alloc<size_t>(454);
    size_t *_4_pool2 = pool.alloc<size_t>(4);

    REQUIRE(pool.block_count() == 3);

    CHECK(pool.available_chunks_in_block(_1_pool0) == 0);
    CHECK_NOTHROW(pool.release(_2_pool0));
    CHECK(pool.available_chunks_in_block(_3_pool0) == 1);
    CHECK_NOTHROW(pool.release(_4_pool0));
    CHECK(pool.available_chunks_in_block(_3_pool0) == 2);

    CHECK(pool.available_chunks_in_block(_1_pool1) == 0);
    CHECK_NOTHROW(pool.release(_2_pool1));
    CHECK(pool.available_chunks_in_block(_3_pool1) == 1);
    CHECK_NOTHROW(pool.release(_4_pool1));
    CHECK(pool.available_chunks_in_block(_3_pool1) == 2);

    CHECK(pool.available_chunks_in_block(_1_pool2) == 0);
    CHECK_NOTHROW(pool.release(_2_pool2));
    CHECK(pool.available_chunks_in_block(_3_pool2) == 1);
    CHECK_NOTHROW(pool.release(_4_pool2));
    CHECK(pool.available_chunks_in_block(_3_pool2) == 2);

    CHECK_NOTHROW(pool.release(_1_pool2));
    CHECK_NOTHROW(pool.release(_3_pool2));
    REQUIRE(pool.block_count() == 2);

    CHECK_NOTHROW(pool.release(_1_pool1));
    CHECK_NOTHROW(pool.release(_3_pool1));
    REQUIRE(pool.block_count() == 1);

    CHECK_NOTHROW(pool.release(_1_pool0));
    CHECK_NOTHROW(pool.release(_3_pool0));
    REQUIRE(pool.block_count() == 1);
    REQUIRE(pool.available_chunks_in_block(reinterpret_cast<size_t *>(pool.block_address(nullptr))) == 4);
}

TEST_CASE("Benchmarking")
{
    constexpr size_t chunkSize = 8;
    // These parameters make 8 chunks
    pool::memory_pool<size_t> pool(4096 * 20, chunkSize);

    size_t n = 0;

    BENCHMARK_ADVANCED("Object creation/destruction in pool")
    (Catch::Benchmark::Chronometer meter)
    {
        std::vector<size_t *> poolObject;
        poolObject.reserve(300000);

        size_t *ptr;

        meter.measure([&] {
            for (int i = 0; i < 10'000; ++i)
            {
                ptr = pool.alloc(++n);
                poolObject.push_back(ptr);
            }
            for (auto &p : poolObject)
            {
                pool.release(p);
            }
        });
    };

    BENCHMARK_ADVANCED("Object creation/destruction using new/delete")
    (Catch::Benchmark::Chronometer meter)
    {
        std::vector<size_t *> systemObject;
        systemObject.reserve(300000);
        size_t *ptr;

        meter.measure([&] {
            for (int i = 0; i < 10'000; ++i)
            {
                ptr = new size_t(++n);
                systemObject.push_back(ptr);
            }
            for (auto &p : systemObject)
            {
                delete p;
                p = nullptr;
            }
        });
    };
}


#if defined REPORT_ALLOCATIONS && defined CHECK_MEMORY_LEAK
template<typename T>
using pool_iostream_reporter = pool::pool_allocator<T, pool::allocator_iostream_reporter, pool::pool_iostream_reporter>;
#elif !defined (REPORT_ALLOCATIONS) && defined CHECK_MEMORY_LEAK
template<typename T>
using pool_iostream_reporter = pool::pool_allocator<T, pool::pool_iostream_reporter>;
#else
template<typename T>
using pool_iostream_reporter = pool::pool_allocator<T>;
#endif /*REPORT_ALLOCATIONS*/


TEST_CASE("String allocator")
{

    std::basic_string<char, std::char_traits<char>, pool_iostream_reporter<char>> string_prevent;

    BENCHMARK_ADVANCED("String allocator benchmark")
    (Catch::Benchmark::Chronometer meter)
    {

        meter.measure([&] {
            std::basic_string<char, std::char_traits<char>, pool_iostream_reporter<char>> string0;
            string0 = "string0 string0 string0";
            string0 = "string0 string0 string0 string0 string0 string0 string0 string0 string0";
            std::basic_string<char, std::char_traits<char>, pool_iostream_reporter<char>> string1;
            string1 = "string1 string1 string1";
            string1 = "string1 string1 string1 string1 string1 string1 string1 string1 string1";
            std::basic_string<char, std::char_traits<char>, pool_iostream_reporter<char>> string2;
            string2 = "string2 string2 string2";
            string2 = "string2 string2 string2 string2 string2 string2 string2 string2 string2";
            std::basic_string<char, std::char_traits<char>, pool_iostream_reporter<char>> string3;
            string3 = "string3 string3 string3";
            string3 = "string3 string3 string3 string3 string3 string3 string3 string3 string3";
        });
    };

    BENCHMARK_ADVANCED("String default allocator benchmark")
    (Catch::Benchmark::Chronometer meter)
    {
        meter.measure([&] {
            std::string string0;
            string0 = "string0 string0 string0";
            string0 = "string0 string0 string0 string0 string0 string0 string0 string0 string0";
            std::string string1;
            string1 = "string1 string1 string1";
            string1 = "string1 string1 string1 string1 string1 string1 string1 string1 string1";
            std::string string2;
            string2 = "string2 string2 string2";
            string2 = "string2 string2 string2 string2 string2 string2 string2 string2 string2";
            std::string string3;
            string3 = "string3 string3 string3";
            string3 = "string3 string3 string3 string3 string3 string3 string3 string3 string3";
        });
    };
}

TEST_CASE("Allocator vector")
{
    using wbstring = std::basic_string<char, std::char_traits<char>, pool_iostream_reporter<char>>;
    std::vector<wbstring, pool_iostream_reporter<wbstring>> sv;
    std::vector<uint32_t, pool_iostream_reporter<uint32_t>> u32v;
    std::vector<uint64_t, pool_iostream_reporter<uint64_t>> u64v;

    sv.emplace_back("string0 string0 string0");
    sv.emplace_back("string1 string1 string1");
    sv.emplace_back("string2 string2 string2");
    sv.emplace_back("string3 string3 string3");
    sv.emplace_back("string4 string4 string4");

    u32v.push_back(0xddffbbccu);
    u32v.push_back(0xaaffbbccu);
    u32v.push_back(0xbbffbbccu);

    u64v.push_back(0xddffbbccddffbbccull);
    u64v.push_back(0xaaffbbccddffbbccull);
    u64v.push_back(0xbbffbbccddffbbccull);

    CHECK(sv[0] == "string0 string0 string0");
    CHECK(sv[1] == "string1 string1 string1");
    CHECK(sv[2] == "string2 string2 string2");
    CHECK(sv[3] == "string3 string3 string3");
    CHECK(sv[4] == "string4 string4 string4");


    CHECK(u32v[0] == 0xddffbbccu);
    CHECK(u32v[1] == 0xaaffbbccu);
    CHECK(u32v[2] == 0xbbffbbccu);

    CHECK(u64v[0] == 0xddffbbccddffbbccull);
    CHECK(u64v[1] == 0xaaffbbccddffbbccull);
    CHECK(u64v[2] == 0xbbffbbccddffbbccull);
}
