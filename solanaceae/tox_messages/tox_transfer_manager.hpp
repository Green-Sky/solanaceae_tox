#pragma once

#include <solanaceae/toxcore/tox_event_interface.hpp>
#include <solanaceae/object_store/object_store.hpp>
#include <solanaceae/message3/registry_message_model.hpp>
#include <solanaceae/tox_contacts/tox_contact_model2.hpp>

#include "./backends/tox_ft_filesystem.hpp"

//#include <solanaceae/file/file2.hpp>
// fwd
struct File2I;

#include <entt/container/dense_map.hpp>

#include <string_view>
#include <memory>

// fwd
struct ToxI;

class ToxTransferManager : public RegistryMessageModelEventI, public ObjectStoreEventI, public ToxEventI {
	public:
		static constexpr const char* version {"4"};

	protected:
		RegistryMessageModelI& _rmm;
		RegistryMessageModelI::SubscriptionReference _rmm_sr;
		ContactStore4I& _cs;
		ToxContactModel2& _tcm;
		ToxI& _t;
		ToxEventProviderI::SubscriptionReference _tep_sr;
		ObjectStore2& _os;
		ObjectStore2::SubscriptionReference _os_sr;

		// TODO: remove
		Backends::ToxFTFilesystem _ftb;

		bool _in_obj_update_event {false};

		entt::dense_map<uint64_t, ObjectHandle> _friend_sending_lookup;
		entt::dense_map<uint64_t, ObjectHandle> _friend_receiving_lookup;

	protected:
		void toxFriendLookupAdd(ObjectHandle o);
		void toxFriendLookupRemove(ObjectHandle o);

		ObjectHandle toxFriendLookupSending(const uint32_t friend_number, const uint32_t file_number) const;
		ObjectHandle toxFriendLookupReceiving(const uint32_t friend_number, const uint32_t file_number) const;

		File2I* objGetFile2Write(ObjectHandle o);
		File2I* objGetFile2Read(ObjectHandle o);

	public:
		ToxTransferManager(
			RegistryMessageModelI& rmm,
			ContactStore4I& cs,
			ToxContactModel2& tcm,
			ToxI& t,
			ToxEventProviderI& tep,
			ObjectStore2& os
		);
		virtual ~ToxTransferManager(void);

		virtual void iterate(void);

	public: // TODO: private?
		Message3Handle toxSendFilePath(const Contact4 c, uint32_t file_kind, std::string_view file_name, std::string_view file_path, std::vector<uint8_t> file_id = {});

		bool resume(ObjectHandle transfer);
		bool pause(ObjectHandle transfer);
		// move to "file" backend?
		bool setFileI(ObjectHandle transfer, std::unique_ptr<File2I>&& new_file); // note, does not emplace FileInfoLocal
		bool setFilePath(ObjectHandle transfer, std::string_view file_path);
		bool setFilePathDir(ObjectHandle transfer, std::string_view file_path);

		// calls setFileI() and resume()
		bool accept(ObjectHandle transfer, std::string_view file_path, bool path_is_file);
		bool acceptObj(ObjectHandle transfer);

	protected: // (r)mm
		bool sendFilePath(const Contact4 c, std::string_view file_name, std::string_view file_path) override;

		// needs to have:
		// - StorageBackendIFile2
		// - SingleInfo
		// - LocalHaveAll (TODO: figure out streaming?)
		// - Tox::FileID (defaults to obj id? rng?)
		// - Tox::FileKind (defaults to 0(DATA) )
		bool sendFileObj(const Contact4 c, ObjectHandle o) override;

	protected: // os
		//bool onEvent(const ObjectStore::Events::ObjectConstruct&) override;
		bool onEvent(const ObjectStore::Events::ObjectUpdate&) override;
		bool onEvent(const ObjectStore::Events::ObjectDestory&) override;

	protected: // events
		virtual bool onToxEvent(const Tox_Event_Friend_Connection_Status* e) override;
		virtual bool onToxEvent(const Tox_Event_File_Recv* e) override;
		virtual bool onToxEvent(const Tox_Event_File_Recv_Control* e) override;
		virtual bool onToxEvent(const Tox_Event_File_Recv_Chunk* e) override;
		virtual bool onToxEvent(const Tox_Event_File_Chunk_Request* e) override;
};

