#include "./tox_ft_filesystem.hpp"

#include <solanaceae/object_store/meta_components.hpp>
#include <solanaceae/object_store/meta_components_file.hpp>

#include <solanaceae/file/file2_std.hpp>

#include <iostream>

namespace Backends {

ToxFTFilesystem::ToxFTFilesystem(
	ObjectStore2& os
) : _os(os) {
}

ToxFTFilesystem::~ToxFTFilesystem(void) {
}

ObjectHandle ToxFTFilesystem::newObject(ByteSpan id, bool throw_construct) {
	ObjectHandle o{_os.registry(), _os.registry().create()};

	o.emplace<ObjComp::Ephemeral::BackendMeta>(this);
	o.emplace<ObjComp::Ephemeral::BackendFile2>(this);
	o.emplace<ObjComp::ID>(std::vector<uint8_t>{id});
	//o.emplace<ObjComp::Ephemeral::FilePath>(object_file_path.generic_u8string());

	if (throw_construct) {
		_os.throwEventConstruct(o);
	}

	return o;
}

std::unique_ptr<File2I> ToxFTFilesystem::file2(Object ov, FILE2_FLAGS flags) {
	if (flags & FILE2_RAW) {
		std::cerr << "TFTF error: does not support raw modes\n";
		return nullptr;
	}

	if (flags == FILE2_NONE) {
		std::cerr << "TFTF error: no file mode set\n";
		assert(false);
		return nullptr;
	}

	if (flags & FILE2_WRITE) {
		std::cerr << "TFTF error: opening file in write mode not supported\n";
	}

	ObjectHandle o{_os.registry(), ov};

	if (!static_cast<bool>(o)) {
		return nullptr;
	}

	// will this do if we go and support enc?
	// use ObjComp::Ephemeral::FilePath instead??
	if (!o.all_of<ObjComp::F::SingleInfoLocal>()) {
		return nullptr;
	}

	const auto& file_path = o.get<ObjComp::F::SingleInfoLocal>().file_path;
	if (file_path.empty()) {
		return nullptr;
	}

	// read only
	auto res = std::make_unique<File2RFile>(file_path);

	if (!res || !res->isGood()) {
		std::cerr << "TFTF error: failed constructing file '" << file_path << "'\n";
		return nullptr;
	}

	return res;
}

} // Backends

