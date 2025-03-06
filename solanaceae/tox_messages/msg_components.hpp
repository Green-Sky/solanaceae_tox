#pragma once

#include <cstdint>

namespace Message::Components {

struct ToxFriendMessageID {
	// only exposed for the read reciept event
	uint32_t id = 0u;
};

struct ToxGroupMessageID {
	uint32_t id = 0u;
};

} // Message::Components

#include "./msg_components_id.inl"

