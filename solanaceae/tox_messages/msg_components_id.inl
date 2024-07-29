#pragma once

#include "./msg_components.hpp"

#include <entt/core/type_info.hpp>

// TODO: move more central
#define DEFINE_COMP_ID(x) \
template<> \
constexpr entt::id_type entt::type_hash<x>::value() noexcept { \
	using namespace entt::literals; \
	return #x##_hs; \
} \
template<> \
constexpr std::string_view entt::type_name<x>::value() noexcept { \
	return #x; \
}

// cross compile(r) stable ids

DEFINE_COMP_ID(Message::Components::ToxFriendMessageID)
DEFINE_COMP_ID(Message::Components::ToxGroupMessageID)

#undef DEFINE_COMP_ID

