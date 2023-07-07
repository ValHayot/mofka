/*
 * (C) 2023 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "mofka/ServiceHandle.hpp"
#include "mofka/RequestResult.hpp"
#include "mofka/Exception.hpp"
#include "mofka/TopicHandle.hpp"

#include "PimplUtil.hpp"
#include "AsyncRequestImpl.hpp"
#include "ClientImpl.hpp"
#include "ServiceHandleImpl.hpp"
#include "TopicHandleImpl.hpp"
#include "MetadataImpl.hpp"

namespace mofka {

PIMPL_DEFINE_COMMON_FUNCTIONS(ServiceHandle);

Client ServiceHandle::client() const {
    return Client(self->m_client);
}

TopicHandle ServiceHandle::createTopic(
        std::string_view name,
        TopicBackendConfig config,
        Validator validator,
        Serializer serializer) {
    const auto hash = std::hash<decltype(name)>()(name);
    const auto ph   = self->m_mofka_phs[hash % self->m_mofka_phs.size()];
    using ResultType = std::tuple<Metadata, Metadata>;
    RequestResult<ResultType> response =
        self->m_client->m_create_topic.on(ph)(
            std::string{name},
            static_cast<Metadata&>(config),
            validator.metadata(),
            serializer.metadata());
    if(!response.success())
        throw Exception(response.error());

    Metadata validator_meta;
    Metadata serializer_meta;
    std::tie(validator_meta, serializer_meta) = response.value();
    validator = Validator::FromMetadata(validator_meta);
    serializer = Serializer::FromMetadata(serializer_meta);
    return std::make_shared<TopicHandleImpl>(
        name, self,
        std::move(validator),
        std::move(serializer));
}

TopicHandle ServiceHandle::openTopic(std::string_view name) {
    const auto hash = std::hash<decltype(name)>()(name);
    const auto ph   = self->m_mofka_phs[hash % self->m_mofka_phs.size()];
    using ResultType = std::tuple<Metadata, Metadata>;
    RequestResult<ResultType> response =
        self->m_client->m_open_topic.on(ph)(std::string{name});
    if(!response.success())
        throw Exception(response.error());

    Metadata validator_meta;
    Metadata serializer_meta;
    std::tie(validator_meta, serializer_meta) = response.value();
    auto validator = Validator::FromMetadata(validator_meta);
    auto serializer = Serializer::FromMetadata(serializer_meta);
    return std::make_shared<TopicHandleImpl>(
        name, self,
        std::move(validator),
        std::move(serializer));
}

}
