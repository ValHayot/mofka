/*
 * (C) 2023 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef MOFKA_TOPIC_HANDLE_IMPL_H
#define MOFKA_TOPIC_HANDLE_IMPL_H

#include "ServiceHandleImpl.hpp"
#include "PartitionTargetInfoImpl.hpp"
#include "mofka/Validator.hpp"
#include "mofka/TargetSelector.hpp"
#include "mofka/Serializer.hpp"
#include <string_view>

namespace mofka {

class TopicHandleImpl {

    public:

    std::string           m_name;
    SP<ServiceHandleImpl> m_service;
    Validator             m_validator;
    TargetSelector        m_selector;
    Serializer            m_serializer;

    TopicHandleImpl() = default;

    TopicHandleImpl(std::string_view name,
                    SP<ServiceHandleImpl> service,
                    Validator validator,
                    TargetSelector selector,
                    Serializer serializer)
    : m_name(name)
    , m_service(std::move(service))
    , m_validator(std::move(validator))
    , m_selector(std::move(selector))
    , m_serializer(std::move(serializer)) {
        m_selector.setTargets(m_service->m_mofka_targets);
    }
};

}

#endif
