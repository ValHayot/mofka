/*
 * (C) 2023 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef MOFKA_BATCH_IMPL_H
#define MOFKA_BATCH_IMPL_H

#include <thallium.hpp>
#include <mutex>
#include <queue>
#include "mofka/RequestResult.hpp"
#include "mofka/EventID.hpp"
#include "mofka/Metadata.hpp"
#include "mofka/Archive.hpp"
#include "mofka/Serializer.hpp"
#include "mofka/Data.hpp"
#include "mofka/Future.hpp"
#include "mofka/Producer.hpp"
#include "PartitionTargetInfoImpl.hpp"
#include "ThreadPoolImpl.hpp"
#include "ProducerImpl.hpp"
#include "Promise.hpp"
#include "DataImpl.hpp"
#include <vector>
#include <cstdint>

namespace mofka {

class BatchImpl {

    struct BatchOutputArchive : public Archive {

        void read(void* buffer, std::size_t size) override {
            /* this function is not supposed to be called */
            (void)buffer;
            (void)size;
        }

        void write(const void* data, size_t size) override {
            auto new_size = m_buffer.size() + size;
            if(m_buffer.capacity() < m_buffer.size() + size) {
                m_buffer.reserve(2*new_size);
            }
            auto offset = m_buffer.size();
            m_buffer.resize(new_size);
            std::memcpy(m_buffer.data() + offset, data, size);
            m_size += size;
        }

        BatchOutputArchive(size_t& s, std::vector<char>& buf)
        : m_size(s)
        , m_buffer(buf) {}

        size_t&            m_size;
        std::vector<char>& m_buffer;
    };

    std::vector<size_t>        m_meta_sizes;      /* size of each serialized metadata object */
    std::vector<char>          m_meta_buffer;     /* packed serialized metadata objects */
    std::vector<size_t>        m_data_offsets;    /* offset at which the data for each metata starts */
    std::vector<size_t>        m_data_sizes;      /* size of the data associated with each metadata */
    std::vector<Data::Segment> m_data_segments;   /* list of data segments */
    size_t                     m_total_data_size; /* sum of sizes in the m_data_segments */

    std::vector<Promise<EventID>> m_promises; /* promise associated with each event */

    public:

    void setPromises(EventID firstID) {
        auto id = firstID;
        for(auto& promise : m_promises) {
            promise.setValue(id);
            ++id;
        }
    }

    void setPromises(Exception ex) {
        for(auto& promise : m_promises) {
            promise.setException(std::move(ex));
        }
    }

    void push(
            const Metadata& metadata,
            const Serializer& serializer,
            const Data& data,
            Promise<EventID> promise) {
        size_t meta_size = 0;
        BatchOutputArchive archive(meta_size, m_meta_buffer);
        serializer.serialize(archive, metadata);
        m_meta_sizes.push_back(meta_size);
        m_data_offsets.push_back(m_total_data_size);
        size_t data_size = 0;
        for(const auto& seg : data.self->m_segments) {
            m_data_segments.push_back(seg);
            data_size += seg.size;
        }
        m_data_sizes.push_back(data_size);
        m_total_data_size += data_size;
        m_promises.push_back(std::move(promise));
    }

    thallium::bulk expose(thallium::engine engine) {
        if(count() == 0) return thallium::bulk{};
        std::vector<std::pair<void *, size_t>> segments;
        segments.reserve(4 + m_data_segments.size());
        segments.emplace_back(m_meta_sizes.data(), m_meta_sizes.size()*sizeof(m_meta_sizes[0]));
        segments.emplace_back(m_meta_buffer.data(), m_meta_buffer.size()*sizeof(m_meta_buffer[0]));
        segments.emplace_back(m_data_offsets.data(), m_data_offsets.size()*sizeof(m_data_offsets[0]));
        segments.emplace_back(m_data_sizes.data(), m_data_sizes.size()*sizeof(m_data_sizes[0]));
        for(const auto& seg : m_data_segments) {
            if(!seg.size) continue;
            segments.emplace_back(const_cast<void*>(seg.ptr), seg.size);
        }
        return engine.expose(segments, thallium::bulk_mode::read_only);
    }

    size_t count() const {
        return m_meta_sizes.size();
    }

    size_t totalSize() const {
        return m_total_data_size;
    }

    size_t dataOffset() const {
        return m_meta_sizes.size()*sizeof(m_meta_sizes[0])
             + m_meta_buffer.size()*sizeof(m_meta_buffer[0]);
    }
};

class ActiveBatchQueue {

    public:

    ActiveBatchQueue(
        std::string topic_name,
        std::string producer_name,
        std::shared_ptr<ClientImpl> client,
        std::shared_ptr<PartitionTargetInfoImpl> target,
        ThreadPool thread_pool,
        BatchSize batch_size)
    : m_topic_name(std::move(topic_name))
    , m_producer_name(std::move(producer_name))
    , m_client(std::move(client))
    , m_target(std::move(target))
    , m_thread_pool{std::move(thread_pool)}
    , m_batch_size{batch_size} {
        start();
    }

    ~ActiveBatchQueue() {
        stop();
    }

    void push(
            const Metadata& metadata,
            const Serializer& serializer,
            const Data& data,
            Promise<EventID> promise) {
        bool need_notification;
        {
            auto adaptive = m_batch_size == BatchSize::Adaptive();
            need_notification = adaptive;
            std::unique_lock<thallium::mutex> guard{m_mutex};
            if(m_batch_queue.empty())
                m_batch_queue.push(std::make_shared<BatchImpl>());
            auto last_batch = m_batch_queue.back();
            if(!adaptive && last_batch->count() == m_batch_size.value) {
                m_batch_queue.push(std::make_shared<BatchImpl>());
                last_batch = m_batch_queue.back();
                need_notification = true;
            }
            last_batch->push(metadata, serializer, data, std::move(promise));
        }
        if(need_notification)
            m_cv.notify_one();
    }

    void stop() {
        if(!m_running) return;
        {
            std::unique_lock<thallium::mutex> guard{m_mutex};
            m_need_stop = true;
        }
        m_cv.notify_one();
        m_terminated.wait();
        m_terminated.reset();
    }

    void start() {
        if(m_running) return;
        m_thread_pool.self->pushWork([this]() { loop(); });
    }

    void flush() {
        if(!m_running) return;
        {
            std::unique_lock<thallium::mutex> guard{m_mutex};
            m_request_flush = true;
        }
        m_cv.notify_one();
    }

    private:

    void loop() {
        m_running = true;
        std::unique_lock<thallium::mutex> guard{m_mutex};
        while(!m_need_stop || !m_batch_queue.empty()) {
            m_cv.wait(guard, [this]() {
                if(m_need_stop || m_request_flush)        return true;
                if(m_batch_queue.empty())                 return false;
                if(m_batch_size == BatchSize::Adaptive()) return true;
                auto batch = m_batch_queue.front();
                if(batch->count() == m_batch_size.value)  return true;
                return false;
            });
            if(m_batch_queue.empty()) {
                m_request_flush = false;
                continue;
            }
            auto batch = m_batch_queue.front();
            m_batch_queue.pop();
            guard.unlock();
            sendBatch(batch);
            guard.lock();
            m_request_flush = false;
        }
        m_running = false;
        m_terminated.set_value();
    }

    void sendBatch(const std::shared_ptr<BatchImpl>& batch) {
        thallium::bulk content;
        try {
            content = batch->expose(m_client->m_engine);
        } catch(const std::exception& ex) {
            batch->setPromises(
                Exception{fmt::format(
                    "Unexpected error when registering batch for RDMA: {}", ex.what())});
            return;
        }
        try {
            auto ph = m_target->m_ph;
            auto rpc = m_client->m_send_batch;
            RequestResult<EventID> result = rpc.on(ph)(
                m_topic_name,
                m_producer_name,
                batch->count(),
                batch->totalSize(),
                batch->dataOffset(),
                content);
            if(result.success())
                batch->setPromises(result.value());
            else
                batch->setPromises(Exception{result.error()});
        } catch(const std::exception& ex) {
            batch->setPromises(
                Exception{fmt::format("Unexpected error when sending batch: {}", ex.what())});
        }
    }

    std::string                              m_topic_name;
    std::string                              m_producer_name;
    std::shared_ptr<ClientImpl>              m_client;
    std::shared_ptr<PartitionTargetInfoImpl> m_target;
    ThreadPool                               m_thread_pool;
    BatchSize                                m_batch_size;
    std::queue<std::shared_ptr<BatchImpl>>   m_batch_queue;
    thallium::managed<thallium::thread>      m_sender_ult;
    bool                                     m_need_stop = false;
    bool                                     m_request_flush = false;
    std::atomic<bool>                        m_running = false;
    thallium::mutex                          m_mutex;
    thallium::condition_variable             m_cv;
    thallium::eventual<void>                 m_terminated;

};

}

#endif
