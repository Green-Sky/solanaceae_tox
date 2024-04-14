#pragma once

#include <solanaceae/message3/message_serializer.hpp>
#include "./tox_message_components.hpp"

inline void registerToxMessageComponents(MessageSerializerNJ& msnj) {
	msnj.registerSerializer<Message::Components::ToxGroupMessageID>();
	msnj.registerDeserializer<Message::Components::ToxGroupMessageID>();
}

