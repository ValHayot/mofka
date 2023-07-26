/*
 * (C) 2023 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "ConsumerHandleImpl.hpp"
#include "PimplUtil.hpp"
#include <limits>

namespace mofka {

PIMPL_DEFINE_COMMON_FUNCTIONS_NO_DTOR(ConsumerHandle);

ConsumerHandle::~ConsumerHandle() {
}

const std::string& ConsumerHandle::name() const {
    return self->m_consumer_name;
}

bool ConsumerHandle::feed(
    size_t count,
    const BulkRef &metadata_sizes,
    const BulkRef &metadata,
    const BulkRef &data_desc_sizes,
    const BulkRef &data_desc)
{
    (void)count;
    (void)metadata_sizes;
    (void)metadata;
    (void)data_desc_sizes;
    (void)data_desc;
    return false;
}

}
