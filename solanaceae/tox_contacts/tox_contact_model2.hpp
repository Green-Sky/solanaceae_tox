#pragma once

#include <solanaceae/toxcore/tox_event_interface.hpp>
#include <solanaceae/contact/contact_model3.hpp>

#include <solanaceae/toxcore/tox_key.hpp>

// fwd
struct ToxI;

// tox contact model for ContactModel3I
class ToxContactModel2 : public ContactModel3I, public ToxEventI {
	Contact3Registry& _cr;
	ToxI& _t;
	ToxEventProviderI::SubscriptionReference _tep_sr;

	Contact3 _root;
	Contact3 _friend_self;

	float _group_status_timer {0.f};

	public:
		static constexpr const char* version {"3"};

		ToxContactModel2(Contact3Registry& cr, ToxI& t, ToxEventProviderI& tep);
		virtual ~ToxContactModel2(void);

		// TODO: continually fetch group peer connection state, since JF does not want to add cb/event
		void iterate(float delta);

	protected: // mmi
		// accept incoming request
		void acceptRequest(Contact3 c, std::string_view self_name, std::string_view password) override;

	public: // util for tox code
		// also creates if non existant
		Contact3Handle getContactFriend(uint32_t friend_number);

		Contact3Handle getContactGroup(uint32_t group_number);
		Contact3Handle getContactGroupPeer(uint32_t group_number, uint32_t peer_number);
		//Contact3Handle getContactGroupPeer(const ToxKey& group_key, const ToxKey& peer_key);
		Contact3Handle getContactGroupPeer(uint32_t group_number, const ToxKey& peer_key);

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

