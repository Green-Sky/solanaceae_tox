#include "./components.hpp"

#include <entt/core/type_info.hpp>

// TODO: move more central
#define DEFINE_COMP_ID(x) \
template<> \
constexpr entt::id_type entt::type_hash<x>::value() noexcept { \
    using namespace entt::literals; \
    return #x##_hs; \
}

// cross compiler stable ids

DEFINE_COMP_ID(Contact::Components::ToxFriendPersistent)
DEFINE_COMP_ID(Contact::Components::ToxFriendEphemeral)
DEFINE_COMP_ID(Contact::Components::ToxConfPersistent)
DEFINE_COMP_ID(Contact::Components::ToxConfEhpemeral)
DEFINE_COMP_ID(Contact::Components::ToxGroupPersistent)
DEFINE_COMP_ID(Contact::Components::ToxGroupEphemeral)
DEFINE_COMP_ID(Contact::Components::ToxGroupIncomingRequest)
DEFINE_COMP_ID(Contact::Components::ToxGroupPeerPersistent)
DEFINE_COMP_ID(Contact::Components::ToxGroupPeerEphemeral)

#undef DEFINE_COMP_ID

