#include "./tox_transfer_manager.hpp"

#include <filesystem>
#include <solanaceae/toxcore/tox_interface.hpp>

#include <solanaceae/message3/file_r_file.hpp>
#include <solanaceae/message3/file_w_file.hpp>

#include <solanaceae/contact/components.hpp>
#include <solanaceae/tox_contacts/components.hpp>
#include <solanaceae/message3/components.hpp>
#include "./components.hpp"

#include <sodium.h>

#include <chrono>
#include <cassert>
#include <iostream>

// https://youtu.be/4XsL5iYHS6c

void ToxTransferManager::toxFriendLookupAdd(Message3Handle h) {
	const auto& comp = h.get<Message::Components::Transfer::ToxTransferFriend>();
	const uint64_t key {(uint64_t(comp.friend_number) << 32) | comp.transfer_number};

	if (h.all_of<Message::Components::Transfer::TagSending>()) {
		assert(!_friend_sending_lookup.count(key));
		_friend_sending_lookup[key] = h;
	}

	if (h.all_of<Message::Components::Transfer::TagReceiving>()) {
		assert(!_friend_receiving_lookup.count(key));
		_friend_receiving_lookup[key] = h;
	}
}

void ToxTransferManager::toxFriendLookupRemove(Message3Handle h) {
	const auto& comp = h.get<Message::Components::Transfer::ToxTransferFriend>();
	const uint64_t key {(uint64_t(comp.friend_number) << 32) | comp.transfer_number};

	if (h.all_of<Message::Components::Transfer::TagSending>()) {
		assert(_friend_sending_lookup.count(key));
		_friend_sending_lookup.erase(key);
	}

	if (h.all_of<Message::Components::Transfer::TagReceiving>()) {
		assert(_friend_receiving_lookup.count(key));
		_friend_receiving_lookup.erase(key);
	}
}

Message3Handle ToxTransferManager::toxFriendLookupSending(const uint32_t friend_number, const uint32_t file_number) const {
	const auto lookup_it = _friend_sending_lookup.find((uint64_t(friend_number) << 32) | file_number);
	if (lookup_it != _friend_sending_lookup.end()) {
		return lookup_it->second;
	} else {
		return {};
	}
}

Message3Handle ToxTransferManager::toxFriendLookupReceiving(const uint32_t friend_number, const uint32_t file_number) const {
	const auto lookup_it = _friend_receiving_lookup.find((uint64_t(friend_number) << 32) | file_number);
	if (lookup_it != _friend_receiving_lookup.end()) {
		return lookup_it->second;
	} else {
		return {};
	}
}

ToxTransferManager::ToxTransferManager(RegistryMessageModel& rmm, Contact3Registry& cr, ToxContactModel2& tcm, ToxI& t, ToxEventProviderI& tep) : _rmm(rmm), _cr(cr), _tcm(tcm), _t(t) {
	tep.subscribe(this, Tox_Event::TOX_EVENT_FRIEND_CONNECTION_STATUS);
	tep.subscribe(this, Tox_Event::TOX_EVENT_FILE_RECV);
	tep.subscribe(this, Tox_Event::TOX_EVENT_FILE_RECV_CONTROL);
	tep.subscribe(this, Tox_Event::TOX_EVENT_FILE_RECV_CHUNK);
	tep.subscribe(this, Tox_Event::TOX_EVENT_FILE_CHUNK_REQUEST);

	_rmm.subscribe(this, RegistryMessageModel_Event::message_construct);
	_rmm.subscribe(this, RegistryMessageModel_Event::message_updated);
	_rmm.subscribe(this, RegistryMessageModel_Event::message_destroy);

	_rmm.subscribe(this, RegistryMessageModel_Event::send_file_path);
}

ToxTransferManager::~ToxTransferManager(void) {
}

void ToxTransferManager::iterate(void) {
	// TODO: time out transfers
}

Message3Handle ToxTransferManager::toxSendFilePath(const Contact3 c, uint32_t file_kind, std::string_view file_name, std::string_view file_path) {
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

	auto file_impl = std::make_unique<FileRFile>(file_path);
	if (!file_impl->isGood()) {
		std::cerr << "TTM error: failed opening file '" << file_path << "'!\n";
		return {};
	}

	// get current time unix epoch utc
	uint64_t ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	// TODO: expose file id
	std::vector<uint8_t> file_id(32); assert(file_id.size() == 32);
	randombytes_buf(file_id.data(), file_id.size());

	const auto c_self = _cr.get<Contact::Components::Self>(c).self;
	if (!_cr.valid(c_self)) {
		std::cerr << "TTM error: failed to get self!\n";
		return {};
	}

	const auto e = reg_ptr->create();
	reg_ptr->emplace<Message::Components::ContactTo>(e, c);
	reg_ptr->emplace<Message::Components::ContactFrom>(e, c_self);
	reg_ptr->emplace<Message::Components::Timestamp>(e, ts); // reactive?

	reg_ptr->emplace<Message::Components::Transfer::TagHaveAll>(e);
	reg_ptr->emplace<Message::Components::Transfer::TagSending>(e);
	reg_ptr->emplace<Message::Components::Transfer::FileKind>(e, file_kind);
	reg_ptr->emplace<Message::Components::Transfer::FileID>(e, file_id);

	{ // file info
		auto& file_info = reg_ptr->emplace<Message::Components::Transfer::FileInfo>(e);
		file_info.file_list.emplace_back() = {std::string{file_name}, file_impl->_file_size};
		file_info.total_size = file_impl->_file_size;

		reg_ptr->emplace<Message::Components::Transfer::FileInfoLocal>(e, std::vector{std::string{file_path}});
	}

	reg_ptr->emplace<Message::Components::Transfer::BytesSent>(e);

	// TODO: determine if this is true
	reg_ptr->emplace<Message::Components::Transfer::TagPaused>(e);

	const auto friend_number = _cr.get<Contact::Components::ToxFriendEphemeral>(c).friend_number;
	const auto&& [transfer_id, err] = _t.toxFileSend(friend_number, file_kind, file_impl->_file_size, file_id, file_name);
	if (err == TOX_ERR_FILE_SEND_OK) {
		reg_ptr->emplace<Message::Components::Transfer::ToxTransferFriend>(e, friend_number, transfer_id.value());
		reg_ptr->emplace<Message::Components::Transfer::File>(e, std::move(file_impl));
		// TODO: add tag signifying init sent status?

		toxFriendLookupAdd({*reg_ptr, e});
	} // else queue?

	_rmm.throwEventConstruct(*reg_ptr, e);
	return {*reg_ptr, e};
}

bool ToxTransferManager::resume(Message3Handle transfer) {
	if (!static_cast<bool>(transfer)) {
		std::cerr << "TTM error: resume() transfer " << entt::to_integral(transfer.entity()) << " is not a valid transfer\n";
		return false;
	}

	// TODO: test for paused?

	if (!transfer.all_of<Message::Components::Transfer::ToxTransferFriend>()) {
		std::cerr << "TTM error: resume() transfer " << entt::to_integral(transfer.entity()) << " ent does not have toxtransfer info\n";
		return false;
	}

	const auto [friend_number, transfer_number] = transfer.get<Message::Components::Transfer::ToxTransferFriend>();

	const auto err = _t.toxFileControl(friend_number, transfer_number, TOX_FILE_CONTROL_RESUME);
	if (err != TOX_ERR_FILE_CONTROL_OK) {
		std::cerr << "TTM error: resume() transfer " << entt::to_integral(transfer.entity()) << " tox file control error " << err << "\n";
		return false;
	}

	transfer.remove<Message::Components::Transfer::TagPaused>();

	_rmm.throwEventUpdate(transfer);

	return true;
}

bool ToxTransferManager::pause(Message3Handle transfer) {
	if (!static_cast<bool>(transfer)) {
		std::cerr << "TTM error: pause() transfer " << entt::to_integral(transfer.entity()) << " is not a valid transfer\n";
		return false;
	}

	// TODO: test for paused?

	if (!transfer.all_of<Message::Components::Transfer::ToxTransferFriend>()) {
		std::cerr << "TTM error: pause() transfer " << entt::to_integral(transfer.entity()) << " ent does not have toxtransfer info\n";
		return false;
	}

	const auto [friend_number, transfer_number] = transfer.get<Message::Components::Transfer::ToxTransferFriend>();

	const auto err = _t.toxFileControl(friend_number, transfer_number, TOX_FILE_CONTROL_PAUSE);
	if (err != TOX_ERR_FILE_CONTROL_OK) {
		std::cerr << "TTM error: pause() transfer " << entt::to_integral(transfer.entity()) << " tox file control error " << err << "\n";
		return false;
	}

	transfer.emplace_or_replace<Message::Components::Transfer::TagPaused>();

	_rmm.throwEventUpdate(transfer);

	return true;
}

bool ToxTransferManager::setFileI(Message3Handle transfer, std::unique_ptr<FileI>&& new_file) {
	if (!static_cast<bool>(transfer)) {
		std::cerr << "TTM error: setFileI() transfer " << entt::to_integral(transfer.entity()) << " is not a valid transfer\n";
		return false;
	}

	if (!new_file->isGood()) {
		std::cerr << "TTM error: failed setting new_file_impl!\n";
		return false;
	}

	transfer.emplace_or_replace<Message::Components::Transfer::File>(std::move(new_file));

	_rmm.throwEventUpdate(transfer);

	return true;
}

bool ToxTransferManager::setFilePathDir(Message3Handle transfer, std::string_view file_path) {
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
	if (transfer.all_of<Message::Components::Transfer::FileInfo>()) {
		const auto& file_info = transfer.get<Message::Components::Transfer::FileInfo>();
		file_size = file_info.total_size; // hack
		// HACK: use fist enty
		assert(file_info.file_list.size() == 1);
		full_file_path += file_info.file_list.front().file_name;

	} else {
		std::cerr << "TTM warning: no FileInfo on transfer, using default\n";
		full_file_path += "file_recv.bin";
	}

	transfer.emplace<Message::Components::Transfer::FileInfoLocal>(std::vector{full_file_path});

	auto file_impl = std::make_unique<FileWFile>(full_file_path, file_size);
	if (!file_impl->isGood()) {
		std::cerr << "TTM error: failed opening file '" << file_path << "'!\n";
		return false;
	}

	transfer.emplace_or_replace<Message::Components::Transfer::File>(std::move(file_impl));

	// TODO: is this a good idea????
	_rmm.throwEventUpdate(transfer);

	return true;
}

bool ToxTransferManager::accept(Message3Handle transfer, std::string_view file_path) {
	if (!static_cast<bool>(transfer)) {
		std::cerr << "TTM error: accepted transfer " << entt::to_integral(transfer.entity()) << " is not a valid transfer\n";
		return false;
	}

	if (!transfer.all_of<Message::Components::Transfer::TagReceiving, Message::Components::Transfer::ToxTransferFriend>()) {
		std::cerr << "TTM error: accepted transfer " << entt::to_integral(transfer.entity()) << " is not a receiving transfer\n";
		return false;
	}

	if (transfer.any_of<Message::Components::Transfer::File>()) {
		std::cerr << "TTM warning: overwriting existing file_impl " << entt::to_integral(transfer.entity()) << "\n";
	}

	if (!setFilePathDir(transfer, file_path)) {
		std::cerr << "TTM error: accepted transfer " << entt::to_integral(transfer.entity()) << " failed setting path\n";
		return false;
	}

	if (!resume(transfer)) {
		std::cerr << "TTM error: accepted transfer " << entt::to_integral(transfer.entity()) << " failed to resume\n";
		return false;
	}

	std::cout << "TTM info: accepted " << entt::to_integral(transfer.entity()) << ", saving to " << file_path << "\n";

	// setFilePathDir() and resume() throw events

	return true;
}

bool ToxTransferManager::onEvent(const Message::Events::MessageConstruct&) {
	return false;
}

bool ToxTransferManager::onEvent(const Message::Events::MessageUpdated& e) {
	if (e.e.all_of<Message::Components::Transfer::ActionAccept>()) {
		accept(e.e, e.e.get<Message::Components::Transfer::ActionAccept>().save_to_path);

		// should?
		e.e.remove<Message::Components::Transfer::ActionAccept>();

		// TODO: recursion??
		// oh no, accept calls it 2x
		//_rmm.throwEventUpdate(
	}

	return false;
}

bool ToxTransferManager::onEvent(const Message::Events::MessageDestory& e) {
	if (e.e.all_of<Message::Components::Transfer::ToxTransferFriend>()) {
		toxFriendLookupRemove(e.e);
	}

	return false;
}

bool ToxTransferManager::onToxEvent(const Tox_Event_Friend_Connection_Status* e) {
	const auto friend_number = tox_event_friend_connection_status_get_friend_number(e);
	const auto connection_status = tox_event_friend_connection_status_get_connection_status(e);

	if (connection_status == TOX_CONNECTION_NONE) {
		auto c = _tcm.getContactFriend(friend_number);
		auto* reg_ptr = _rmm.get(c);
		if (reg_ptr == nullptr) {
			return false;
		}

		std::vector<Message3> to_destory;
		reg_ptr->view<Message::Components::Transfer::ToxTransferFriend>().each([&](const Message3 ent, const auto& ttfs) {
			assert(ttfs.friend_number == friend_number);
			//if (ttfs.friend_number == friend_number) {
			to_destory.push_back(ent);
			std::cerr << "TTM warning: friend disconnected, forcefully removing e:" << entt::to_integral(ent) << " frd:" << friend_number << " fnb:" << ttfs.transfer_number << "\n";
		});

		for (const auto ent : to_destory) {
			// update lookup table
			toxFriendLookupRemove({*reg_ptr, ent});

			// TODO: removing file a good idea?
			reg_ptr->remove<Message::Components::Transfer::ToxTransferFriend, Message::Components::Transfer::File>(ent);

			reg_ptr->emplace_or_replace<Message::Components::Transfer::TagPaused>(ent);

			_rmm.throwEventUpdate(*reg_ptr, ent);
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
	Message3Handle transfer {};
	reg_ptr->view<Message::Components::Transfer::TagReceiving, Message::Components::Transfer::ToxTransferFriend>().each([&](Message3 ent, const Message::Components::Transfer::ToxTransferFriend& ttf) {
		if (ttf.friend_number == friend_number && ttf.transfer_number == file_number) {
			transfer = {*reg_ptr, ent};
		}
	});
	if (static_cast<bool>(transfer)) {
		std::cerr << "TTM error: existing file transfer frd:" << friend_number << " fnb:" << file_number << "\n";
		return false;
	}
	assert(!_friend_receiving_lookup.count((uint64_t(friend_number) << 32) | file_number));

	// TODO: also check for file id

	// get current time unix epoch utc
	uint64_t ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	// create ent
	transfer = {*reg_ptr, reg_ptr->create()};

	auto self_c = _cr.get<Contact::Components::Self>(c).self;

	transfer.emplace<Message::Components::ContactTo>(self_c);
	transfer.emplace<Message::Components::ContactFrom>(c);
	transfer.emplace<Message::Components::Timestamp>(ts); // reactive?

	transfer.emplace<Message::Components::Transfer::TagReceiving>();
	transfer.emplace<Message::Components::Transfer::TagPaused>();
	transfer.emplace<Message::Components::Transfer::ToxTransferFriend>(friend_number, file_number);
	transfer.emplace<Message::Components::Transfer::FileKind>(file_kind);

	auto [f_id_opt, _] = _t.toxFileGetFileID(friend_number, file_number);
	assert(f_id_opt.has_value());
	transfer.emplace<Message::Components::Transfer::FileID>(f_id_opt.value());

	{ // file info
		auto& file_info = transfer.emplace<Message::Components::Transfer::FileInfo>();
		file_info.file_list.push_back({std::string{file_name}, file_size});
		file_info.total_size = file_size;
	}

	transfer.emplace<Message::Components::Transfer::BytesReceived>();

	toxFriendLookupAdd(transfer);

	_rmm.throwEventConstruct(transfer);

	return true;
}

bool ToxTransferManager::onToxEvent(const Tox_Event_File_Recv_Control* e) {
	const auto friend_number = tox_event_file_recv_control_get_friend_number(e);
	const auto file_number = tox_event_file_recv_control_get_file_number(e);
	const auto control = tox_event_file_recv_control_get_control(e);

	// first try sending
	Message3Handle transfer = toxFriendLookupSending(friend_number, file_number);
	if (!static_cast<bool>(transfer)) {
		// then receiving
		transfer = toxFriendLookupReceiving(friend_number, file_number);
	}

	if (!static_cast<bool>(transfer)) {
		std::cerr << "TMM waring: control for unk ft\n";
		return false; // shrug, we don't know about it, might be someone else's
	}

	if (control == TOX_FILE_CONTROL_CANCEL) {
		std::cerr << "TTM: friend transfer canceled frd:" << friend_number << " fnb:" << file_number << "\n";

		// update lookup table
		toxFriendLookupRemove(transfer);

		transfer.remove<
			Message::Components::Transfer::ToxTransferFriend,
			// TODO: removing file a good idea?
			Message::Components::Transfer::File
		>();

		_rmm.throwEventUpdate(transfer);
	} else if (control == TOX_FILE_CONTROL_PAUSE) {
		std::cerr << "TTM: friend transfer paused frd:" << friend_number << " fnb:" << file_number << "\n";
		// TODO: add distinction between local and remote pause
		transfer.emplace_or_replace<Message::Components::Transfer::TagPaused>();
		_rmm.throwEventUpdate(transfer);
	} else if (control == TOX_FILE_CONTROL_RESUME) {
		std::cerr << "TTM: friend transfer resumed frd:" << friend_number << " fnb:" << file_number << "\n";
		transfer.remove<Message::Components::Transfer::TagPaused>();
		_rmm.throwEventUpdate(transfer);
	}

	return true;
}

bool ToxTransferManager::onToxEvent(const Tox_Event_File_Recv_Chunk* e) {
	const auto friend_number = tox_event_file_recv_chunk_get_friend_number(e);
	const auto file_number = tox_event_file_recv_chunk_get_file_number(e);
	const uint8_t* data = tox_event_file_recv_chunk_get_data(e);
	const auto data_size = tox_event_file_recv_chunk_get_length(e);
	const auto position = tox_event_file_recv_chunk_get_position(e);

	Message3Handle transfer = toxFriendLookupReceiving(friend_number, file_number);
	if (!static_cast<bool>(transfer)) {
		return false; // shrug, we don't know about it, might be someone else's
	}

	if (data_size == 0) {
		std::cout << "TTM finished friend " << friend_number << " transfer " << file_number << ", closing\n";

		// update lookup table
		toxFriendLookupRemove(transfer);

		transfer.remove<
			Message::Components::Transfer::ToxTransferFriend,
			// TODO: removing file a good idea?
			Message::Components::Transfer::File
		>();

		transfer.emplace<Message::Components::Transfer::TagHaveAll>();

		_rmm.throwEventUpdate(transfer);
	} else if (!transfer.all_of<Message::Components::Transfer::File>() || !transfer.get<Message::Components::Transfer::File>()->isGood()) {
		std::cerr << "TTM error: file not good f" << friend_number << " t" << file_number << ", closing\n";
		_t.toxFileControl(friend_number, file_number, Tox_File_Control::TOX_FILE_CONTROL_CANCEL);

		// update lookup table
		toxFriendLookupRemove(transfer);

		transfer.remove<
			Message::Components::Transfer::ToxTransferFriend,
			// TODO: removing file a good idea?
			Message::Components::Transfer::File
		>();

		_rmm.throwEventUpdate(transfer);
	} else {
		auto* file = transfer.get<Message::Components::Transfer::File>().get();
		const auto res = file->write(position, std::vector<uint8_t>{data, data+data_size});
		transfer.get<Message::Components::Transfer::BytesReceived>().total += data_size;

		// queue?
		_rmm.throwEventUpdate(transfer);
	}

	return true;
}

bool ToxTransferManager::onToxEvent(const Tox_Event_File_Chunk_Request* e) {
	const auto friend_number = tox_event_file_chunk_request_get_friend_number(e);
	const auto file_number = tox_event_file_chunk_request_get_file_number(e);
	const auto position = tox_event_file_chunk_request_get_position(e);
	const auto data_size = tox_event_file_chunk_request_get_length(e);

	Message3Handle transfer = toxFriendLookupSending(friend_number, file_number);
	if (!static_cast<bool>(transfer)) {
		std::cerr << "TTM warning: chunk request for unk ft\n";
		return false; // shrug, we don't know about it, might be someone else's
	}

	// tox wants us to end the transmission
	if (data_size == 0) {
		std::cout << "TTM finished friend " << friend_number << " transfer " << file_number << ", closing\n";

		// update lookup table
		toxFriendLookupRemove(transfer);

		transfer.remove<
			Message::Components::Transfer::ToxTransferFriend,
			// TODO: removing file a good idea?
			Message::Components::Transfer::File
		>();

		// TODO: add tag finished?
		_rmm.throwEventUpdate(transfer);
	} else if (!transfer.all_of<Message::Components::Transfer::File>() || !transfer.get<Message::Components::Transfer::File>()->isGood()) {
		std::cerr << "TTM error: file not good f" << friend_number << " t" << file_number << ", closing\n";
		_t.toxFileControl(friend_number, file_number, Tox_File_Control::TOX_FILE_CONTROL_CANCEL);

		// update lookup table
		toxFriendLookupRemove(transfer);

		transfer.remove<
			Message::Components::Transfer::ToxTransferFriend,
			// TODO: removing file a good idea?
			Message::Components::Transfer::File
		>();

		_rmm.throwEventUpdate(transfer);
	} else {
		auto* file = transfer.get<Message::Components::Transfer::File>().get();
		const auto data = file->read(position, data_size);

		const auto err = _t.toxFileSendChunk(friend_number, file_number, position, data);
		// TODO: investigate if i need to retry if sendq full
		if (err == TOX_ERR_FILE_SEND_CHUNK_OK) {
			transfer.get<Message::Components::Transfer::BytesSent>().total += data.size();
			_rmm.throwEventUpdate(transfer);
		}
	}

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

	toxSendFilePath(c, 0, file_name, file_path);

	return false;
}
