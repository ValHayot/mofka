/*
 * (C) 2023 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef MOFKA_CONSUMER_IMPL_H
#define MOFKA_CONSUMER_IMPL_H

#include "PimplUtil.hpp"
#include "TopicHandleImpl.hpp"
#include "PartitionTargetInfoImpl.hpp"
#include "ProducerImpl.hpp"

#include "mofka/Consumer.hpp"
#include "mofka/UUID.hpp"

#include <thallium.hpp>
#include <string_view>
#include <queue>

namespace mofka {

class ClientImpl;

class ConsumerImpl : public std::enable_shared_from_this<ConsumerImpl> {

    friend class ClientImpl;

    public:

    thallium::engine                       m_engine;
    const std::string                      m_name;
    const UUID                             m_uuid;
    const BatchSize                        m_batch_size;
    const SP<ThreadPoolImpl>               m_thread_pool;
    const DataBroker                       m_data_broker;
    const DataSelector                     m_data_selector;
    const EventProcessor                   m_event_processor;
    const std::vector<PartitionTargetInfo> m_targets;
    const std::shared_ptr<TopicHandleImpl> m_topic;

    const std::string m_self_addr;

    /* The futures/promises queue works as follows:
     *
     * If m_futures_credit is true, it means the futures in the queue
     * have been created by the user via pull() operations. Hence if
     * the consumer needs to issue a new operation, it will use the
     * promise in m_futures.front() (the oldest created by the user)
     * and take it off the queue. If the user calls pull(), it simply
     * append a new promise/future pair at the back of the queue.
     *
     * If m_future_credit is false, it means the future in the queue
     * have been created by the consumer before the user had a chance
     * to call pull(). Hence if the consumer needs to issue a new
     * operation, it will push a new promise/future pair at the back
     * of the queue. If the user calls pull(), it will use the future
     * at in m_futures.front() (the oldest created by the consumer)
     * and take it off the queue. This is the symetric of the above.
     */
    std::deque<std::pair<Promise<Event>, Future<Event>>> m_futures;
    bool                                                 m_futures_credit;
    thallium::mutex                                      m_futures_mtx;

    /**
     * Vector of eventuals that will be set on completion of the
     * ULTs that pull events from each target.
     */
    std::vector<thallium::eventual<void>> m_pulling_ult_completed;

    ConsumerImpl(thallium::engine engine,
                 std::string_view name,
                 BatchSize batch_size,
                 SP<ThreadPoolImpl> thread_pool,
                 DataBroker broker,
                 DataSelector selector,
                 std::vector<PartitionTargetInfo> targets,
                 std::shared_ptr<TopicHandleImpl> topic)
    : m_engine(std::move(engine))
    , m_name(name)
    , m_uuid(UUID::generate())
    , m_batch_size(batch_size)
    , m_thread_pool(std::move(thread_pool))
    , m_data_broker(std::move(broker))
    , m_data_selector(std::move(selector))
    , m_targets(std::move(targets))
    , m_topic(std::move(topic))
    , m_self_addr(m_engine.self())
    {
        start();
    }

    ~ConsumerImpl() {
        join();
    }

    private:

    void start();

    void join();

    void pullFrom(
        size_t target_info_index,
        thallium::eventual<void>& ev);

    void recvBatch(
        size_t target_info_index,
        size_t count,
        EventID firstID,
        const BulkRef &metadata_sizes,
        const BulkRef &metadata,
        const BulkRef &data_desc_sizes,
        const BulkRef &data_desc);

    SP<DataImpl> requestData(
        SP<PartitionTargetInfoImpl> target,
        SP<MetadataImpl> metadata,
        SP<DataDescriptorImpl> descriptor);
};

}

#endif
