/////////////////////////////////////////////////////////////////////////////////////
//
// Created by Ricardo Romero on 20/12/22.
// Copyright (c) 2022 Ricardo Romero.  All rights reserved.
//

#pragma once

#ifndef __cplusplus
#error "C++ compiler needed"
#endif /*__cplusplus*/

#ifndef WBSCRP_POOL_CONCEPT_HPP
#define WBSCRP_POOL_CONCEPT_HPP

#include <concepts>
#include <cstdint>
#include <vector>

namespace pool
{
#if defined REPORT_ALLOCATIONS && defined CHECK_MEMORY_LEAK
    template <typename T>
    concept pool_reporter = requires(T t, void *p, std::size_t b0, uint8_t *p8, const std::vector<std::pair<uint64_t *, uint64_t *>> &pv) {
                                {
                                    t.allocate_block(p, b0, b0)
                                } -> std::same_as<void>;
                                {
                                    t.deallocate_block(p, b0, b0)
                                } -> std::same_as<void>;
                                {
                                    t.alloc_report(p, p, b0, b0, b0, b0, b0)
                                } -> std::same_as<void>;
                                {
                                    t.dealloc_report(p, p, b0, b0, b0, b0, b0)
                                } -> std::same_as<void>;
                                {
                                    t.check_memory_leaks(p8, pv, b0, b0, b0, b0, b0)
                                } -> std::same_as<void>;
                            };

    template <typename T>
    concept allocator_reporter = requires(T t, void *ptr, int64_t count, const void *const p, std::size_t size) {
                                     {
                                         t.global_new(ptr)
                                     } -> std::same_as<void>;
                                     {
                                         t.global_freed(ptr)
                                     } -> std::same_as<void>;
                                     {
                                         t.add_ref_count(count)
                                     } -> std::same_as<void>;
                                     {
                                         t.sub_ref_count(count)
                                     } -> std::same_as<void>;
                                     {
                                         t.copy_ctor_ref_count(count)
                                     } -> std::same_as<void>;
                                     {
                                         t.move_ctor_ref_count(count)
                                     } -> std::same_as<void>;
                                     {
                                         t.alloc_request(size)
                                     } -> std::same_as<void>;
                                     {
                                         t.dealloc_request(p, size)
                                     } -> std::same_as<void>;
                                 };
#elif !defined(REPORT_ALLOCATIONS) && defined CHECK_MEMORY_LEAK
    template <typename T>
    concept pool_reporter = requires(T t, std::size_t b0, uint8_t *p8, const std::vector<std::pair<uint64_t *, uint64_t *>> &pv) {
                                {
                                    t.check_memory_leaks(p8, pv, b0, b0, b0, b0, b0)
                                } -> std::same_as<void>;
                            };
#endif /*REPORT_ALLOCATIONS*/

} // namespace pool

#endif // WBSCRP_POOL_CONCEPT_HPP
