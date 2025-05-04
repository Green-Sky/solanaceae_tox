#pragma once

#include <solanaceae/contact/fwd.hpp>
#include <solanaceae/object_store/meta_components_file.hpp> // contains the alias

#include <solanaceae/message3/registry_message_model.hpp>

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

		// TODO: replace this with something generic
		struct ToxContact {
			ContactHandle4 c;
		};

		// TODO: replace this with something generic
		struct ToxMessage {
			// the message, if the ft is visible as a message
			Message3Handle m;
		};

	} // Ephemeral

} // ObjectStore::Components

#include "./obj_components_id.inl"

