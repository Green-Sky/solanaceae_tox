#pragma once

#include <solanaceae/toxcore/tox_event_interface.hpp>
#include <solanaceae/contact/contact_model4.hpp>

#include <solanaceae/toxcore/tox_key.hpp>

// fwd
struct ToxI;

// tox contact model for ContactModel4I
class ToxContactModel2 : public ContactModel4I, public ToxEventI {
	ContactStore4I& _cs;
	ToxI& _t;
	ToxEventProviderI::SubscriptionReference _tep_sr;

	Contact4 _root;
	Contact4 _friend_self;

	float _group_status_timer {0.f};

	public:
		static constexpr const char* version {"4"};

		ToxContactModel2(ContactStore4I& cs, ToxI& t, ToxEventProviderI& tep);
		virtual ~ToxContactModel2(void);

		// TODO: continually fetch group peer connection state, since JF does not want to add cb/event
		void iterate(float delta);

	protected: // mmi
		bool addContact(Contact4 c) override;

		// accept incoming request
		bool acceptRequest(Contact4 c, std::string_view self_name, std::string_view password) override;

		bool leave(Contact4 c, std::string_view reason) override;

	public: // util for tox code
		// also creates if non existant
		ContactHandle4 getContactFriend(uint32_t friend_number);

		ContactHandle4 getContactGroup(uint32_t group_number);
		ContactHandle4 getContactGroupPeer(uint32_t group_number, uint32_t peer_number);
		//ContactHandle4 getContactGroupPeer(const ToxKey& group_key, const ToxKey& peer_key);
		ContactHandle4 getContactGroupPeer(uint32_t group_number, const ToxKey& peer_key);

		// TODO: add proper perm api to contacts
		bool groupPeerCanSpeak(uint32_t group_number, uint32_t peer_number);

	protected: // tox events
		bool onToxEvent(const Tox_Event_Friend_Connection_Status* e) override;
		bool onToxEvent(const Tox_Event_Friend_Status* e) override;
		bool onToxEvent(const Tox_Event_Friend_Name* e) override;
		bool onToxEvent(const Tox_Event_Friend_Status_Message* e) override;
		bool onToxEvent(const Tox_Event_Friend_Request* e) override;

		bool onToxEvent(const Tox_Event_Group_Invite* e) override;
		bool onToxEvent(const Tox_Event_Group_Self_Join* e) override;
		bool onToxEvent(const Tox_Event_Group_Peer_Join* e) override;
		bool onToxEvent(const Tox_Event_Group_Peer_Exit* e) override;
		bool onToxEvent(const Tox_Event_Group_Peer_Name* e) override;
		bool onToxEvent(const Tox_Event_Group_Topic* e) override;
};

