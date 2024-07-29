#pragma once

#include <solanaceae/tox_messages/msg_components.hpp>

#include <nlohmann/json.hpp>

namespace Message::Components {

	// TODO: friend msg id, does not have the same qualities
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ToxGroupMessageID, id)
	// TODO: transfer stuff, needs content rewrite

} // Message::Components

