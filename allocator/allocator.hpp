/////////////////////////////////////////////////////////////////////////////////////
//
// Created by Ricardo Romero on 10/12/22.
// Copyright (c) 2022 Ricardo Romero.  All rights reserved.
//

#pragma once

#ifndef __cplusplus
#error "C++ compiler needed"
#endif /*__cplusplus*/

#ifndef WBSCRP_STRING_ALLOCATOR_HPP
#define WBSCRP_STRING_ALLOCATOR_HPP

#include "memory_pool.hpp"
#include <bit>
#include <cassert>
#include <mutex>
#include <unordered_map>

namespace pool
{
#if defined(REPORT_ALLOCATIONS) && defined(CHECK_MEMORY_LEAK)
    template <allocator_reporter R, pool_reporter P>
#elif !defined(REPORT_ALLOCATIONS) && defined(CHECK_MEMORY_LEAK)
    template <pool_reporter P>
#endif
    struct global_allocator final
    {
#ifdef REPORT_ALLOCATIONS
        using reporter_type = R;
#endif /*REPORT_ALLOCATIONS*/

#if defined(REPORT_ALLOCATIONS) && defined(CHECK_MEMORY_LEAK)
        using pool_type   = pool::memory_pool<void, P, false>; // The global pool will allocate actually void * and handle different blocks for different chunk sizes
        using global_pool = pool::memory_pool<pool_type, P, false>;
        using this_type   = global_allocator<R, P>;
#elif !defined(REPORT_ALLOCATIONS) && defined(CHECK_MEMORY_LEAK)
        using pool_type   = pool::memory_pool<void, P, false>; // The global pool will allocate actually void * and handle different blocks for different chunk sizes
        using global_pool = pool::memory_pool<pool_type, P, false>;
        using this_type   = global_allocator<P>;
#else
        using pool_type   = pool::memory_pool<void, false>; // The global pool will allocate actually void * and handle different blocks for different chunk sizes
        using global_pool = pool::memory_pool<pool_type, false>;
        using this_type   = global_allocator;
#endif /*REPORT_ALLOCATIONS*/

        static constexpr auto pool_type_size_adjusted = static_cast<std::size_t>(static_cast<int>(2 << (std::bit_width(sizeof(pool_type)) - 1)));

        global_allocator() :
            global_block(32768, pool_type_size_adjusted)
        {
        }

#ifdef REPORT_ALLOCATIONS
        ~global_allocator()
        {
            _reporter.global_freed(this);
        }
#endif /*REPORT_ALLOCATIONS*/

        auto create_pool(std::size_t size, std::size_t chunkSize) noexcept -> auto
        {
            auto find = local_blocks.find(chunkSize);
            if (find == local_blocks.end())
            {
                local_blocks[chunkSize] = global_block.template alloc(size, chunkSize);
                return local_blocks.find(chunkSize);
            }

            return find;
        }

        auto allocate(std::size_t n) -> void *
        {
            std::unique_lock<std::mutex> lock(thread_protection);

            std::size_t chunk_size = this_type::adjust_chunk_size(n);

            auto pool = create_pool(
                this_type::usable_size_from_chunk_size(chunk_size),
                chunk_size);

            // The [ ] operator won't create pools because create_pool will do it if it doesn't exist
            return pool->second->template alloc();
        }


        auto deallocate(void *p, std::size_t chunkSize) -> void
        {
            std::unique_lock<std::mutex> lock(thread_protection);

            auto find = local_blocks.find(chunkSize);
            if (find != local_blocks.end())
            {
                find->second->release(p);
            }
        }



    public:
        static constexpr auto adjust_chunk_size(std::size_t chunkSize) noexcept -> std::size_t
        {
            std::size_t chunk_size;
            if (chunkSize < 8)
                chunk_size = 8;
            else
            {
                const auto bw = std::bit_width(chunkSize);
                chunk_size    = static_cast<std::size_t>(static_cast<int>(2 << (bw - 1)));
            }

            return chunk_size;
        }

        static constexpr auto usable_size_from_chunk_size(std::size_t chunkSize) noexcept -> std::size_t
        {
            auto usableSize = chunkSize * 1000;

            if (usableSize > (2 << 19))
                usableSize = chunkSize * 1000;

            return usableSize;
        }

#ifdef REPORT_ALLOCATIONS
        MP_NODISCARD const reporter_type &reporter() const noexcept
        {
            return _reporter;
        }

    private:
        reporter_type _reporter;
#endif /*REPORT_ALLOCATIONS*/

    private:
        int64_t count_ref { 0 };
        std::mutex thread_protection;
        global_pool global_block;
        std::unordered_map<std::size_t, pool_type *> local_blocks;

#if defined(REPORT_ALLOCATIONS) && defined(CHECK_MEMORY_LEAK)
        template <typename T, typename C, typename K>
        friend struct pool_allocator;

    public:
        static global_allocator<R, P> *_global;
#elif !defined(REPORT_ALLOCATIONS) && defined(CHECK_MEMORY_LEAK)
        template <typename T, typename K>
        friend struct pool_allocator;

    public:
        static global_allocator<P> *_global;
#else
        template <typename T>
        friend struct pool_allocator;

    public:
        static global_allocator *_global;
#endif
    };

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wglobal-constructors"
#pragma GCC diagnostic ignored "-Wexit-time-destructors"
#endif
    static std::mutex _construct_mutex;
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#if defined(REPORT_ALLOCATIONS) && defined(CHECK_MEMORY_LEAK)
    template <typename T, typename R, typename P>
    struct pool_allocator
#elif !defined(REPORT_ALLOCATIONS) && defined(CHECK_MEMORY_LEAK)
    template <typename T, typename P>
    struct pool_allocator
#else
    template <typename T>
    struct pool_allocator
#endif
    {
        using value_type = T;

#if defined(REPORT_ALLOCATIONS) && defined(CHECK_MEMORY_LEAK)
        using allocator_reporter_type = R;
        using pool_reporter_type      = P;
        using global_allocator        = global_allocator<allocator_reporter_type, pool_reporter_type>;
#elif !defined(REPORT_ALLOCATIONS) && defined(CHECK_MEMORY_LEAK)
        using pool_reporter_type = P;
        using global_allocator   = global_allocator<pool_reporter_type>;
#else
        using global_allocator = global_allocator;
#endif

    private:
        auto initialize_pool() -> void
        {
            std::unique_lock<std::mutex> lock(_construct_mutex);
            if (global_allocator::_global == nullptr)
            {
                global_allocator::_global = new global_allocator;
#ifdef REPORT_ALLOCATIONS
                global_allocator::_global->reporter().global_new(global_allocator::_global);
#endif
            }
            ++global_allocator::_global->count_ref;
#ifdef REPORT_ALLOCATIONS
            global_allocator::_global->reporter().add_ref_count(global_allocator::_global->count_ref);
#endif
        }

    public:
        auto create_pool(std::size_t chunk_size) -> void
        {
            global_allocator::_global->create_pool(
                global_allocator::usable_size_from_chunk_size(chunk_size),
                chunk_size);
        }

        pool_allocator()
        {
            initialize_pool();
        }

        ~pool_allocator()
        {
            std::unique_lock<std::mutex> lock(_construct_mutex);
            if (global_allocator::_global)
            {

                --global_allocator::_global->count_ref;

#ifdef REPORT_ALLOCATIONS
                global_allocator::_global->reporter().sub_ref_count(global_allocator::_global->count_ref);
#endif /*REPORT_ALLOCATIONS*/

                if (global_allocator::_global->count_ref <= 0)
                {
                    // Since std::unordered_map will try to call the destructor of an allocated block
                    // of a fixed_memory_pool chunk, this will cause a big set of troubles if we do not call release of each pool first
                    for (auto &[chunk, block] : global_allocator::_global->local_blocks)
                    {
                        global_allocator::_global->global_block.release(block);
                    }

                    delete global_allocator::_global;
                    global_allocator::_global = nullptr;
                }
            }
        }

#if defined(REPORT_ALLOCATIONS) && defined(CHECK_MEMORY_LEAK)
        pool_allocator(const pool_allocator<T, R, P> &) noexcept
#elif !defined(REPORT_ALLOCATIONS) && defined(CHECK_MEMORY_LEAK)
        pool_allocator(const pool_allocator<T, P> &) noexcept
#else
        pool_allocator(const pool_allocator<T> &) noexcept
#endif
        {
            std::unique_lock<std::mutex> lock(_construct_mutex);
            assert(global_allocator::_global != nullptr);
            ++global_allocator::_global->count_ref;
#ifdef REPORT_ALLOCATIONS
            global_allocator::_global->reporter().copy_ctor_ref_count(global_allocator::_global->count_ref);
#endif /*REPORT_ALLOCATIONS*/
        }

#if defined(REPORT_ALLOCATIONS) && defined(CHECK_MEMORY_LEAK)
        pool_allocator(pool_allocator<T, R, P> &&) noexcept
        {
            std::unique_lock<std::mutex> lock(_construct_mutex);
            assert(global_allocator::_global != nullptr);
            ++global_allocator::_global->count_ref;
#ifdef REPORT_ALLOCATIONS
            global_allocator::_global->reporter().move_ctor_ref_count(global_allocator::_global->count_ref);
#endif /*REPORT_ALLOCATIONS*/
        }
#elif !defined(REPORT_ALLOCATIONS) && defined(CHECK_MEMORY_LEAK)
        pool_allocator(pool_allocator<T, P> &&) noexcept
        {
            std::unique_lock<std::mutex> lock(_construct_mutex);
            assert(global_allocator::_global != nullptr);
            ++global_allocator::_global->count_ref;
#ifdef REPORT_ALLOCATIONS
            global_allocator::_global->reporter().move_ctor_ref_count(global_allocator::_global->count_ref);
#endif /*REPORT_ALLOCATIONS*/
        }
#else
        pool_allocator(pool_allocator<T> &&) noexcept
        {
            std::unique_lock<std::mutex> lock(_construct_mutex);
            assert(global_allocator::_global != nullptr);
            ++global_allocator::_global->count_ref;
#ifdef REPORT_ALLOCATIONS
            global_allocator::_global->reporter().move_ctor_ref_count(global_allocator::_global->count_ref);
#endif /*REPORT_ALLOCATIONS*/
        }
#endif

        MP_NODISCARD auto allocate(std::size_t n) -> value_type *
        {
            assert(global_allocator::_global != nullptr);

            if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
                throw std::bad_array_new_length();

#ifdef REPORT_ALLOCATIONS
            global_allocator::_global->reporter().alloc_request(n * sizeof(T));
#endif /*REPORT_ALLOCATIONS*/

            if (auto *t = reinterpret_cast<value_type *>(global_allocator::_global->allocate(n * sizeof(value_type))); t)
            {
                return t;
            }

            throw std::bad_alloc();
        }

        auto deallocate(value_type *p, std::size_t _n) noexcept -> void
        {
            const std::size_t n = _n * sizeof(value_type);
#ifdef REPORT_ALLOCATIONS
            global_allocator::_global->reporter().dealloc_request(reinterpret_cast<void *>(p), n);
#endif /*REPORT_ALLOCATIONS*/

            std::size_t chunk_size;
            if (n < 8)
                chunk_size = 8;
            else
            {
                const auto bw = std::bit_width(n);
                chunk_size    = static_cast<std::size_t>(static_cast<int>(2 << (bw - 1)));
            }

            global_allocator::_global->deallocate(p, chunk_size);
        }

        static auto get_global_allocator() -> auto
        {
            return global_allocator::_global;
        }
    };


#if defined(REPORT_ALLOCATIONS) && defined(CHECK_MEMORY_LEAK)
    template <class T, class U, typename R, typename P>
    bool operator==(const pool_allocator<T, R, P> &, const pool_allocator<U, R, P> &)
    {
        return true;
    }

    template <class T, class U, typename R, typename P>
    bool operator!=(const pool_allocator<T, R, P> &, const pool_allocator<U, R, P> &)
    {
        return false;
    }
#elif !defined(REPORT_ALLOCATIONS) && defined(CHECK_MEMORY_LEAK)
    template <class T, class U, typename P>
    bool operator==(const pool_allocator<T, P> &, const pool_allocator<U, P> &)
    {
        return true;
    }

    template <class T, class U, typename P>
    bool operator!=(const pool_allocator<T, P> &, const pool_allocator<U, P> &)
    {
        return false;
    }
#else
    template <class T, class U>
    bool operator==(const pool_allocator<T> &, const pool_allocator<U> &)
    {
        return true;
    }

    template <class T, class U>
    bool operator!=(const pool_allocator<T> &, const pool_allocator<U> &)
    {
        return false;
    }
#endif

} // namespace pool

#if defined(REPORT_ALLOCATIONS) && defined(CHECK_MEMORY_LEAK)
template <pool::allocator_reporter T, pool::pool_reporter P>
inline pool::global_allocator<T, P> *pool::global_allocator<T, P>::_global = nullptr;
#elif !defined(REPORT_ALLOCATIONS) && defined(CHECK_MEMORY_LEAK)
template <pool::pool_reporter P>
inline pool::global_allocator<P> *pool::global_allocator<P>::_global = nullptr;
#else
inline pool::global_allocator *pool::global_allocator::_global = nullptr;
#endif

#endif // WBSCRP_STRING_ALLOCATOR_HPP
