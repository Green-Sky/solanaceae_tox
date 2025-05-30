#pragma once

#include <solanaceae/toxcore/tox_event_interface.hpp>
#include <solanaceae/message3/registry_message_model.hpp>
#include <solanaceae/tox_contacts/tox_contact_model2.hpp>

// fwd
struct ToxI;

class ToxMessageManager : public RegistryMessageModelEventI, public ToxEventI {
	protected:
		RegistryMessageModelI& _rmm;
		RegistryMessageModelI::SubscriptionReference _rmm_sr;
		ContactStore4I& _cs;
		ToxContactModel2& _tcm;
		ToxI& _t;
		ToxEventProviderI::SubscriptionReference _tep_sr;

	public:
		ToxMessageManager(RegistryMessageModelI& rmm, ContactStore4I& cs, ToxContactModel2& tcm, ToxI& t, ToxEventProviderI& tep);
		virtual ~ToxMessageManager(void);

	public: // mm3
		bool sendText(const Contact4 c, std::string_view message, bool action = false) override;

	protected: // tox events
		// TODO: add friend request message handling
		bool onToxEvent(const Tox_Event_Friend_Message* e) override;
		bool onToxEvent(const Tox_Event_Friend_Read_Receipt* e) override;

		bool onToxEvent(const Tox_Event_Group_Message* e) override;
		bool onToxEvent(const Tox_Event_Group_Private_Message* e) override;
};

