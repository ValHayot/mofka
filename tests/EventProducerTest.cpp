/*
 * (C) 2023 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <bedrock/Server.hpp>
#include <mofka/Client.hpp>
#include <mofka/TopicHandle.hpp>
#include "BedrockConfig.hpp"

TEST_CASE("Event producer test", "[event-producer]") {

    spdlog::set_level(spdlog::level::from_str("critical"));
    auto remove_file = EnsureFileRemoved{"mofka.ssg"};

    auto server = bedrock::Server("na+sm", config);
    auto gid = server.getSSGManager().getGroup("mofka_group")->getHandle<uint64_t>();
    auto engine = server.getMargoManager().getThalliumEngine();

    SECTION("Initialize client/topic/producer") {
        auto client = mofka::Client{engine};
        REQUIRE(static_cast<bool>(client));
        auto sh = client.connect(mofka::SSGGroupID{gid});
        REQUIRE(static_cast<bool>(sh));
        mofka::TopicHandle topic;
        REQUIRE(!static_cast<bool>(topic));
        auto topic_config = mofka::TopicBackendConfig{};/*R"(
            {
                "__type__":"default",
                "data_store": {
                    "__type__": "memory"
                }
            })"
        };
        */
        topic = sh.createTopic("mytopic", topic_config);
        REQUIRE(static_cast<bool>(topic));

        auto thread_count = GENERATE(as<mofka::ThreadCount>{}, 0, 1, 2);
        auto batch_size   = GENERATE(mofka::BatchSize::Adaptive(), mofka::BatchSize::Adaptive());
        auto ordering     = GENERATE(mofka::Ordering::Strict, mofka::Ordering::Loose);

        auto producer = topic.producer(
            "myproducer", batch_size, thread_count, ordering);
        REQUIRE(static_cast<bool>(producer));

        SECTION("Push events into the topic using the producer") {
            auto future = producer.push(
                mofka::Metadata("{\"name\":\"matthieu\"}"),
                mofka::Data{nullptr, 0});
            producer.flush();
            future.wait();
        }

        SECTION("Push events with data") {
            std::string someData = "This is some data";
            auto future = producer.push(
                mofka::Metadata("{\"name\":\"matthieu\"}"),
                mofka::Data{someData.data(), someData.size()});
            producer.flush();
            future.wait();
        }
    }

    server.finalize();
}
