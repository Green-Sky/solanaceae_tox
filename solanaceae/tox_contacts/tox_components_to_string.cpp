#include "./tox_components_to_string.hpp"

#include "./components.hpp"

#include <entt/entity/registry.hpp>
#include <entt/entity/handle.hpp>

#include <solanaceae/util/utils.hpp>

#include <string>

namespace Contact {

void registerToxComponents2Str(ContactStore4I& cs) {
	cs.registerComponentToString(
		entt::type_id<Contact::Components::ToxFriendPersistent>().hash(),
		+[](ContactHandle4 c, bool verbose) -> std::string {
			const auto& comp = c.get<Contact::Components::ToxFriendPersistent>();
			return "key:" + bin2hex(ByteSpan{comp.key.data});
		},
		"Tox",
		"FriendPersistent",
		entt::type_id<Contact::Components::ToxFriendPersistent>().name(),
		true
	);

	cs.registerComponentToString(
		entt::type_id<Contact::Components::ToxFriendEphemeral>().hash(),
		+[](ContactHandle4 c, bool verbose) -> std::string {
			const auto& comp = c.get<Contact::Components::ToxFriendEphemeral>();
			return std::to_string(comp.friend_number);
		},
		"Tox",
		"FriendEphemeral",
		entt::type_id<Contact::Components::ToxFriendEphemeral>().name(),
		true
	);

	cs.registerComponentToString(
		entt::type_id<Contact::Components::ToxConfPersistent>().hash(),
		+[](ContactHandle4 c, bool verbose) -> std::string {
			const auto& comp = c.get<Contact::Components::ToxConfPersistent>();
			return "key:" + bin2hex(ByteSpan{comp.key.data});
		},
		"Tox",
		"ConfPersistent",
		entt::type_id<Contact::Components::ToxConfPersistent>().name(),
		true
	);

	cs.registerComponentToString(
		entt::type_id<Contact::Components::ToxConfEhpemeral>().hash(),
		+[](ContactHandle4 c, bool verbose) -> std::string {
			const auto& comp = c.get<Contact::Components::ToxConfEhpemeral>();
			return std::to_string(comp.id);
		},
		"Tox",
		"ConfEhpemeral",
		entt::type_id<Contact::Components::ToxConfEhpemeral>().name(),
		true
	);

	cs.registerComponentToString(
		entt::type_id<Contact::Components::ToxGroupPersistent>().hash(),
		+[](ContactHandle4 c, bool verbose) -> std::string {
			const auto& comp = c.get<Contact::Components::ToxGroupPersistent>();
			return "id:" + bin2hex(ByteSpan{comp.chat_id.data});
		},
		"Tox",
		"GroupPersistent",
		entt::type_id<Contact::Components::ToxGroupPersistent>().name(),
		true
	);

	cs.registerComponentToString(
		entt::type_id<Contact::Components::ToxGroupEphemeral>().hash(),
		+[](ContactHandle4 c, bool verbose) -> std::string {
			const auto& comp = c.get<Contact::Components::ToxGroupEphemeral>();
			return std::to_string(comp.group_number);
		},
		"Tox",
		"GroupEphemeral",
		entt::type_id<Contact::Components::ToxGroupEphemeral>().name(),
		true
	);

	cs.registerComponentToString(
		entt::type_id<Contact::Components::ToxGroupIncomingRequest>().hash(),
		+[](ContactHandle4 c, bool verbose) -> std::string {
			const auto& comp = c.get<Contact::Components::ToxGroupIncomingRequest>();
			std::string str;
			str += "friend:" + std::to_string(comp.friend_number) + " ";
			if (verbose) {
				str += "invite:" + bin2hex(comp.invite_data);
			} else {
				str += "invite:(" + std::to_string(comp.invite_data.size()) + " bytes)";
			}
			return str;
		},
		"Tox",
		"GroupIncomingRequest",
		entt::type_id<Contact::Components::ToxGroupIncomingRequest>().name(),
		true
	);

	cs.registerComponentToString(
		entt::type_id<Contact::Components::ToxGroupPeerPersistent>().hash(),
		+[](ContactHandle4 c, bool verbose) -> std::string {
			const auto& comp = c.get<Contact::Components::ToxGroupPeerPersistent>();
			return "group:" + bin2hex(ByteSpan{comp.chat_id.data}) + " peer:" + bin2hex(ByteSpan{comp.peer_key.data});
		},
		"Tox",
		"GroupPeerPersistent",
		entt::type_id<Contact::Components::ToxGroupPeerPersistent>().name(),
		true
	);

	cs.registerComponentToString(
		entt::type_id<Contact::Components::ToxGroupPeerEphemeral>().hash(),
		+[](ContactHandle4 c, bool verbose) -> std::string {
			const auto& comp = c.get<Contact::Components::ToxGroupPeerEphemeral>();
			return "group:" + std::to_string(comp.group_number) + " peer:" + std::to_string(comp.peer_number);
		},
		"Tox",
		"GroupPeerEphemeral",
		entt::type_id<Contact::Components::ToxGroupPeerEphemeral>().name(),
		true
	);
}

} // Contact
