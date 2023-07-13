/*
 * (C) 2023 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef MOFKA_CLIENT_IMPL_H
#define MOFKA_CLIENT_IMPL_H

#include <bedrock/Client.hpp>
#include <thallium.hpp>
#include <thallium/serialization/stl/unordered_set.hpp>
#include <thallium/serialization/stl/unordered_map.hpp>
#include <thallium/serialization/stl/string.hpp>

namespace mofka {

namespace tl = thallium;

class ClientImpl {

    public:

    tl::engine           m_engine;
    tl::remote_procedure m_create_topic;
    tl::remote_procedure m_open_topic;
    tl::remote_procedure m_send_batch;
    bedrock::Client      m_bedrock_client;

    ClientImpl(const tl::engine& engine)
    : m_engine(engine)
    , m_create_topic(m_engine.define("mofka_create_topic"))
    , m_open_topic(m_engine.define("mofka_open_topic"))
    , m_send_batch(m_engine.define("mofka_send_batch"))
    , m_bedrock_client(m_engine)
    {}
};

}

#endif
