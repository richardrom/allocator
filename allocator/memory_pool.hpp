/////////////////////////////////////////////////////////////////////////////////////
//
// Created by Ricardo Romero on 10/12/22.
// Copyright (c) 2022 Ricardo Romero.  All rights reserved.
//

#pragma once

#ifndef __cplusplus
#error "C++ compiler needed"
#endif /*__cplusplus*/

#ifndef MEMPOOL_FIXPOOL_BLOCK_HPP
#define MEMPOOL_FIXPOOL_BLOCK_HPP

#include <bit>
#include <cstdint>
#include <memory>
#if defined(__APPLE__)
#include <unistd.h>
#elif defined(__linux__) || defined(__MINGW32__)
#include <unistd.h>
#elif defined(WIN32)
#include <sysinfoapi.h>
#include <windows.h>
#endif /**/

#include "pool_concept.hpp"

#if __cplusplus >= 201603L
#define MP_NODISCARD [[nodiscard]]
#else
#define MP_NODISCARD
#endif

namespace pool
{

#if defined(REPORT_ALLOCATIONS) || defined(CHECK_MEMORY_LEAK)
    template <typename T, pool_reporter P, bool dest=true>
#else
    template <typename T, bool dest=true>
#endif /*REPORT_ALLOCATIONS*/
    struct memory_pool
    {
    private:
        struct block final
        {
            explicit block(size_t blockSize, size_t chunk) :
                available_space { blockSize },
                available_chunks { blockSize / chunk }
            {
            }

            void *_block { nullptr };

            size_t available_space { 0 };
            size_t used_space { 0ull };
            size_t available_chunks { 0 };
            size_t used_chunks { 0ull };

            size_t *next_free_chunk { nullptr };
            uint8_t *block_beginning { nullptr };
            uint8_t *block_end { nullptr };

#ifdef _DEBUG
            // Just for debugging purposes
            [[maybe_unused]] size_t *block_beginning_ { nullptr };
#endif /*_DEBUG*/

            // Double linked list
            block *next_block { nullptr };
            block *previous_block { nullptr };
        } *first_block;

    public:
        explicit memory_pool(size_t blockSize, size_t chunk) :
            block_size { blockSize },
            chunk_size { chunk }
        {

            if (blockSize % chunk_size)
                throw std::runtime_error("chunk size must fit in the block size");
            if (!(chunk_size >= sizeof(void *)))
                throw std::runtime_error("chunk size must be at least the size of void *");

            allocate_block(&first_block, nullptr);
        }

        ~memory_pool()
        {
            block *next = first_block;
            while (next != nullptr)
            {
                block *currentBlock = next;

#if defined CHECK_MEMORY_LEAK || defined(REPORT_ALLOCATIONS)
                if (currentBlock->used_chunks > 0)
                {
                    std::vector<std::pair<uint64_t *, uint64_t *>> pv;
                    auto freeList = dump_free_list(reinterpret_cast<T *>(currentBlock->block_beginning));
                    for (const auto &[p0, p1] : freeList)
                        pv.template emplace_back(reinterpret_cast<uint64_t *>(p0), reinterpret_cast<uint64_t *>(p1));
                    reporter.check_memory_leaks(currentBlock->block_beginning, pv, currentBlock->available_chunks, currentBlock->used_chunks, currentBlock->available_space, currentBlock->used_space, chunk_size);
                }
#endif /*CHECK_MEMORY_LEAK*/

                next = currentBlock->next_block; // This is the next block
                // free the current block
                free_block(currentBlock);
            }
        }

    protected:
        void allocate_block(block **pBlock, block *previous)
        {
            *pBlock = new block(block_size, chunk_size); // Call the block constructor to initialize all the internal variables

            if (*pBlock == nullptr)
                throw std::runtime_error("block info: out of memory");

            (*pBlock)->_block = std::aligned_alloc(chunk_size, block_size);

            if ((*pBlock)->_block == nullptr)
                throw std::runtime_error("block: out of memory");

#ifdef REPORT_ALLOCATIONS
            reporter.allocate_block(*pBlock, block_size, chunk_size);
#endif /*REPORT_ALLOCATIONS*/

            memset((*pBlock)->_block, 0, block_size);

            // Init the free list
            (*pBlock)->next_free_chunk = static_cast<size_t *>((*pBlock)->_block);
            (*pBlock)->block_beginning = static_cast<uint8_t *>((*pBlock)->_block);
            (*pBlock)->block_end       = (*pBlock)->block_beginning + block_size;
            (*pBlock)->previous_block  = previous;

#ifdef _DEBUG
            // Just for debugging purposes
            (*pBlock)->block_beginning_ = static_cast<size_t *>((*pBlock)->_block);
#endif /*_DEBUG*/

            // Will use size_t * in order to be able to store addresses at the beginning of the block
            auto *currentChunk = reinterpret_cast<size_t *>((*pBlock)->_block);

            for (size_t n = 0; n < (*pBlock)->available_chunks; ++n)
            {

#ifdef CHECK_MEMORY_ALIGNMENT
                if (reinterpret_cast<uint64_t>(currentChunk) % sizeof(void *))
                    throw std::runtime_error("block not aligned"); // Check the free-list alignment
                if (reinterpret_cast<uint64_t>(currentChunk) % chunk_size)
                    throw std::runtime_error("block not aligned"); // Check the chunk-size alignment

#endif
                // Write the addresses of the available blocks which will point to the next chunk
                if (n == ((*pBlock)->available_chunks - 1))
                {
                    *currentChunk = reinterpret_cast<uint64_t>(nullptr); // Last block must be nullptr
                }
                else
                {
                    // nextChunk will be used to read the address of the next chunk
                    // Let's use uint8_t because if the data size is less than the pointer size this will truncate

                    uint8_t *nextChunk = reinterpret_cast<uint8_t *>((*pBlock)->next_free_chunk) + (chunk_size * (n + 1));

                    *currentChunk = reinterpret_cast<size_t>(nextChunk);
                    currentChunk  = reinterpret_cast<size_t *>(nextChunk);
                }
            }
        }

        void free_block(block *pBlock)
        {
            auto _free = [](auto *ptr) {
                if (ptr)
                {
                    if constexpr (std::is_same_v<decltype(ptr), block *>)
                        delete ptr;
                    else
                        free(ptr);
                }
            };

            _free(pBlock->_block);
            _free(pBlock);

#ifdef REPORT_ALLOCATIONS
            reporter.deallocate_block(pBlock, block_size, chunk_size);
#endif /*REPORT_ALLOCATIONS*/
        }

        block *block_from_pointer(T *ptr)
        {
            // Get the block of the current chunk
            block *next        = first_block;
            auto *freedAddress = reinterpret_cast<uint8_t *>(ptr);
            while (next != nullptr)
            {
                block *currentBlock = next;

                // This is the next block
                next = currentBlock->next_block;

                if (freedAddress >= currentBlock->block_beginning && freedAddress <= currentBlock->block_end)
                {
                    return currentBlock;
                }
            }

            throw std::out_of_range("block does not belong to the pool");
        }

    public:
        template <typename... Args>
        auto alloc(Args &&...args) -> T *
        {
            // Just return according to get_available_chunk
            if constexpr (std::is_same<void, T>::value)
                return new (get_available_chunk()) void *;
            else
                return new (get_available_chunk()) T(std::forward<Args>(args)...);
        }

        void release(T *&ptr)
        {
            if (ptr == nullptr)
                return;

            // Get the block of the current chunk
            block *used_block = block_from_pointer(ptr);

            // Update chunks
            --used_block->used_chunks;
            ++used_block->available_chunks;

            // Update size
            used_block->available_space += chunk_size;
            used_block->used_space -= chunk_size;

#ifdef REPORT_ALLOCATIONS
            reporter.dealloc_report(used_block, ptr, chunk_size, used_block->available_space, used_block->available_chunks, used_block->used_space, used_block->used_chunks);
#endif /*REPORT_ALLOCATIONS*/

            if (used_block->used_chunks == 0)
            {
                bool releaseUsedBlock = false;
                if (used_block->previous_block == nullptr) // This is the first block
                {
                    if (used_block->next_block != nullptr) // And we do not have any more block available
                    {
                        // So, make the next block the first block
                        first_block = used_block->next_block;
                        // Set the first_block previous block nullptr because that previous blocks points to used_block
                        // And we are going to free this block
                        first_block->previous_block = nullptr;
                        releaseUsedBlock            = true;
                    }
                }
                else
                {
                    // Update the next_block previous block to point to the previous block of the used block
                    if (used_block->next_block != nullptr)
                    {
                        used_block->previous_block->next_block = used_block->next_block;
                        used_block->next_block->previous_block = used_block->previous_block;
                    }
                    else
                    {
                        // Update the previous block -> next_block to zero because this is the one we are freeing
                        used_block->previous_block->next_block = nullptr;
                    }

                    releaseUsedBlock = true;
                }

                if (releaseUsedBlock)
                {
                    // Call the destructor before freeing the block
                    if constexpr (dest && std::is_destructible<T>::value && !std::is_trivially_destructible<T>::value)
                        ptr->~T();

                    // Do not free the block, we might scramble the available address
                    // But we are still in O(1) and without any system call when allocating a new chunk
                    free_block(used_block);

                    // Once freed set the pointer to nullptr
                    ptr = nullptr;
                    return; // We don't need the next code
                }
            }

            if (used_block->available_chunks == 1)
            {
                // In this situation used_block->next_free_chunk is going to be nullptr
                // So, we only need to point used_block->next_free_chunk to this freed chunk
                // and write zero to the address pointed by used_block->next_free_chunk
                used_block->next_free_chunk  = reinterpret_cast<size_t *>(ptr);


                // Call the destructor
                if constexpr (dest && std::is_destructible<T>::value && !std::is_trivially_destructible<T>::value)
                    ptr->~T();

                *used_block->next_free_chunk = 0;
                ptr = nullptr;

                return; // We don't need the next code
            }

            // Call the destructor
            if constexpr (dest && std::is_destructible<T>::value && !std::is_trivially_destructible<T>::value)
                ptr->~T();

            // In this situation we must point used_block->next_free_chunk to ptr
            // and write into *ptr the address of used_block->next_free_chunk
            // This way the new used_block->next_free_chunk (*ptr) will point to the previous used_block->next_free_chunk

            // freed will become the next available chunk
            auto *freed = reinterpret_cast<size_t *>(ptr);

            // Now we must write into freed the address set by used_block->next_free_chunk
            // which will become the next available address from freed
            *freed = reinterpret_cast<size_t>(used_block->next_free_chunk);

            // Point the new next available chunk to freed
            used_block->next_free_chunk = freed;

            // Once freed set the pointer to nullptr
            ptr = nullptr;
        }

        MP_NODISCARD auto get_chunk_size() const noexcept -> size_t
        {
            return chunk_size;
        }

        MP_NODISCARD auto block_count() const noexcept -> size_t
        {
            size_t count = 0;

            auto *block = first_block;
            do
            {
                ++count;
                block = block->next_block;
            } while (block != nullptr);

            return count;
        }

        MP_NODISCARD auto available_chunks_in_block(T *p)
        {
            return block_from_pointer(p)->available_chunks;
        }

        MP_NODISCARD auto available_space_in_block(T *p)
        {
            return block_from_pointer(p)->available_space;
        }

        MP_NODISCARD auto used_chunks_in_block(T *p)
        {
            return block_from_pointer(p)->used_chunks;
        }

        MP_NODISCARD auto used_space_in_block(T *p)
        {
            return block_from_pointer(p)->used_space;
        }

        /// \brief Get the address of the block where p lives or the first block if p == nullptr
        /// \param p Pointer to get the block or the first block if p == nullptr
        /// \return A block address
        MP_NODISCARD auto block_address(T *p) -> auto
        {
            if (p == nullptr)
                return first_block->block_beginning;
            return block_from_pointer(p)->block_beginning;
        }

        /// \brief Dumps the free list of the block where p lives
        /// \param p pointer used to determinate the block
        /// \return a vector with a pair of values where the first parameters is a free memory block and the second parameter
        /// is the next free block where the first parameter pointed to
        /// A vector of size zero  corresponds to a fully used block
        /// A vector entry where the first parameter is an address and the second is nullptr corresponds to the end of the list
        MP_NODISCARD auto dump_free_list(T *p) -> std::vector<std::pair<T *, T *>>
        {
            auto *block = block_from_pointer(p);
            if (!block->available_chunks)
                return {};

            std::vector<std::pair<T *, T *>> data;
            data.reserve(block->available_chunks);

            auto *free = block->next_free_chunk;
            do {
                auto *next = reinterpret_cast<size_t *>(*free);
                data.emplace_back(reinterpret_cast<T *>(free), reinterpret_cast<T *>(next));
                free = next;
            } while (free != nullptr);

            return data;
        }

    protected:
        auto get_available_chunk() -> T *
        {
            // Get the block of the current chunk
            block *current_block;
            block *next = first_block;
            while (true)
            {
                current_block = next;
                if (current_block->available_chunks)
                {
                    // In this block we have space, lets use this one!
                    break;
                }

                next = current_block->next_block;
                if (next == nullptr)
                {
                    // Ups! we do not have any more blocks
                    // Stop the loop because current_block points to the last available block.
                    // The next condition will handle the creation of a new block
                    break;
                }
            }

            if (current_block->available_chunks == 0) // No more chunks
            {
                // Allocate a new block; allocate_block, will handle for us setting the double-linked list
                // of the newly created block
                allocate_block(&current_block->next_block, current_block);
                current_block = current_block->next_block; // Update the current block
            }

            // Update chunks
            ++current_block->used_chunks;
            --current_block->available_chunks;

            // Update size
            current_block->available_space -= chunk_size;
            current_block->used_space += chunk_size;

            // Get the available address
            auto *available = current_block->next_free_chunk;

            // Update current_block->next_free_chunk, so it points to the next available address, which is: *current_block->next_free_chunk
            current_block->next_free_chunk = reinterpret_cast<size_t *>(*available);

#ifdef REPORT_ALLOCATIONS
            reporter.alloc_report(current_block, available, chunk_size, current_block->available_space, current_block->available_chunks, current_block->used_space, current_block->used_chunks);
#endif /*REPORT_ALLOCATIONS*/

            // Return the available address
            return reinterpret_cast<T *>(available);
        }

    private:
        size_t block_size { 0 };
        size_t chunk_size { 0 };

#if defined(REPORT_ALLOCATIONS) || defined(CHECK_MEMORY_LEAK)
        P reporter;
#endif /*REPORT_ALLOCATIONS*/
    };



} // namespace pool

#endif // MEMPOOL_FIXPOOL_BLOCK_HPP
