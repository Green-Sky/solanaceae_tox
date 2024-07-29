#pragma once

#include "./obj_components.hpp"

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

DEFINE_COMP_ID(ObjComp::Tox::FileID)
DEFINE_COMP_ID(ObjComp::Tox::FileKind)

// tmp
DEFINE_COMP_ID(ObjComp::Tox::TagIncomming)
DEFINE_COMP_ID(ObjComp::Tox::TagOutgoing)

DEFINE_COMP_ID(ObjComp::Ephemeral::ToxTransferFriend)

#undef DEFINE_COMP_ID

