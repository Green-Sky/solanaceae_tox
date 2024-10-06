#include "./tox_transfer_manager.hpp"

#include <solanaceae/toxcore/tox_interface.hpp>

#include <solanaceae/file/file2_std.hpp>

#include <solanaceae/contact/components.hpp>
#include <solanaceae/tox_contacts/components.hpp>
#include <solanaceae/message3/components.hpp>
#include "./msg_components.hpp"
#include "./obj_components.hpp"

#include <sodium.h>

#include <filesystem>
#include <cassert>
#include <iostream>

// https://youtu.be/4XsL5iYHS6c

namespace Components {

	struct TFTFile2 {
		// the cached file2 for receiving/sending only
		std::unique_ptr<File2I> file;
	};

} // Components

void ToxTransferManager::toxFriendLookupAdd(ObjectHandle o) {
	const auto& comp = o.get<ObjComp::Ephemeral::ToxTransferFriend>();
	const uint64_t key {(uint64_t(comp.friend_number) << 32) | comp.transfer_number};

	if (o.all_of<ObjComp::Tox::TagOutgoing>()) {
		assert(!_friend_sending_lookup.count(key));
		_friend_sending_lookup[key] = o;
	}

	if (o.all_of<ObjComp::Tox::TagIncomming>()) {
		assert(!_friend_receiving_lookup.count(key));
		_friend_receiving_lookup[key] = o;
	}
}

void ToxTransferManager::toxFriendLookupRemove(ObjectHandle o) {
	const auto& comp = o.get<ObjComp::Ephemeral::ToxTransferFriend>();
	const uint64_t key {(uint64_t(comp.friend_number) << 32) | comp.transfer_number};

	if (o.all_of<ObjComp::Tox::TagOutgoing>()) {
		assert(_friend_sending_lookup.count(key));
		_friend_sending_lookup.erase(key);
	}

	if (o.all_of<ObjComp::Tox::TagIncomming>()) {
		assert(_friend_receiving_lookup.count(key));
		_friend_receiving_lookup.erase(key);
	}
}

ObjectHandle ToxTransferManager::toxFriendLookupSending(const uint32_t friend_number, const uint32_t file_number) const {
	const auto lookup_it = _friend_sending_lookup.find((uint64_t(friend_number) << 32) | file_number);
	if (lookup_it != _friend_sending_lookup.end()) {
		return lookup_it->second;
	} else {
		return {};
	}
}

ObjectHandle ToxTransferManager::toxFriendLookupReceiving(const uint32_t friend_number, const uint32_t file_number) const {
	const auto lookup_it = _friend_receiving_lookup.find((uint64_t(friend_number) << 32) | file_number);
	if (lookup_it != _friend_receiving_lookup.end()) {
		return lookup_it->second;
	} else {
		return {};
	}
}

ToxTransferManager::ToxTransferManager(
	RegistryMessageModelI& rmm,
	Contact3Registry& cr,
	ToxContactModel2& tcm,
	ToxI& t,
	ToxEventProviderI& tep,
	ObjectStore2& os
) : _rmm(rmm), _cr(cr), _tcm(tcm), _t(t), _os(os), _ftb(os) {
	tep.subscribe(this, Tox_Event_Type::TOX_EVENT_FRIEND_CONNECTION_STATUS);
	tep.subscribe(this, Tox_Event_Type::TOX_EVENT_FILE_RECV);
	tep.subscribe(this, Tox_Event_Type::TOX_EVENT_FILE_RECV_CONTROL);
	tep.subscribe(this, Tox_Event_Type::TOX_EVENT_FILE_RECV_CHUNK);
	tep.subscribe(this, Tox_Event_Type::TOX_EVENT_FILE_CHUNK_REQUEST);

	_os.subscribe(this, ObjectStore_Event::object_update);
	_os.subscribe(this, ObjectStore_Event::object_destroy);

	_rmm.subscribe(this, RegistryMessageModel_Event::send_file_path);
}

ToxTransferManager::~ToxTransferManager(void) {
}

void ToxTransferManager::iterate(void) {
	// TODO: time out transfers
}

Message3Handle ToxTransferManager::toxSendFilePath(const Contact3 c, uint32_t file_kind, std::string_view file_name, std::string_view file_path, std::vector<uint8_t> file_id) {
	if (
		// TODO: add support of offline queuing
		!_cr.all_of<Contact::Components::ToxFriendEphemeral>(c)
	) {
		std::cerr << "TTM error: unsupported contact type\n";
		return {};
	}

	auto* reg_ptr = _rmm.get(c);
	if (reg_ptr == nullptr) {
		return {};
	}

	auto file_impl = std::make_unique<File2RFile>(file_path);
	if (!file_impl->isGood()) {
		std::cerr << "TTM error: failed opening file '" << file_path << "'!\n";
		return {};
	}

	// get current time unix epoch utc
	uint64_t ts = Message::getTimeMS();

	if (file_id.empty()) {
		file_id.resize(32);
		randombytes_buf(file_id.data(), file_id.size());
	} else if (file_id.size() != 32) {
		// trunc or pad with zero
		file_id.resize(32);
	}
	assert(file_id.size() == 32);

	const auto c_self = _cr.get<Contact::Components::Self>(c).self;
	if (!_cr.valid(c_self)) {
		std::cerr << "TTM error: failed to get self!\n";
		return {};
	}

	// TODO: move this to backend
	ObjectHandle o {_os.registry(), _os.registry().create()};

	o.emplace<ObjComp::F::TagLocalHaveAll>();
	o.emplace<ObjComp::Tox::TagOutgoing>();
	o.emplace<ObjComp::Tox::FileKind>(file_kind);
	o.emplace<ObjComp::Tox::FileID>(file_id);

	// file info
	o.emplace<ObjComp::F::SingleInfo>(std::string{file_name}, file_impl->_file_size);
	o.emplace<ObjComp::F::SingleInfoLocal>(std::string{file_path});
	o.emplace<ObjComp::Ephemeral::FilePath>(std::string{file_path}); // ?

	o.emplace<ObjComp::Ephemeral::Backend>(&_ftb);

	o.emplace<ObjComp::Ephemeral::File::TransferStats>();

	// TODO: replace with better state tracking
	o.emplace<ObjComp::Ephemeral::File::TagTransferPaused>();

	Message3Handle msg {*reg_ptr, reg_ptr->create()};
	msg.emplace<Message::Components::ContactTo>(c);
	msg.emplace<Message::Components::ContactFrom>(c_self);
	msg.emplace<Message::Components::Timestamp>(ts); // reactive?
	msg.emplace<Message::Components::Read>(ts);
	msg.emplace<Message::Components::ReceivedBy>().ts.try_emplace(c_self, ts);
	msg.emplace<Message::Components::MessageFileObject>(o);

	const auto friend_number = _cr.get<Contact::Components::ToxFriendEphemeral>(c).friend_number;
	const auto&& [transfer_id, err] = _t.toxFileSend(friend_number, file_kind, file_impl->_file_size, file_id, file_name);
	if (err == TOX_ERR_FILE_SEND_OK) {
		assert(transfer_id.has_value());
		o.emplace<ObjComp::Ephemeral::ToxTransferFriend>(friend_number, transfer_id.value());
		o.emplace<Components::TFTFile2>(std::move(file_impl));
		// TODO: add tag signifying init sent status?

		toxFriendLookupAdd(o);
	} // else queue?

	_os.throwEventConstruct(o);
	_rmm.throwEventConstruct(msg);
	return msg;
}

bool ToxTransferManager::resume(ObjectHandle transfer) {
	if (!static_cast<bool>(transfer)) {
		std::cerr << "TTM error: resume() transfer " << entt::to_integral(transfer.entity()) << " is not a valid transfer\n";
		return false;
	}

	// TODO: test for paused?

	if (!transfer.all_of<ObjComp::Ephemeral::ToxTransferFriend>()) {
		std::cerr << "TTM error: resume() transfer " << entt::to_integral(transfer.entity()) << " ent does not have toxtransfer info\n";
		return false;
	}

	const auto [friend_number, transfer_number] = transfer.get<ObjComp::Ephemeral::ToxTransferFriend>();

	const auto err = _t.toxFileControl(friend_number, transfer_number, TOX_FILE_CONTROL_RESUME);
	if (err != TOX_ERR_FILE_CONTROL_OK) {
		std::cerr << "TTM error: resume() transfer " << entt::to_integral(transfer.entity()) << " tox file control error " << err << "\n";
		return false;
	}

	transfer.remove<ObjComp::Ephemeral::File::TagTransferPaused>();

	_os.throwEventUpdate(transfer);

	return true;
}

bool ToxTransferManager::pause(ObjectHandle transfer) {
	if (!static_cast<bool>(transfer)) {
		std::cerr << "TTM error: pause() transfer " << entt::to_integral(transfer.entity()) << " is not a valid transfer\n";
		return false;
	}

	// TODO: test for paused?

	if (!transfer.all_of<ObjComp::Ephemeral::ToxTransferFriend>()) {
		std::cerr << "TTM error: pause() transfer " << entt::to_integral(transfer.entity()) << " ent does not have toxtransfer info\n";
		return false;
	}

	const auto [friend_number, transfer_number] = transfer.get<ObjComp::Ephemeral::ToxTransferFriend>();

	const auto err = _t.toxFileControl(friend_number, transfer_number, TOX_FILE_CONTROL_PAUSE);
	if (err != TOX_ERR_FILE_CONTROL_OK) {
		std::cerr << "TTM error: pause() transfer " << entt::to_integral(transfer.entity()) << " tox file control error " << err << "\n";
		return false;
	}

	transfer.emplace_or_replace<ObjComp::Ephemeral::File::TagTransferPaused>();

	_os.throwEventUpdate(transfer);

	return true;
}

bool ToxTransferManager::setFileI(ObjectHandle transfer, std::unique_ptr<File2I>&& new_file) {
	if (!static_cast<bool>(transfer)) {
		std::cerr << "TTM error: setFileI() transfer " << entt::to_integral(transfer.entity()) << " is not a valid transfer\n";
		return false;
	}

	if (!new_file->isGood()) {
		std::cerr << "TTM error: failed setting new_file_impl!\n";
		return false;
	}

	transfer.emplace_or_replace<Components::TFTFile2>(std::move(new_file));

	_os.throwEventUpdate(transfer);

	return true;
}

bool ToxTransferManager::setFilePath(ObjectHandle transfer, std::string_view file_path) {
	if (!static_cast<bool>(transfer)) {
		std::cerr << "TTM error: setFilePath() transfer " << entt::to_integral(transfer.entity()) << " is not a valid transfer\n";
		return false;
	}

	uint64_t file_size {0};
	std::filesystem::path full_file_path{file_path};
	if (auto parent_path = full_file_path.parent_path(); !parent_path.empty()) {
		std::filesystem::create_directories(parent_path);
	}

	// TODO: read file name(s) from comp
	if (transfer.all_of<ObjComp::F::SingleInfo>()) {
		const auto& file_info = transfer.get<ObjComp::F::SingleInfo>();
		file_size = file_info.file_size;
	}

	transfer.emplace_or_replace<ObjComp::F::SingleInfoLocal>(full_file_path.u8string());
	transfer.emplace_or_replace<ObjComp::Ephemeral::FilePath>(full_file_path.u8string()); // ?

	// huh? we also set file2i ?
	auto file_impl = std::make_unique<File2RWFile>(full_file_path.u8string(), file_size, true);
	if (!file_impl->isGood()) {
		std::cerr << "TTM error: failed opening file '" << file_path << "'!\n";
		return false;
	}

	transfer.emplace_or_replace<Components::TFTFile2>(std::move(file_impl));

	// TODO: is this a good idea????
	_os.throwEventUpdate(transfer);

	return true;
}

bool ToxTransferManager::setFilePathDir(ObjectHandle transfer, std::string_view file_path) {
	if (!static_cast<bool>(transfer)) {
		std::cerr << "TTM error: setFilePathDir() transfer " << entt::to_integral(transfer.entity()) << " is not a valid transfer\n";
		return false;
	}

	uint64_t file_size {0};
	std::string full_file_path{file_path};
	// TODO: replace with filesystem or something
	// TODO: ensure dir exists
	if (full_file_path.back() != '/') {
		full_file_path += "/";
	}

	std::filesystem::create_directories(full_file_path);

	// TODO: read file name(s) from comp
	if (transfer.all_of<ObjComp::F::SingleInfo>()) {
		const auto& file_info = transfer.get<ObjComp::F::SingleInfo>();
		file_size = file_info.file_size;
		full_file_path += file_info.file_name;

	} else {
		std::cerr << "TTM warning: no FileInfo on transfer, using default\n";
		full_file_path += "file_recv.bin";
	}

	transfer.emplace_or_replace<ObjComp::F::SingleInfoLocal>(full_file_path);
	transfer.emplace_or_replace<ObjComp::Ephemeral::FilePath>(full_file_path); // ?

	// huh? we also set file2i ?
	auto file_impl = std::make_unique<File2RWFile>(full_file_path, file_size, true);
	if (!file_impl->isGood()) {
		std::cerr << "TTM error: failed opening file '" << file_path << "'!\n";
		return false;
	}

	transfer.emplace_or_replace<Components::TFTFile2>(std::move(file_impl));

	// TODO: is this a good idea???? - no lol, it was not
	_os.throwEventUpdate(transfer);

	return true;
}

bool ToxTransferManager::accept(ObjectHandle transfer, std::string_view file_path, bool path_is_file) {
	if (!static_cast<bool>(transfer)) {
		std::cerr << "TTM error: accepted transfer " << entt::to_integral(transfer.entity()) << " is not a valid transfer\n";
		return false;
	}

	if (!transfer.all_of<ObjComp::Tox::TagIncomming, ObjComp::Ephemeral::ToxTransferFriend>()) {
		std::cerr << "TTM error: accepted transfer " << entt::to_integral(transfer.entity()) << " is not a receiving transfer\n";
		return false;
	}

	if (transfer.any_of<Components::TFTFile2>()) {
		std::cerr << "TTM warning: overwriting existing file_impl " << entt::to_integral(transfer.entity()) << "\n";
	}

	if (path_is_file) {
		if (!setFilePath(transfer, file_path)) {
			std::cerr << "TTM error: accepted transfer " << entt::to_integral(transfer.entity()) << " failed setting path\n";
			return false;
		}
	} else {
		if (!setFilePathDir(transfer, file_path)) {
			std::cerr << "TTM error: accepted transfer " << entt::to_integral(transfer.entity()) << " failed setting path dir\n";
			return false;
		}
	}

	if (!resume(transfer)) {
		std::cerr << "TTM error: accepted transfer " << entt::to_integral(transfer.entity()) << " failed to resume\n";
		return false;
	}

	std::cout << "TTM info: accepted " << entt::to_integral(transfer.entity()) << ", saving to " << file_path << "\n";

	// setFilePathDir() and resume() throw events

	return true;
}

bool ToxTransferManager::sendFilePath(const Contact3 c, std::string_view file_name, std::string_view file_path) {
	if (
		// TODO: add support of offline queuing
		!_cr.all_of<Contact::Components::ToxFriendEphemeral>(c)
	) {
		// TODO: add support for persistant friend filesends
		return false;
	}

	return static_cast<bool>(toxSendFilePath(c, 0, file_name, file_path));
}

bool ToxTransferManager::onEvent(const ObjectStore::Events::ObjectUpdate& e) {
	if (_in_obj_update_event) {
		return false;
	}

	_in_obj_update_event = true;
	if (e.e.all_of<ObjComp::Ephemeral::File::ActionTransferAccept, ObjComp::Ephemeral::ToxTransferFriend>()) {
		accept(
			e.e,
			e.e.get<ObjComp::Ephemeral::File::ActionTransferAccept>().save_to_path,
			e.e.get<ObjComp::Ephemeral::File::ActionTransferAccept>().path_is_file
		);

		// should?
		e.e.remove<ObjComp::Ephemeral::File::ActionTransferAccept>();

		// TODO: recursion??
		// oh no, accept calls it 2x
		//_rmm.throwEventUpdate(
	}
	_in_obj_update_event = false;

	return false;
}

bool ToxTransferManager::onEvent(const ObjectStore::Events::ObjectDestory& e) {
	if (e.e.all_of<ObjComp::Ephemeral::ToxTransferFriend>()) {
		toxFriendLookupRemove(e.e);
	}

	return false;
}

bool ToxTransferManager::onToxEvent(const Tox_Event_Friend_Connection_Status* e) {
	const auto friend_number = tox_event_friend_connection_status_get_friend_number(e);
	const auto connection_status = tox_event_friend_connection_status_get_connection_status(e);

	if (connection_status == TOX_CONNECTION_NONE) {
		auto c = _tcm.getContactFriend(friend_number);

		std::vector<Object> to_destory;
		_os.registry().view<ObjComp::Ephemeral::ToxTransferFriend>().each([&](const Object ov, const auto& ttf) {
			if (ttf.friend_number == friend_number) {
				to_destory.push_back(ov);
				std::cerr << "TTM warning: friend disconnected, forcefully removing e:" << entt::to_integral(ov) << " frd:" << friend_number << " fnb:" << ttf.transfer_number << "\n";
			}
		});

		for (const auto ov : to_destory) {
			ObjectHandle o {_os.registry(), ov};

			// update lookup table
			toxFriendLookupRemove(o);

			o.remove<ObjComp::Ephemeral::ToxTransferFriend, Components::TFTFile2>();

			o.emplace_or_replace<ObjComp::Ephemeral::File::TagTransferPaused>();

			//_rmm.throwEventUpdate(*reg_ptr, ent);
			_os.throwEventUpdate(ov);
		}
	}

	return false; // always continue
}

bool ToxTransferManager::onToxEvent(const Tox_Event_File_Recv* e) {
	const auto friend_number = tox_event_file_recv_get_friend_number(e);
	const std::string_view file_name {
		reinterpret_cast<const char*>(tox_event_file_recv_get_filename(e)),
		tox_event_file_recv_get_filename_length(e)
	};
	const auto file_number = tox_event_file_recv_get_file_number(e);
	const auto file_size = tox_event_file_recv_get_file_size(e);
	const auto file_kind = tox_event_file_recv_get_kind(e);

	auto c = _tcm.getContactFriend(friend_number);
	auto* reg_ptr = _rmm.get(c);
	if (reg_ptr == nullptr) {
		return false;
	}

	// making sure, we dont have a dup
	ObjectHandle o {};
	_os.registry().view<ObjComp::Tox::TagIncomming, ObjComp::Ephemeral::ToxTransferFriend>().each([&](Object ov, const ObjComp::Ephemeral::ToxTransferFriend& ttf) {
		if (ttf.friend_number == friend_number && ttf.transfer_number == file_number) {
			o = {_os.registry(), ov};
		}
	});
	if (static_cast<bool>(o)) {
		std::cerr << "TTM error: existing file transfer frd:" << friend_number << " fnb:" << file_number << "\n";
		return false;
	}
	assert(!_friend_receiving_lookup.count((uint64_t(friend_number) << 32) | file_number));

	// TODO: also check for file id

	auto [f_id_opt, _] = _t.toxFileGetFileID(friend_number, file_number);
	//assert(f_id_opt.has_value());
	if (!f_id_opt.has_value()) {
		// very unfortuante, toxcore already forgot about the transfer we are handling
		// TODO: make sure we exit cracefully here
		std::cerr << "TTM error: querying for fileid failed, toxcore already forgot. frd:" << friend_number << " fnb:" << file_number << "\n";
		return false;
	}

	// get current time unix epoch utc
	uint64_t ts = Message::getTimeMS();

	auto self_c = _cr.get<Contact::Components::Self>(c).self;

	o = {_os.registry(), _os.registry().create()};

	o.emplace<ObjComp::Tox::TagIncomming>();
	o.emplace<ObjComp::Ephemeral::File::TagTransferPaused>();
	o.emplace<ObjComp::Ephemeral::ToxTransferFriend>(friend_number, file_number);
	o.emplace<ObjComp::Tox::FileKind>(file_kind);

	o.emplace<ObjComp::Tox::FileID>(f_id_opt.value());

	// file info
	o.emplace<ObjComp::F::SingleInfo>(std::string{file_name}, file_size);

	o.emplace<ObjComp::Ephemeral::File::TransferStats>();

	o.emplace<ObjComp::Ephemeral::Backend>(&_ftb);

	toxFriendLookupAdd(o);

	Message3Handle msg = {*reg_ptr, reg_ptr->create()};

	msg.emplace<Message::Components::ContactTo>(self_c);
	msg.emplace<Message::Components::ContactFrom>(c);
	msg.emplace<Message::Components::Timestamp>(ts); // reactive?
	msg.emplace<Message::Components::TagUnread>();
	{
		auto& rb = msg.emplace<Message::Components::ReceivedBy>().ts;
		//rb.try_emplace(self_c, ts); // only on completion
		rb.try_emplace(c, ts);
	}
	msg.emplace<Message::Components::MessageFileObject>(o);

	o.emplace<ObjComp::Ephemeral::ToxMessage>(msg);

	_os.throwEventConstruct(o);
	_rmm.throwEventConstruct(msg);

	return true;
}

bool ToxTransferManager::onToxEvent(const Tox_Event_File_Recv_Control* e) {
	const auto friend_number = tox_event_file_recv_control_get_friend_number(e);
	const auto file_number = tox_event_file_recv_control_get_file_number(e);
	const auto control = tox_event_file_recv_control_get_control(e);

	// first try sending
	ObjectHandle o = toxFriendLookupSending(friend_number, file_number);
	if (!static_cast<bool>(o)) {
		// then receiving
		o = toxFriendLookupReceiving(friend_number, file_number);
	}

	if (!static_cast<bool>(o)) {
		std::cerr << "TMM waring: control for unk ft\n";
		return false; // shrug, we don't know about it, might be someone else's
	}

	if (control == TOX_FILE_CONTROL_CANCEL) {
		std::cerr << "TTM: friend transfer canceled frd:" << friend_number << " fnb:" << file_number << "\n";

		// update lookup table
		toxFriendLookupRemove(o);

		o.remove<
			ObjComp::Ephemeral::ToxTransferFriend,
			Components::TFTFile2
		>();

		//_rmm.throwEventUpdate(transfer);
		_os.throwEventUpdate(o);
	} else if (control == TOX_FILE_CONTROL_PAUSE) {
		std::cerr << "TTM: friend transfer paused frd:" << friend_number << " fnb:" << file_number << "\n";
		// TODO: add distinction between local and remote pause
		o.emplace_or_replace<ObjComp::Ephemeral::File::TagTransferPaused>();
		//_rmm.throwEventUpdate(transfer);
		_os.throwEventUpdate(o);
	} else if (control == TOX_FILE_CONTROL_RESUME) {
		std::cerr << "TTM: friend transfer resumed frd:" << friend_number << " fnb:" << file_number << "\n";
		o.remove<ObjComp::Ephemeral::File::TagTransferPaused>();
		//_rmm.throwEventUpdate(transfer);
		_os.throwEventUpdate(o);
	}

	return true;
}

bool ToxTransferManager::onToxEvent(const Tox_Event_File_Recv_Chunk* e) {
	const auto friend_number = tox_event_file_recv_chunk_get_friend_number(e);
	const auto file_number = tox_event_file_recv_chunk_get_file_number(e);
	const uint8_t* data = tox_event_file_recv_chunk_get_data(e);
	const auto data_size = tox_event_file_recv_chunk_get_data_length(e);
	const auto position = tox_event_file_recv_chunk_get_position(e);

	ObjectHandle o = toxFriendLookupReceiving(friend_number, file_number);
	if (!static_cast<bool>(o)) {
		return false; // shrug, we don't know about it, might be someone else's
	}

	if (data_size == 0) {
		uint64_t ts = Message::getTimeMS();

		std::cout << "TTM finished friend " << friend_number << " transfer " << file_number << ", closing\n";

		// update lookup table
		toxFriendLookupRemove(o);

		o.remove<
			ObjComp::Ephemeral::ToxTransferFriend,
			Components::TFTFile2
		>();

		o.emplace_or_replace<ObjComp::F::TagLocalHaveAll>();

		_os.throwEventUpdate(o);

		// TODO: move out generic? do we want to update on EVERY chunk?
		if (const auto* msg_ptr = o.try_get<ObjComp::Ephemeral::ToxMessage>(); msg_ptr != nullptr && static_cast<bool>(msg_ptr->m)) {
			const auto& msg = msg_ptr->m;

			// re-unread a finished transfer
			msg.emplace_or_replace<Message::Components::TagUnread>();
			msg.remove<Message::Components::Read>();

			auto c = _tcm.getContactFriend(friend_number);
			if (static_cast<bool>(c)) {
				auto self_c = _cr.get<Contact::Components::Self>(c).self;
				auto& rb = msg.get_or_emplace<Message::Components::ReceivedBy>().ts;
				rb.try_emplace(self_c, ts); // on completion
			}

			_rmm.throwEventUpdate(msg);
		}
	} else if (!o.all_of<Components::TFTFile2>() || !o.get<Components::TFTFile2>().file || !o.get<Components::TFTFile2>().file->isGood()) {
		std::cerr << "TTM error: file not good f" << friend_number << " t" << file_number << ", closing\n";
		_t.toxFileControl(friend_number, file_number, Tox_File_Control::TOX_FILE_CONTROL_CANCEL);

		// update lookup table
		toxFriendLookupRemove(o);

		o.remove<
			ObjComp::Ephemeral::ToxTransferFriend,
			Components::TFTFile2
		>();

		_os.throwEventUpdate(o);
		// update messages?
	} else {
		auto& file = o.get<Components::TFTFile2>().file;
		const auto res = file->write({data, data_size}, position);
		//o.get<Message::Components::Transfer::BytesReceived>().total += data_size;
		o.get_or_emplace<ObjComp::Ephemeral::File::TransferStats>().total_down += data_size;

		// queue?
		_os.throwEventUpdate(o);
		//_rmm.throwEventUpdate(msg);
	}

	return true;
}

bool ToxTransferManager::onToxEvent(const Tox_Event_File_Chunk_Request* e) {
	const auto friend_number = tox_event_file_chunk_request_get_friend_number(e);
	const auto file_number = tox_event_file_chunk_request_get_file_number(e);
	const auto position = tox_event_file_chunk_request_get_position(e);
	const auto data_size = tox_event_file_chunk_request_get_length(e);

	ObjectHandle o = toxFriendLookupSending(friend_number, file_number);
	if (!static_cast<bool>(o)) {
		std::cerr << "TTM warning: chunk request for unk ft\n";
		return false; // shrug, we don't know about it, might be someone else's
	}

	// tox wants us to end the transmission
	if (data_size == 0) {
		std::cout << "TTM finished friend " << friend_number << " transfer " << file_number << ", closing\n";

		// update lookup table
		toxFriendLookupRemove(o);

		o.remove<
			ObjComp::Ephemeral::ToxTransferFriend,
			Components::TFTFile2
		>();

		// TODO: add tag finished?
		//_rmm.throwEventUpdate(o);
		_os.throwEventUpdate(o);
	} else if (!o.all_of<Components::TFTFile2>() || !o.get<Components::TFTFile2>().file || !o.get<Components::TFTFile2>().file->isGood()) {
		std::cerr << "TTM error: file not good f" << friend_number << " t" << file_number << ", closing\n";
		_t.toxFileControl(friend_number, file_number, Tox_File_Control::TOX_FILE_CONTROL_CANCEL);

		// update lookup table
		toxFriendLookupRemove(o);

		o.remove<
			ObjComp::Ephemeral::ToxTransferFriend,
			Components::TFTFile2
		>();

		//_rmm.throwEventUpdate(o);
		_os.throwEventUpdate(o);
	} else {
		auto& file = o.get<Components::TFTFile2>().file;
		const auto data = file->read(data_size, position);
		if (data.empty()) {
			std::cerr << "TMM error: failed to read file!!\n";
			return true;
		}

		// TODO: get rid of the data cast and support spans in the tox api
		const auto err = _t.toxFileSendChunk(friend_number, file_number, position, static_cast<std::vector<uint8_t>>(data));
		// TODO: investigate if i need to retry if sendq full
		if (err == TOX_ERR_FILE_SEND_CHUNK_OK) {
			o.get_or_emplace<ObjComp::Ephemeral::File::TransferStats>().total_up += data_size;
			_os.throwEventUpdate(o);
		}
	}

	return true;
}

