#pragma once

#include <solanaceae/toxcore/tox_key.hpp>

namespace Contact::Components {

	// ==========
	// friend
	// ==========

	struct ToxFriendPersistent {
		ToxKey key;
	};

	struct ToxFriendEphemeral {
		uint32_t friend_number;
	};


	// ==========
	// TODO: conference (old groups)
	// ==========

	struct ToxConfPersistent {
		ToxKey key;
	};

	struct ToxConfEhpemeral {
		uint32_t id;
	};


	// ==========
	// groups (ngc)
	// ==========

	struct ToxGroupPersistent {
		ToxKey chat_id;
	};

	struct ToxGroupEphemeral {
		uint32_t group_number;
	};

	struct ToxGroupPeerPersistent {
		ToxKey chat_id;
		ToxKey peer_key;
	};

	struct ToxGroupPeerEphemeral {
		uint32_t group_number;
		uint32_t peer_number;
	};

} // Contact::Components

#include "./components_id.inl"

