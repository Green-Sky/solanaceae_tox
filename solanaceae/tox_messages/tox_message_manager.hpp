#pragma once

#include <solanaceae/toxcore/tox_event_interface.hpp>
#include <solanaceae/message3/registry_message_model.hpp>
#include <solanaceae/tox_contacts/tox_contact_model2.hpp>

// fwd
struct ToxI;

class ToxMessageManager : public RegistryMessageModelEventI, public ToxEventI {
	protected:
		RegistryMessageModel& _rmm;
		Contact3Registry& _cr;
		ToxContactModel2& _tcm;
		ToxI& _t;

	public:
		ToxMessageManager(RegistryMessageModel& rmm, Contact3Registry& cr, ToxContactModel2& tcm, ToxI& t, ToxEventProviderI& tep);
		virtual ~ToxMessageManager(void);

	public: // mm3
		bool sendText(const Contact3 c, std::string_view message, bool action = false) override;

	protected: // tox events
		bool onToxEvent(const Tox_Event_Friend_Message* e) override;

		bool onToxEvent(const Tox_Event_Group_Message* e) override;
		bool onToxEvent(const Tox_Event_Group_Private_Message* e) override;
};

