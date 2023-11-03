#include "./tox_contact_model2.hpp"

#include <solanaceae/toxcore/tox_interface.hpp>
#include <solanaceae/contact/components.hpp>

#include "./components.hpp"

#include <algorithm>
#include <string_view>
#include <iostream>

ToxContactModel2::ToxContactModel2(Contact3Registry& cr, ToxI& t, ToxEventProviderI& tep) : _cr(cr), _t(t), _tep(tep) {
	_tep.subscribe(this, Tox_Event::TOX_EVENT_FRIEND_CONNECTION_STATUS);
	_tep.subscribe(this, Tox_Event::TOX_EVENT_FRIEND_STATUS);
	_tep.subscribe(this, Tox_Event::TOX_EVENT_FRIEND_NAME);
	_tep.subscribe(this, Tox_Event::TOX_EVENT_FRIEND_REQUEST);

	// TODO: conf

	_tep.subscribe(this, Tox_Event::TOX_EVENT_GROUP_INVITE);
	_tep.subscribe(this, Tox_Event::TOX_EVENT_GROUP_SELF_JOIN);
	_tep.subscribe(this, Tox_Event::TOX_EVENT_GROUP_PEER_JOIN);
	_tep.subscribe(this, Tox_Event::TOX_EVENT_GROUP_PEER_EXIT);
	_tep.subscribe(this, Tox_Event::TOX_EVENT_GROUP_PEER_NAME);


	// add self
	Contact3 c = entt::null;
	// TODO: if self exists

	c = _cr.create();
	_cr.emplace<Contact::Components::ContactModel>(c, this);
	_cr.emplace<Contact::Components::TagSelfStrong>(c);
	_cr.emplace<Contact::Components::Name>(c, _t.toxSelfGetName());
	_cr.emplace<Contact::Components::ID>(c, _t.toxSelfGetPublicKey());

	_friend_self = c;

	// fill in contacts
	for (const uint32_t f_id : _t.toxSelfGetFriendList()) {
		getContactFriend(f_id);
	}

	for (const uint32_t g_id : _t.toxGroupGetList()) {
		getContactGroup(g_id);
	}
}

ToxContactModel2::~ToxContactModel2(void) {
}

void ToxContactModel2::iterate(float delta) {
	// continually fetch group peer connection state, since JF does not want to add cb/event
	_group_status_timer += delta;
	// every second
	if (_group_status_timer >= 1.f) {
		_group_status_timer = 0.f;

		_cr.view<Contact::Components::ToxGroupPeerEphemeral, Contact::Components::ConnectionState>().each([this](auto c, const auto& tox_peer, auto& con) {
			auto state_opt = std::get<0>(_t.toxGroupPeerGetConnectionStatus(tox_peer.group_number, tox_peer.peer_number));
			if (state_opt.has_value()) {
				if (state_opt.value() == TOX_CONNECTION_UDP) {
					con.state = Contact::Components::ConnectionState::State::direct;
				} else if (state_opt.value() == TOX_CONNECTION_TCP) {
					con.state = Contact::Components::ConnectionState::State::cloud;
				}
			}
		});
	}
}

void ToxContactModel2::acceptRequest(Contact3 c, std::string_view self_name, std::string_view password) {
	assert(!_cr.any_of<Contact::Components::ToxFriendEphemeral>(c));
	assert(_cr.all_of<Contact::Components::RequestIncoming>(c));

	if (_cr.all_of<Contact::Components::ToxFriendPersistent>(c)) {
		const auto& key = _cr.get<Contact::Components::ToxFriendPersistent>(c).key.data;
		auto [friend_number_opt, _] = _t.toxFriendAddNorequest({key.cbegin(), key.cend()});
		if (friend_number_opt.has_value()) {
			_cr.emplace<Contact::Components::ToxFriendEphemeral>(c, friend_number_opt.value());
			_cr.remove<Contact::Components::RequestIncoming>(c);
		} else {
			std::cerr << "TCM2 error: failed to accept friend request/invite\n";
		}
	} else if (false) { // conf
	} else if (_cr.all_of<Contact::Components::ToxGroupIncomingRequest>(c)) { // group
		const auto& ir = _cr.get<Contact::Components::ToxGroupIncomingRequest>(c);
		auto [group_number_opt, _] = _t.toxGroupInviteAccept(ir.friend_number, ir.invite_data, self_name, password);
		if (group_number_opt.has_value()) {
			_cr.emplace<Contact::Components::ToxGroupEphemeral>(c, group_number_opt.value());

			if (auto group_chatid_opt = _t.toxGroupGetChatId(group_number_opt.value()); group_chatid_opt.has_value()) {
				_cr.emplace_or_replace<Contact::Components::ToxGroupPersistent>(c, group_chatid_opt.value());
			} else {
				std::cerr << "TCM2 error: getting chatid for group" << group_number_opt.value() << "!!\n";
			}

			if (auto self_opt = _t.toxGroupSelfGetPeerId(group_number_opt.value()); self_opt.has_value()) {
				_cr.emplace_or_replace<Contact::Components::Self>(c, getContactGroupPeer(group_number_opt.value(), self_opt.value()));
			} else {
				std::cerr << "TCM2 error: getting self for group" << group_number_opt.value() << "!!\n";
			}

			_cr.remove<Contact::Components::ToxGroupIncomingRequest>(c);
			_cr.remove<Contact::Components::RequestIncoming>(c);
		} else {
			std::cerr << "TCM2 error: failed to accept group request/invite\n";
		}
	} else {
		std::cerr << "TCM2 error: failed to accept request (unk)\n";
	}
}

Contact3Handle ToxContactModel2::getContactFriend(uint32_t friend_number) {
	Contact3 c = entt::null;

	// first check contacts with friend id
	// TODO: lookup table
	//_cr.view<Contact::Components::ToxFriendEphemeral>().each([&c, friend_number](const Contact3 e, const Contact::Components::ToxFriendEphemeral& f_id) {
	for (const auto e : _cr.view<Contact::Components::ToxFriendEphemeral>()) {
		if (_cr.get<Contact::Components::ToxFriendEphemeral>(e).friend_number == friend_number) {
			c = e;
			break;
		}
	}

	if (_cr.valid(c)) {
		return {_cr, c};
	}

	// else check by pubkey
	auto f_key_opt = _t.toxFriendGetPublicKey(friend_number);
	assert(f_key_opt.has_value()); // TODO: handle gracefully?

	const ToxKey& f_key = f_key_opt.value();
	//_cr.view<Contact::Components::ToxFriendPersistent>().each([&c, &f_key](const Contact3 e, const Contact::Components::ToxFriendPersistent& f_key_comp) {
	for (const auto e : _cr.view<Contact::Components::ToxFriendPersistent>()) {
		if (f_key == _cr.get<Contact::Components::ToxFriendPersistent>(e).key) {
			c = e;
			break;
		}
	}

	if (_cr.valid(c)) {
		// param friend number matches pubkey in db, add
		_cr.emplace_or_replace<Contact::Components::ToxFriendEphemeral>(c, friend_number);

		return {_cr, c};
	}

	// else, new ent
	c = _cr.create();

	_cr.emplace<Contact::Components::TagBig>(c);
	_cr.emplace<Contact::Components::ContactModel>(c, this);
	_cr.emplace<Contact::Components::ToxFriendEphemeral>(c, friend_number);
	_cr.emplace<Contact::Components::ToxFriendPersistent>(c, f_key);
	_cr.emplace<Contact::Components::ID>(c, f_key_opt.value());
	_cr.emplace<Contact::Components::Self>(c, _friend_self);
	_cr.emplace<Contact::Components::Name>(c, _t.toxFriendGetName(friend_number).value_or("<unk>"));

	std::cout << "TCM2: created friend contact " << friend_number << "\n";

	return {_cr, c};
}

Contact3Handle ToxContactModel2::getContactGroup(uint32_t group_number) {
	Contact3 c = entt::null;

	// first check contacts with group_number
	// TODO: lookup table
	//_cr.view<Contact::Components::ToxGroupEphemeral>().each([&c, group_number](const Contact3 e, const Contact::Components::ToxGroupEphemeral& g_e) {
	for (const auto e : _cr.view<Contact::Components::ToxGroupEphemeral>()) {
		if (_cr.get<Contact::Components::ToxGroupEphemeral>(e).group_number == group_number) {
			c = e;
			break;
		}
	}

	if (_cr.valid(c)) {
		return {_cr, c};
	}

	// else check by pubkey
	auto g_key_opt = _t.toxGroupGetChatId(group_number);
	assert(g_key_opt.has_value()); // TODO: handle gracefully?

	const ToxKey& g_key = g_key_opt.value();
	//_cr.view<Contact::Components::ToxGroupPersistent>().each([&c, &g_key](const Contact3 e, const Contact::Components::ToxGroupPersistent& g_key_comp) {
	for (const auto e : _cr.view<Contact::Components::ToxGroupPersistent>()) {
		if (g_key == _cr.get<Contact::Components::ToxGroupPersistent>(e).chat_id) {
			c = e;
			break;
		}
	}

	if (_cr.valid(c)) {
		// param group number matches pubkey in db, add
		_cr.emplace_or_replace<Contact::Components::ToxGroupEphemeral>(c, group_number);

		return {_cr, c};
	}

	// else, new ent
	c = _cr.create();

	_cr.emplace<Contact::Components::ContactModel>(c, this);
	_cr.emplace<Contact::Components::TagBig>(c);
	_cr.emplace<Contact::Components::ParentOf>(c); // start empty
	_cr.emplace<Contact::Components::ToxGroupEphemeral>(c, group_number);
	_cr.emplace<Contact::Components::ToxGroupPersistent>(c, g_key);
	_cr.emplace<Contact::Components::ID>(c, g_key_opt.value());
	_cr.emplace<Contact::Components::Name>(c, _t.toxGroupGetName(group_number).value_or("<unk>"));
	_cr.emplace<Contact::Components::ConnectionState>(
		c,
		_t.toxGroupIsConnected(group_number).value_or(false)
			? Contact::Components::ConnectionState::State::cloud
			: Contact::Components::ConnectionState::State::disconnected
	);

	auto self_opt = _t.toxGroupSelfGetPeerId(group_number);
	if (self_opt.has_value()) {
		_cr.emplace<Contact::Components::Self>(c, getContactGroupPeer(group_number, self_opt.value()));
	} else {
		std::cerr << "TCM2 error: getting self for group" << group_number << "!!\n";
	}

	std::cout << "TCM2: created group contact " << group_number << "\n";

	return {_cr, c};
}

Contact3Handle ToxContactModel2::getContactGroupPeer(uint32_t group_number, uint32_t peer_number) {
	Contact3 c = entt::null;

	Contact3Handle group_c = getContactGroup(group_number);

	assert(static_cast<bool>(group_c));

	// first check contacts with peer id
	// TODO: lookup table
	//_cr.view<Contact::Components::ToxGroupPeerEphemeral>().each([&c, group_number, peer_number](const Contact3 e, const Contact::Components::ToxGroupPeerEphemeral& p_comp) {
	for (const auto e : _cr.view<Contact::Components::ToxGroupPeerEphemeral>()) {
		const auto& p_comp = _cr.get<Contact::Components::ToxGroupPeerEphemeral>(e);
		if (p_comp.group_number == group_number && p_comp.peer_number == peer_number) {
			c = e;
			break;
		}
	}

	if (_cr.valid(c)) {
		return {_cr, c};
	}

	const auto& g_key = group_c.get<Contact::Components::ToxGroupPersistent>().chat_id;

	// else check by key
	auto [g_p_key_opt, _] = _t.toxGroupPeerGetPublicKey(group_number, peer_number);
	//assert(g_p_key_opt.has_value()); // TODO: handle gracefully?
	if (!g_p_key_opt.has_value()) {
		// if the key could not be retreived, that means the peer has exited (idk why the earlier search did not work, it should have)
		// also exit here, to not create, pubkey less <.<
		std::cerr << "TCM2 error: we did not have offline peer in db, which is worrying\n";
		return {};
	}

	const ToxKey& g_p_key = g_p_key_opt.value();
	//_cr.view<Contact::Components::ToxGroupPeerPersistent>().each([&c, &g_key, &g_p_key](const Contact3 e, const Contact::Components::ToxGroupPeerPersistent& g_p_key_comp) {
	for (const auto e : _cr.view<Contact::Components::ToxGroupPeerPersistent>()) {
		const auto& g_p_key_comp = _cr.get<Contact::Components::ToxGroupPeerPersistent>(e);
		if (g_p_key == g_p_key_comp.peer_key && g_key == g_p_key_comp.chat_id) {
			c = e;
			break;
		}
	}

	if (_cr.valid(c)) {
		// param numbers matches pubkey in db, add
		_cr.emplace_or_replace<Contact::Components::ToxGroupPeerEphemeral>(c, group_number, peer_number);

		return {_cr, c};
	}

	// else, new ent
	c = _cr.create();

	_cr.emplace<Contact::Components::Parent>(c, group_c);
	{ // add sub to parent
		auto& parent_sub_list = group_c.get_or_emplace<Contact::Components::ParentOf>().subs;
		if (std::find(parent_sub_list.cbegin(), parent_sub_list.cend(), c) == parent_sub_list.cend()) {
			parent_sub_list.push_back(c);
		}
	}
	_cr.emplace<Contact::Components::ContactModel>(c, this);
	_cr.emplace<Contact::Components::ToxGroupPeerEphemeral>(c, group_number, peer_number);
	_cr.emplace<Contact::Components::ToxGroupPeerPersistent>(c, g_key, g_p_key);
	_cr.emplace<Contact::Components::ID>(c, g_p_key_opt.value());
	const auto name_opt = std::get<0>(_t.toxGroupPeerGetName(group_number, peer_number));
	if (name_opt.has_value()) {
		_cr.emplace<Contact::Components::Name>(c, name_opt.value());
	}

	{ // self
		// TODO: this is very flaky
		auto self_number_opt = _t.toxGroupSelfGetPeerId(group_number);
		if (peer_number == self_number_opt.value()) {
			_cr.emplace<Contact::Components::TagSelfStrong>(c);
		} else {
			_cr.emplace<Contact::Components::Self>(c, getContactGroupPeer(group_number, self_number_opt.value()));
		}
	}

	std::cout << "TCM2: created group peer contact " << group_number << " " << peer_number << "\n";

	return {_cr, c};
}

//Contact3Handle ToxContactModel2::getContactGroupPeer(const ToxKey& group_key, const ToxKey& peer_key) {
	//return {};
//}

Contact3Handle ToxContactModel2::getContactGroupPeer(uint32_t group_number, const ToxKey& peer_key) {
	Contact3 c = entt::null;

	Contact3Handle group_c = getContactGroup(group_number);

	assert(static_cast<bool>(group_c));

	const auto& g_key = group_c.get<Contact::Components::ToxGroupPersistent>().chat_id;

	// search by key
	//_cr.view<Contact::Components::ToxGroupPeerPersistent>().each([&c, &g_key, &peer_key](const Contact3 e, const Contact::Components::ToxGroupPeerPersistent& g_p_key_comp) {
	for (const auto e : _cr.view<Contact::Components::ToxGroupPeerPersistent>()) {
		const auto& g_p_key_comp = _cr.get<Contact::Components::ToxGroupPeerPersistent>(e);
		if (peer_key == g_p_key_comp.peer_key && g_key == g_p_key_comp.chat_id) {
			c = e;
			break;
		}
	}

	if (_cr.valid(c)) {
		return {_cr, c};
	}

	// TODO: maybe not create contacts via history sync
	// else, new ent
	c = _cr.create();

	_cr.emplace<Contact::Components::Parent>(c, group_c);
	{ // add sub to parent
		auto& parent_sub_list = group_c.get_or_emplace<Contact::Components::ParentOf>().subs;
		if (std::find(parent_sub_list.cbegin(), parent_sub_list.cend(), c) == parent_sub_list.cend()) {
			parent_sub_list.push_back(c);
		}
	}
	_cr.emplace<Contact::Components::ContactModel>(c, this);
	//_cr.emplace<Contact::Components::ToxGroupPeerEphemeral>(c, group_number, peer_number);
	_cr.emplace<Contact::Components::ToxGroupPeerPersistent>(c, g_key, peer_key);
	_cr.emplace<Contact::Components::ID>(c, std::vector<uint8_t>{peer_key.data.cbegin(), peer_key.data.cend()});
	//_cr.emplace<Contact::Components::Name>(c, "<unk>");
	//_cr.emplace<Contact::Components::Name>(c, std::get<0>(_t.toxGroupPeerGetName(group_number, peer_number)).value_or("<unk>"));

	{ // self
		// TODO: this is very flaky
		auto self_number_opt = _t.toxGroupSelfGetPeerId(group_number);
		_cr.emplace<Contact::Components::Self>(c, getContactGroupPeer(group_number, self_number_opt.value()));
	}

	std::cout << "TCM2: created group peer contact via pubkey " << group_number << "\n";

	return {_cr, c};
}

bool ToxContactModel2::onToxEvent(const Tox_Event_Friend_Connection_Status* e) {
	const Tox_Connection connection_status = tox_event_friend_connection_status_get_connection_status(e);
	auto c = getContactFriend(tox_event_friend_connection_status_get_friend_number(e));

	c.emplace_or_replace<Contact::Components::ConnectionState>(
		(connection_status == TOX_CONNECTION_NONE) ? Contact::Components::ConnectionState::State::disconnected :
		(connection_status == TOX_CONNECTION_UDP) ? Contact::Components::ConnectionState::State::direct :
		Contact::Components::ConnectionState::State::cloud
	);

	if (connection_status == TOX_CONNECTION_NONE) {
		c.remove<Contact::Components::ToxFriendEphemeral>();
	}

	return false;
}

bool ToxContactModel2::onToxEvent(const Tox_Event_Friend_Status* e) {
	//tox_event_friend_status_get_status(e);

	//TOX_USER_STATUS_NONE,
	//TOX_USER_STATUS_AWAY,
	//TOX_USER_STATUS_BUSY,

	//auto c = getContactFriend(tox_event_friend_status_get_friend_number(e));
	return false;
}

bool ToxContactModel2::onToxEvent(const Tox_Event_Friend_Name* e) {
	const std::string_view name {
		reinterpret_cast<const char*>(tox_event_friend_name_get_name(e)),
		tox_event_friend_name_get_name_length(e)
	};

	auto c = getContactFriend(tox_event_friend_name_get_friend_number(e));
	c.emplace_or_replace<Contact::Components::Name>(std::string{name});

	return false; // return true?
}

bool ToxContactModel2::onToxEvent(const Tox_Event_Friend_Request* e) {
	const ToxKey pub_key{tox_event_friend_request_get_public_key(e), TOX_PUBLIC_KEY_SIZE};

	Contact3 c = entt::null;

	// check for existing
	for (const auto e : _cr.view<Contact::Components::ToxFriendPersistent>()) {
		if (pub_key == _cr.get<Contact::Components::ToxFriendPersistent>(e).key) {
			c = e;
			break;
		}
	}

	if (_cr.valid(c)) {
		_cr.emplace_or_replace<Contact::Components::RequestIncoming>(c);
		_cr.remove<Contact::Components::ToxFriendEphemeral>(c);

		std::cout << "TCM2: marked friend contact as requested\n";
	} else {
		// else, new ent
		c = _cr.create();

		_cr.emplace<Contact::Components::RequestIncoming>(c);
		_cr.emplace<Contact::Components::TagBig>(c);
		_cr.emplace<Contact::Components::ContactModel>(c, this);
		_cr.emplace<Contact::Components::ToxFriendPersistent>(c, pub_key);
		_cr.emplace<Contact::Components::ID>(c, std::vector<uint8_t>{pub_key.data.cbegin(), pub_key.data.cend()});
		_cr.emplace<Contact::Components::Self>(c, _friend_self);

		std::cout << "TCM2: created friend contact (requested)\n";
	}

	return false; // return false, so tox_message can handle the message
}

bool ToxContactModel2::onToxEvent(const Tox_Event_Group_Invite* e) {
	const std::string_view group_name {
		reinterpret_cast<const char*>(tox_event_group_invite_get_group_name(e)),
		tox_event_group_invite_get_group_name_length(e)
	};

	// HACK: extract chatid
	// TODO: request better api
	assert(tox_event_group_invite_get_invite_data_length(e) == TOX_GROUP_CHAT_ID_SIZE + TOX_GROUP_PEER_PUBLIC_KEY_SIZE);
	const ToxKey chat_id{tox_event_group_invite_get_invite_data(e), TOX_GROUP_CHAT_ID_SIZE};

	Contact3 c = entt::null;

	// check for existing
	for (const auto e : _cr.view<Contact::Components::ToxGroupPersistent>()) {
		if (chat_id == _cr.get<Contact::Components::ToxGroupPersistent>(e).chat_id) {
			c = e;
			break;
		}
	}

	if (_cr.valid(c)) {
		std::cout << "TCM2: already in group from invite\n";
	} else {
		// else, new ent
		c = _cr.create();

		_cr.emplace<Contact::Components::RequestIncoming>(c, true, true);
		_cr.emplace<Contact::Components::TagBig>(c);
		_cr.emplace<Contact::Components::ContactModel>(c, this);
		_cr.emplace<Contact::Components::ToxGroupPersistent>(c, chat_id);
		_cr.emplace<Contact::Components::ID>(c, std::vector<uint8_t>{chat_id.data.cbegin(), chat_id.data.cend()});
		_cr.emplace<Contact::Components::Name>(c, std::string(group_name));

		auto& ir = _cr.emplace<Contact::Components::ToxGroupIncomingRequest>(c);
		ir.friend_number = tox_event_group_invite_get_friend_number(e);
		ir.invite_data = {
			tox_event_group_invite_get_invite_data(e),
			tox_event_group_invite_get_invite_data(e) + tox_event_group_invite_get_invite_data_length(e)
		};

		// TODO: self
		//_cr.emplace<Contact::Components::Self>(c, _friend_self);

		std::cout << "TCM2: created group contact (requested)\n";
	}

	return false;
}

bool ToxContactModel2::onToxEvent(const Tox_Event_Group_Self_Join* e) {
	const uint32_t group_number = tox_event_group_self_join_get_group_number(e);
	if (const auto self_id_opt = _t.toxGroupSelfGetPeerId(group_number); self_id_opt.has_value()) {
		auto c = getContactGroupPeer(group_number, self_id_opt.value());
		c.emplace_or_replace<Contact::Components::TagSelfStrong>();
		c.emplace_or_replace<Contact::Components::ConnectionState>(Contact::Components::ConnectionState::State::direct);

		auto gc = getContactGroup(group_number);
		assert(static_cast<bool>(gc)); // should be no failure mode
		gc.emplace_or_replace<Contact::Components::ConnectionState>(
			_t.toxGroupIsConnected(group_number).value_or(false)
				? Contact::Components::ConnectionState::State::cloud
				: Contact::Components::ConnectionState::State::disconnected
		);
	} else {
		assert(false);
	}

	return false;
}

bool ToxContactModel2::onToxEvent(const Tox_Event_Group_Peer_Join* e) {
	const uint32_t group_number = tox_event_group_peer_join_get_group_number(e);
	const uint32_t peer_number = tox_event_group_peer_join_get_peer_id(e);

	auto c = getContactGroupPeer(group_number, peer_number);

	// ensure its set
	c.emplace_or_replace<Contact::Components::ToxGroupPeerEphemeral>(group_number, peer_number);

	auto [peer_state_opt, _] = _t.toxGroupPeerGetConnectionStatus(group_number, peer_number);
	c.emplace_or_replace<Contact::Components::ConnectionState>(
		(peer_state_opt.value_or(TOX_CONNECTION_NONE) == TOX_CONNECTION_NONE) ? Contact::Components::ConnectionState::State::disconnected :
		(peer_state_opt.value_or(TOX_CONNECTION_NONE) == TOX_CONNECTION_UDP) ? Contact::Components::ConnectionState::State::direct :
		Contact::Components::ConnectionState::State::cloud
	);

	// update name
	const auto name_opt = std::get<0>(_t.toxGroupPeerGetName(group_number, peer_number));
	if (name_opt.has_value()) {
		_cr.emplace_or_replace<Contact::Components::Name>(c, name_opt.value());
	}

	return false;
}

bool ToxContactModel2::onToxEvent(const Tox_Event_Group_Peer_Exit* e) {
	const uint32_t group_number = tox_event_group_peer_exit_get_group_number(e);
	const uint32_t peer_number = tox_event_group_peer_exit_get_peer_id(e);
	const auto exit_type = tox_event_group_peer_exit_get_exit_type(e);
	// set name?
	// we dont care about the part messae?

	if (exit_type == Tox_Group_Exit_Type::TOX_GROUP_EXIT_TYPE_SELF_DISCONNECTED) {
		// you disconnected intentionally, or you where kicked
		// TODO: we need to remove all ToxGroupPeerEphemeral components of that group
	} else {
		auto c = getContactGroupPeer(group_number, peer_number);

		if (!static_cast<bool>(c)) {
			return false; // we dont track this contact ?????
		}

		c.emplace_or_replace<Contact::Components::ConnectionState>(Contact::Components::ConnectionState::State::disconnected);
		c.remove<Contact::Components::ToxGroupPeerEphemeral>();

		// they where kicked
		// exit_type == Tox_Group_Exit_Type::TOX_GROUP_EXIT_TYPE_KICK
	}

	return false;
}

bool ToxContactModel2::onToxEvent(const Tox_Event_Group_Peer_Name* e) {
	const uint32_t group_number = tox_event_group_peer_name_get_group_number(e);
	const uint32_t peer_number = tox_event_group_peer_name_get_peer_id(e);

	const std::string_view name {
		reinterpret_cast<const char*>(tox_event_group_peer_name_get_name(e)),
		tox_event_group_peer_name_get_name_length(e)
	};

	auto c = getContactGroupPeer(group_number, peer_number);

	c.emplace_or_replace<Contact::Components::Name>(std::string{name});

	return false;
}

