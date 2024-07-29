cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

project(solanaceae)

add_library(solanaceae_tox_contacts
	./solanaceae/tox_contacts/components.hpp
	./solanaceae/tox_contacts/components_id.inl

	./solanaceae/tox_contacts/tox_contact_model2.hpp
	./solanaceae/tox_contacts/tox_contact_model2.cpp
)

target_include_directories(solanaceae_tox_contacts PUBLIC .)
target_compile_features(solanaceae_tox_contacts PUBLIC cxx_std_17)
target_link_libraries(solanaceae_tox_contacts PUBLIC
	solanaceae_util
	solanaceae_contact
	solanaceae_toxcore

	solanaceae_message3 # for messageissame
)

add_library(solanaceae_tox_messages
	./solanaceae/tox_messages/msg_components.hpp
	./solanaceae/tox_messages/msg_components_id.inl

	./solanaceae/tox_messages/tox_message_manager.hpp
	./solanaceae/tox_messages/tox_message_manager.cpp

	# TODO: seperate tf?

	./solanaceae/tox_messages/obj_components.hpp
	./solanaceae/tox_messages/obj_components_id.inl

	./solanaceae/tox_messages/backends/tox_ft_filesystem.hpp
	./solanaceae/tox_messages/backends/tox_ft_filesystem.cpp

	./solanaceae/tox_messages/tox_transfer_manager.hpp
	./solanaceae/tox_messages/tox_transfer_manager.cpp
)

target_include_directories(solanaceae_tox_messages PUBLIC .)
target_compile_features(solanaceae_tox_messages PUBLIC cxx_std_17)
target_link_libraries(solanaceae_tox_messages PUBLIC
	solanaceae_tox_contacts
	solanaceae_message3
	solanaceae_file2
	solanaceae_object_store
)

