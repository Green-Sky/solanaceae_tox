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
)

