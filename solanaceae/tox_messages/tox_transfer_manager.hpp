#pragma once

#include <solanaceae/toxcore/tox_event_interface.hpp>
#include <solanaceae/message3/registry_message_model.hpp>
#include <solanaceae/tox_contacts/tox_contact_model2.hpp>

#include <solanaceae/file/file2.hpp>

#include <entt/container/dense_map.hpp>

#include <string_view>
#include <memory>

// fwd
struct ToxI;

class ToxTransferManager : public RegistryMessageModelEventI, public ToxEventI {
	protected:
		RegistryMessageModel& _rmm;
		Contact3Registry& _cr;
		ToxContactModel2& _tcm;
		ToxI& _t;

		entt::dense_map<uint64_t, Message3Handle> _friend_sending_lookup;
		entt::dense_map<uint64_t, Message3Handle> _friend_receiving_lookup;

	protected:
		void toxFriendLookupAdd(Message3Handle h);
		void toxFriendLookupRemove(Message3Handle h);

		Message3Handle toxFriendLookupSending(const uint32_t friend_number, const uint32_t file_number) const;
		Message3Handle toxFriendLookupReceiving(const uint32_t friend_number, const uint32_t file_number) const;

	public:
		ToxTransferManager(RegistryMessageModel& rmm, Contact3Registry& cr, ToxContactModel2& tcm, ToxI& t, ToxEventProviderI& tep);
		virtual ~ToxTransferManager(void);

		virtual void iterate(void);

	public: // TODO: private?
		Message3Handle toxSendFilePath(const Contact3 c, uint32_t file_kind, std::string_view file_name, std::string_view file_path);

		bool resume(Message3Handle transfer);
		bool pause(Message3Handle transfer);
		bool setFileI(Message3Handle transfer, std::unique_ptr<File2I>&& new_file); // note, does not emplace FileInfoLocal
		bool setFilePath(Message3Handle transfer, std::string_view file_path);
		bool setFilePathDir(Message3Handle transfer, std::string_view file_path);

		// calls setFileI() and resume()
		bool accept(Message3Handle transfer, std::string_view file_path, bool is_file_path);

	protected:
		bool onEvent(const Message::Events::MessageConstruct&) override;
		bool onEvent(const Message::Events::MessageUpdated&) override;
		bool onEvent(const Message::Events::MessageDestory&) override;

	protected: // events
		virtual bool onToxEvent(const Tox_Event_Friend_Connection_Status* e) override;
		virtual bool onToxEvent(const Tox_Event_File_Recv* e) override;
		virtual bool onToxEvent(const Tox_Event_File_Recv_Control* e) override;
		virtual bool onToxEvent(const Tox_Event_File_Recv_Chunk* e) override;
		virtual bool onToxEvent(const Tox_Event_File_Chunk_Request* e) override;

		bool sendFilePath(const Contact3 c, std::string_view file_name, std::string_view file_path) override;
};

