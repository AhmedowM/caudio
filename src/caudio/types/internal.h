#ifndef CAUDIO_TYPES_INTERNAL_H
#define CAUDIO_TYPES_INTERNAL_H

/**
 * @file types/internal.h
 * @brief Private shared state for the caudio types implementation.
 *
 * Not installed. The record lifecycle and the JSON codec share vector
 * callbacks and the thread-local copy error channel, so both translation units
 * must see the same definitions.
 */

#include <caudio/types/record.h>
#include <cstdx/allocator.h>
#include <cstdx/vector.h>

/* Vector lifecycle callbacks for the owning record element types. */
extern const cx_vector_ops ca_impl_device_info_vector_ops;
extern const cx_vector_ops ca_impl_top_entity_vector_ops;
extern const cx_vector_ops ca_impl_track_vector_ops;

/*
 * cx_vector copy callbacks cannot report allocation failures. Record copies
 * stash the allocator and the first failure here; the enclosing copy checks it
 * after each push.
 */
extern thread_local const cx_allocator* ca_impl_copy_alloc;
extern thread_local ca_status ca_impl_copy_status;

/**
 * @brief Start a copy operation and clear the error channel.
 */
void ca_impl_copy_reset(const cx_allocator* alloc);

/**
 * @brief Record the first failure encountered by a vector copy callback.
 */
void ca_impl_copy_fail(ca_status status);

#endif /* CAUDIO_TYPES_INTERNAL_H */
