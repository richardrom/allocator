/////////////////////////////////////////////////////////////////////////////////////
//
// Created by Ricardo Romero on 20/12/22.
// Copyright (c) 2022 Ricardo Romero.  All rights reserved.
//

#pragma once

#ifndef __cplusplus
#error "C++ compiler needed"
#endif /*__cplusplus*/

#ifndef WBSCRP_REPORTER_HPP
#define WBSCRP_REPORTER_HPP

#if defined(CHECK_MEMORY_LEAK) || defined(REPORT_ALLOCATIONS)
#include <iomanip>
#include <iostream>
#endif /*defined (CHECK_MEMORY_LEAK) || defined(REPORT_ALLOCATIONS)*/

namespace pool
{
#if defined(CHECK_MEMORY_LEAK) || defined(REPORT_ALLOCATIONS)
    struct pool_reporter_base
    {
        inline static void check_memory_leaks(uint8_t *blockBeginAddress, const std::vector<std::pair<uint64_t *, uint64_t *>> &freeList, std::size_t availableChunks, std::size_t usedChunks, std::size_t availableSpace, std::size_t usedSpace, std::size_t chunkSize)
        {
            std::cout << "MEMORY LEAK DETECTED:\n";
            std::cout << std::setfill(' ') << std::right << std::setw(12) << "chunks: " << std::setw(8) << usedChunks
                      << " of " << availableChunks + usedChunks << "\n";
            std::cout << std::right << std::setw(12) << "size: " << std::setw(8) << usedSpace
                      << " of " << availableSpace + usedSpace << "\n";
            std::cout << "MEMORY DUMP:\n";

            for (auto i = 0ull; i < availableChunks + usedChunks; ++i)
            {
                uint64_t *current = reinterpret_cast<uint64_t *>(blockBeginAddress + (i * chunkSize));

                auto iterFind = std::find_if(freeList.begin(), freeList.end(),
                    [&current](const std::pair<uint64_t *, uint64_t *> &p) {
                        if (current == p.first)
                            return true;
                        return false;
                    });

                if (iterFind == freeList.end())
                {
                    std::cout << std::setfill(' ') << std::right << std::setw(24) << "*0x" << std::hex << std::uppercase << reinterpret_cast<std::size_t>(current) << ": ";
                    for (std::size_t j = 0; j < 8; ++j)
                    {
                        const auto *_toArray = reinterpret_cast<uint8_t *>(current);
                        std::cout << std::setfill('0') << std::setw(2) << std::hex << static_cast<int64_t>(_toArray[j]) << " ";
                    }

                    for (std::size_t j = 0; j < 8; ++j)
                    {
                        const auto *_toArray = reinterpret_cast<char *>(current);
                        if (isgraph(static_cast<int>(_toArray[j])))
                            std::cout << std::hex << _toArray[j];
                        else
                            std::cout << ".";
                    }

                    std::cout << "\n";
                }
            }
        }
    };
    struct allocator_iostream_reporter
    {
        inline static void global_new(void *ptr) noexcept
        {
            std::cout << "New allocator global block allocated. Block: (0x" << std::hex << std::uppercase << reinterpret_cast<std::size_t>(ptr) << std::dec << ")\n";
        }

        inline static void global_freed(void *ptr) noexcept
        {
            std::cout << "Allocator global freed. Block: (0x" << std::hex << std::uppercase << reinterpret_cast<std::size_t>(ptr) << std::dec << ")\n";
        }

        inline static void add_ref_count(int64_t count) noexcept
        {
            std::cout << "Allocator global ref count: (constructor) " << std::dec << count << "\n";
        }

        inline static void sub_ref_count(int64_t count) noexcept
        {
            std::cout << "Allocator global ref count (destructor): " << std::dec << count << "\n";
        }

        inline static void copy_ctor_ref_count(int64_t count) noexcept
        {
            std::cout << "Allocator global ref count (copy constructor): " << std::dec << count << "\n";
        }

        inline static void move_ctor_ref_count(int64_t count) noexcept
        {
            std::cout << "Allocator global ref count (move constructor): " << std::dec << count << "\n";
        }

        inline static void alloc_request(std::size_t size) noexcept
        {
            std::cout << "New allocation request: Size: " << std::dec << size << "\n";
        }

        inline static void dealloc_request(const void *const p, std::size_t n) noexcept
        {
            std::cout << "Deallocation requested. Block: (0x" << std::hex << std::uppercase << reinterpret_cast<std::size_t>(p) << std::dec << "); Size: " << n << ". \n";
            std::cout << std::setfill(' ') << std::right << std::setw(24) << "*0x" << std::hex << std::uppercase << reinterpret_cast<std::size_t>(p) << ": ";
            for (std::size_t j = 0; j < 8; ++j)
            {
                const auto *_toArray = reinterpret_cast<const uint8_t *>(p);
                std::cout << std::setfill('0') << std::setw(2) << std::hex << static_cast<int64_t>(_toArray[j]) << " ";
            }

            for (std::size_t j = 0; j < 8; ++j)
            {
                const auto *_toArray = reinterpret_cast<const char *>(p);
                if (isgraph(static_cast<int>(_toArray[j])))
                    std::cout << std::hex << _toArray[j];
                else
                    std::cout << ".";
            }

            std::cout << "\n";
        }

    };
#endif
#if defined(CHECK_MEMORY_LEAK) && defined(REPORT_ALLOCATIONS)
    struct pool_iostream_reporter : public pool_reporter_base
    {
        inline static void allocate_block(void *p, std::size_t blockSize, std::size_t chunkSize)
        {
            std::cout << "New block allocated. Block size: " << blockSize << " bytes; (Base Address: 0x" << std::hex << std::uppercase << reinterpret_cast<std::size_t>(p) << std::dec << "); Chunk size: " << chunkSize << "\n";
        }

        inline static void deallocate_block(void *p, std::size_t blockSize, std::size_t chunkSize)
        {
            std::cout << "Block freed. Block size: " << blockSize << " (0x" << std::hex << std::uppercase << reinterpret_cast<std::size_t>(p) << std::dec << "); Chunk size: " << chunkSize << "\n";
        }

        inline static void alloc_report(void *currentBlock, void *newPtr, std::size_t chunkSize, std::size_t availableSpace, std::size_t availableChunks, std::size_t usedSpace, std::size_t usedChunks)
        {
            std::cout << "New allocation in block (0x" << std::hex << std::uppercase << reinterpret_cast<std::size_t>(currentBlock) << std::dec << "): (" << chunkSize << ") : 0x"
                      << std::hex << std::uppercase << reinterpret_cast<std::size_t>(newPtr) << std::dec << "; Free space: " << availableSpace << " bytes (" << availableChunks << " chunks); Used space: " << usedSpace << " bytes (" << usedChunks << " chunks);\n";
        }

        inline static void dealloc_report(void *usedBlock, void *oldPtr, std::size_t chunkSize, std::size_t availableSpace, std::size_t availableChunks, std::size_t usedSpace, std::size_t usedChunks)
        {
            std::cout << "Chunk free in block (0x" << std::hex << std::uppercase << reinterpret_cast<std::size_t>(usedBlock) << std::dec << "): (" << chunkSize << ") : 0x"
                      << std::hex << std::uppercase << reinterpret_cast<std::size_t>(oldPtr) << std::dec << "; Free space: " << availableSpace << " bytes (" << availableChunks
                      << " chunks); Used space: " << usedSpace << " bytes (" << usedChunks << " chunks);\n";
        }
    };
#elif defined(CHECK_MEMORY_LEAK) && !defined(REPORT_ALLOCATIONS)
    struct pool_iostream_reporter : public pool_reporter_base
    {
    };
#endif /*REPORT_ALLOCATIONS*/
} // namespace pool

#endif // WBSCRP_REPORTER_HPP
