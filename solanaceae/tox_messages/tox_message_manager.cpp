#include "./tox_message_manager.hpp"

#include <solanaceae/util/time.hpp>

#include <solanaceae/toxcore/tox_interface.hpp>
#include <solanaceae/contact/contact_store_i.hpp>

#include <solanaceae/contact/components.hpp>
#include <solanaceae/tox_contacts/components.hpp>
#include <solanaceae/message3/components.hpp>
#include "./msg_components.hpp"

#include <sodium.h>

#include <chrono>
#include <iostream>
#include <variant>

ToxMessageManager::ToxMessageManager(
	RegistryMessageModelI& rmm,
	ContactStore4I& cs,
	ToxContactModel2& tcm,
	ToxI& t,
	ToxEventProviderI& tep
) :
	_rmm(rmm),
	_rmm_sr(_rmm.newSubRef(this)),
	_cs(cs),
	_tcm(tcm),
	_t(t),
	_tep_sr(tep.newSubRef(this))
{
	_tep_sr
		// TODO: system messages?
		//.subscribe(Tox_Event::TOX_EVENT_FRIEND_CONNECTION_STATUS)
		//.subscribe(Tox_Event::TOX_EVENT_FRIEND_STATUS)
		.subscribe(Tox_Event_Type::TOX_EVENT_FRIEND_MESSAGE)
		.subscribe(Tox_Event_Type::TOX_EVENT_FRIEND_READ_RECEIPT)

		// TODO: conf

		// TODO: system messages?
		//.subscribe(Tox_Event::TOX_EVENT_GROUP_PEER_JOIN)
		//.subscribe(Tox_Event::TOX_EVENT_GROUP_SELF_JOIN)
		//.subscribe(Tox_Event::TOX_EVENT_GROUP_PEER_NAME)
		.subscribe(Tox_Event_Type::TOX_EVENT_GROUP_MESSAGE)
		.subscribe(Tox_Event_Type::TOX_EVENT_GROUP_PRIVATE_MESSAGE)
	;

	_rmm_sr.subscribe(RegistryMessageModel_Event::send_text);
}

ToxMessageManager::~ToxMessageManager(void) {
}

bool ToxMessageManager::sendText(const Contact4 c, std::string_view message, bool action) {
	const auto& cr = _cs.registry();
	if (!cr.valid(c)) {
		return false;
	}

	if (message.empty()) {
		return false; // TODO: empty messages allowed?
	}

	if (cr.all_of<Contact::Components::TagSelfStrong>(c)) {
		return false; // message to self? not with tox
	}

	// testing for persistent is enough
	if (!cr.any_of<
		Contact::Components::ToxFriendPersistent,
		// TODO: conf
		Contact::Components::ToxGroupPersistent,
		Contact::Components::ToxGroupPeerPersistent
	>(c)) {
		return false;
	}

	auto* reg_ptr = _rmm.get(c);
	if (reg_ptr == nullptr) {
		return false; // nope
	}

	Message3Registry& reg = *reg_ptr;

	if (!cr.all_of<Contact::Components::Self>(c)) {
		std::cerr << "TMM error: cant get self\n";
		return false;
	}
	const Contact4 c_self = cr.get<Contact::Components::Self>(c).self;

	// get current time unix epoch utc
	uint64_t ts = getTimeMS();

	// TODO: split into multiple messages here, if its too long ?

	auto new_msg_e = reg.create();
	reg.emplace<Message::Components::ContactFrom>(new_msg_e, c_self);
	reg.emplace<Message::Components::ContactTo>(new_msg_e, c);

	reg.emplace<Message::Components::MessageText>(new_msg_e, message);

	if (action) {
		reg.emplace<Message::Components::TagMessageIsAction>(new_msg_e);
	}

	reg.emplace<Message::Components::TimestampWritten>(new_msg_e, ts);
	reg.emplace<Message::Components::Timestamp>(new_msg_e, ts); // reactive?

	// mark as read
	reg.emplace<Message::Components::Read>(new_msg_e, ts); // reactive?

	// if sent?
	reg.emplace<Message::Components::TimestampProcessed>(new_msg_e, ts);

	reg.emplace<Message::Components::ReceivedBy>(new_msg_e).ts[c_self] = ts;

	if (cr.any_of<Contact::Components::ToxFriendEphemeral>(c)) {
		const uint32_t friend_number = cr.get<Contact::Components::ToxFriendEphemeral>(c).friend_number;

		auto [res, _] = _t.toxFriendSendMessage(
			friend_number,
			action ? Tox_Message_Type::TOX_MESSAGE_TYPE_ACTION : Tox_Message_Type::TOX_MESSAGE_TYPE_NORMAL,
			message
		);

		if (!res.has_value()) {
			// set manually, so it can still be synced
			const uint32_t msg_id = randombytes_random();
			reg.emplace<Message::Components::ToxFriendMessageID>(new_msg_e, msg_id);

			std::cerr << "TMM: failed to send friend message\n";
		} else {
			reg.emplace<Message::Components::ToxFriendMessageID>(new_msg_e, res.value());
		}
	} else if (cr.any_of<Contact::Components::ToxFriendPersistent>(c)) {
		// here we just assume friend not online (no ephemeral id)
		// set manually, so it can still be synced
		const uint32_t msg_id = randombytes_random();
		reg.emplace<Message::Components::ToxFriendMessageID>(new_msg_e, msg_id);
		std::cerr << "TMM: failed to send friend message, offline and not in tox profile\n";
	} else if (
		cr.any_of<Contact::Components::ToxGroupEphemeral>(c)
	) {
		const uint32_t group_number = cr.get<Contact::Components::ToxGroupEphemeral>(c).group_number;

		auto [message_id_opt, _] = _t.toxGroupSendMessage(
			group_number,
			action ? Tox_Message_Type::TOX_MESSAGE_TYPE_ACTION : Tox_Message_Type::TOX_MESSAGE_TYPE_NORMAL,
			message
		);

		if (!message_id_opt.has_value()) {
			// set manually, so it can still be synced
			const uint32_t msg_id = randombytes_random();
			reg.emplace<Message::Components::ToxGroupMessageID>(new_msg_e, msg_id);

			std::cerr << "TMM: failed to send group message!\n";
		} else {
			// TODO: does group msg without msgid make sense???
			reg.emplace<Message::Components::ToxGroupMessageID>(new_msg_e, message_id_opt.value());

			// TODO: generalize?
			reg.emplace<Message::Components::SyncedBy>(new_msg_e).ts.emplace(c_self, ts);
		}
	} else if (
		// non online group
		cr.any_of<Contact::Components::ToxGroupPersistent>(c)
	) {
		// create msg_id
		const uint32_t msg_id = randombytes_random();
		reg.emplace<Message::Components::ToxGroupMessageID>(new_msg_e, msg_id);

		// TODO: generalize?
		reg.emplace<Message::Components::SyncedBy>(new_msg_e).ts.emplace(c_self, ts);
	} else if (
		cr.any_of<Contact::Components::ToxGroupPeerEphemeral>(c)
	) {
		const auto& numbers = cr.get<Contact::Components::ToxGroupPeerEphemeral>(c);

		auto [message_id_opt, _] = _t.toxGroupSendPrivateMessage(
			numbers.group_number,
			numbers.peer_number,
			action ? Tox_Message_Type::TOX_MESSAGE_TYPE_ACTION : Tox_Message_Type::TOX_MESSAGE_TYPE_NORMAL,
			message
		);

		if (!message_id_opt.has_value()) {
			// set manually, so it can still be synced
			const uint32_t msg_id = randombytes_random();
			reg.emplace<Message::Components::ToxGroupMessageID>(new_msg_e, msg_id);

			std::cerr << "TMM: failed to send group message!\n";
		} else {
			// TODO: does group msg without msgid make sense???
			reg.emplace<Message::Components::ToxGroupMessageID>(new_msg_e, message_id_opt.value());

			// TODO: how do we do private messages?
			// same as friends?
			//reg.emplace<Message::Components::SyncedBy>(new_msg_e).ts.emplace(c_self, ts);
		}
	}

	_rmm.throwEventConstruct(reg, new_msg_e);
	return true;
}

bool ToxMessageManager::onToxEvent(const Tox_Event_Friend_Message* e) {
	uint32_t friend_number = tox_event_friend_message_get_friend_number(e);
	Tox_Message_Type type = tox_event_friend_message_get_type(e);

	// get current time unix epoch utc
	uint64_t ts = getTimeMS();

	std::string_view message {reinterpret_cast<const char*>(tox_event_friend_message_get_message(e)), tox_event_friend_message_get_message_length(e)};
	message = message.substr(0, message.find_first_of('\0')); // trim \0 // hi zoff
	// TODO: low-p, extract ts from zofftrim
	// TODO: sanitize utf8

	std::cout << "TMM friend message " << message << "\n";

	const auto c = _tcm.getContactFriend(friend_number);
	const auto self_c = c.get<Contact::Components::Self>().self;

	auto* reg_ptr = _rmm.get(c);
	if (reg_ptr == nullptr) {
		std::cerr << "TMM error: cant find reg\n";
		return false;
	}

	Message3Registry& reg = *reg_ptr;
	auto new_msg_e = reg.create();

	reg.emplace<Message::Components::ContactFrom>(new_msg_e, c);
	reg.emplace<Message::Components::ContactTo>(new_msg_e, self_c);

	reg.emplace<Message::Components::MessageText>(new_msg_e, message);

	if (type == Tox_Message_Type::TOX_MESSAGE_TYPE_ACTION) {
		reg.emplace<Message::Components::TagMessageIsAction>(new_msg_e);
	}

	reg.emplace<Message::Components::TimestampProcessed>(new_msg_e, ts);
	//reg.emplace<Components::TimestampWritten>(new_msg_e, 0);
	reg.emplace<Message::Components::Timestamp>(new_msg_e, ts); // reactive?
	{
		auto& rtr = reg.emplace<Message::Components::ReceivedBy>(new_msg_e).ts;
		rtr.try_emplace(self_c, ts);
		rtr.try_emplace(c, ts);
	}

	reg.emplace<Message::Components::TagUnread>(new_msg_e);

	c.emplace_or_replace<Contact::Components::LastActivity>(ts);

	_rmm.throwEventConstruct(reg, new_msg_e);
	return false; // TODO: return true?
}

bool ToxMessageManager::onToxEvent(const Tox_Event_Friend_Read_Receipt* e) {
	uint32_t friend_number = tox_event_friend_read_receipt_get_friend_number(e);
	uint32_t msg_id = tox_event_friend_read_receipt_get_message_id(e);

	// get current time unix epoch utc
	uint64_t ts = getTimeMS();

	const auto c = _tcm.getContactFriend(friend_number);
	const auto self_c = c.get<Contact::Components::Self>().self;

	auto* reg_ptr = _rmm.get(c);
	if (reg_ptr == nullptr) {
		std::cerr << "TMM error: cant find reg\n";
		return false;
	}

	Message3Registry& reg = *reg_ptr;

	// find message by message id
	// TODO: keep a short list of unconfirmed msg ids
	// this iterates in reverse, so newest messages should be pretty front
	for (const auto& [m, msg_id_comp] : reg.view<Message::Components::ToxFriendMessageID>().each()) {
		if (msg_id_comp.id == msg_id) {
			// found it
			auto& rtr = reg.get_or_emplace<Message::Components::ReceivedBy>(m);
			// insert but dont overwrite
			rtr.ts.try_emplace(c, ts);
			break;
		}
	}

	return true;
}

bool ToxMessageManager::onToxEvent(const Tox_Event_Group_Message* e) {
	const uint32_t group_number = tox_event_group_message_get_group_number(e);
	const uint32_t peer_number = tox_event_group_message_get_peer_id(e);
	const uint32_t message_id = tox_event_group_message_get_message_id(e);
	const Tox_Message_Type type = tox_event_group_message_get_message_type(e);

	const uint64_t ts = getTimeMS();

	auto message = std::string_view{reinterpret_cast<const char*>(tox_event_group_message_get_message(e)), tox_event_group_message_get_message_length(e)};
	std::cout << "TMM group message: " << message << "\n";

	const auto c = _tcm.getContactGroupPeer(group_number, peer_number);
	const auto self_c = c.get<Contact::Components::Self>().self;

	auto* reg_ptr = _rmm.get(c);
	//auto* reg_ptr = _rmm.get({ContactGroupPeerEphemeral{group_number, peer_number}});
	if (reg_ptr == nullptr) {
		std::cerr << "TMM error: cant find reg\n";
		return false;
	}

	Message3Registry& reg = *reg_ptr;
	// TODO: check for existence, hs or other syncing mechanics might have sent it already (or like, it arrived 2x or whatever)
	auto new_msg_e = reg.create();

	{ // contact
		// from
		reg.emplace<Message::Components::ContactFrom>(new_msg_e, c);

		// to
		reg.emplace<Message::Components::ContactTo>(new_msg_e, c.get<Contact::Components::Parent>().parent);
	}

	reg.emplace<Message::Components::ToxGroupMessageID>(new_msg_e, message_id);

	reg.emplace<Message::Components::MessageText>(new_msg_e, message);
	if (type == Tox_Message_Type::TOX_MESSAGE_TYPE_ACTION) {
		reg.emplace<Message::Components::TagMessageIsAction>(new_msg_e);
	}

	reg.emplace<Message::Components::TimestampProcessed>(new_msg_e, ts);
	//reg.emplace<Components::TimestampWritten>(new_msg_e, 0);
	reg.emplace<Message::Components::Timestamp>(new_msg_e, ts); // reactive?

	reg.emplace<Message::Components::TagUnread>(new_msg_e);

	{ // by whom
		reg.get_or_emplace<Message::Components::SyncedBy>(new_msg_e).ts.emplace(self_c, ts);
	}

	{
		auto& rtr = reg.emplace<Message::Components::ReceivedBy>(new_msg_e).ts;
		rtr.try_emplace(self_c, ts);
		rtr.try_emplace(c, ts);
	}

	c.emplace_or_replace<Contact::Components::LastActivity>(ts);

	_rmm.throwEventConstruct(reg, new_msg_e);
	return false; // TODO: true?
}

bool ToxMessageManager::onToxEvent(const Tox_Event_Group_Private_Message* e) {
	const uint32_t group_number = tox_event_group_private_message_get_group_number(e);
	const uint32_t peer_number = tox_event_group_private_message_get_peer_id(e);
	const Tox_Message_Type type = tox_event_group_private_message_get_message_type(e);

	const uint64_t ts = getTimeMS();

	auto message = std::string_view{reinterpret_cast<const char*>(tox_event_group_private_message_get_message(e)), tox_event_group_private_message_get_message_length(e)};
	std::cout << "TMM group private message: " << message << "\n";

	const auto c = _tcm.getContactGroupPeer(group_number, peer_number);
	const auto self_c = c.get<Contact::Components::Self>().self;

	auto* reg_ptr = _rmm.get(c);
	if (reg_ptr == nullptr) {
		std::cerr << "TMM error: cant find reg\n";
		return false;
	}

	Message3Registry& reg = *reg_ptr;
	auto new_msg_e = reg.create();

	{ // contact
		// from
		reg.emplace<Message::Components::ContactFrom>(new_msg_e, c);

		// to
		reg.emplace<Message::Components::ContactTo>(new_msg_e, self_c);
	}

	reg.emplace<Message::Components::MessageText>(new_msg_e, message);
	if (type == Tox_Message_Type::TOX_MESSAGE_TYPE_ACTION) {
		reg.emplace<Message::Components::TagMessageIsAction>(new_msg_e);
	}

	reg.emplace<Message::Components::TimestampProcessed>(new_msg_e, ts);
	//reg.emplace<Components::TimestampWritten>(new_msg_e, 0);
	reg.emplace<Message::Components::Timestamp>(new_msg_e, ts); // reactive?

	reg.emplace<Message::Components::TagUnread>(new_msg_e);

	// private does not track synced by
	// but receive state
	{
		auto& rtr = reg.emplace<Message::Components::ReceivedBy>(new_msg_e).ts;
		rtr.try_emplace(self_c, ts);
		rtr.try_emplace(c, ts);
	}

	c.emplace_or_replace<Contact::Components::LastActivity>(ts);

	_rmm.throwEventConstruct(reg, new_msg_e);
	return false;
}

