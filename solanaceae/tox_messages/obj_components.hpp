#pragma once

#include <solanaceae/object_store/meta_components_file.hpp> // contains the alias

#include <solanaceae/toxcore/tox_key.hpp>

namespace ObjectStore::Components {

	namespace Tox {

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

		// temporary replacement for Sending/Receiving
		// TODO: something generic in os ?????
		struct TagIncomming {};
		struct TagOutgoing {};

	} // Tox

	namespace Ephemeral {

		struct ToxTransferFriend {
			uint32_t friend_number;
			uint32_t transfer_number;
		};

	} // Ephemeral

} // ObjectStore::Components

#include "./obj_components_id.inl"

