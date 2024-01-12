#pragma once

#include <solanaceae/contact/contact_model3.hpp>

#include <solanaceae/toxcore/tox_key.hpp>

#include <cstdint>
#include <set>

namespace Message::Components {

struct ToxGroupMessageID {
	uint32_t id = 0u;
};

// TODO: move all those comps

namespace Transfer {

	struct ToxTransferFriend {
		uint32_t friend_number;
		uint32_t transfer_number;
	};

	struct FileID {
		// persistent ID
		// sometimes called file_id or hash
		ToxKey id;
		// TODO: variable length
	};

	struct FileKind {
		// TODO: use tox file kind
		uint64_t kind {0};
	};

} // Transfer

} // Message::Components

#include "./components_id.inl"

