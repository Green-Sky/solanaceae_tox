#pragma once

#include <solanaceae/object_store/object_store.hpp>

namespace Backends {

struct ToxFTFilesystem : public StorageBackendIMeta, public StorageBackendIFile2 {
	ObjectStore2& _os;

	ToxFTFilesystem(
		ObjectStore2& os
	);
	~ToxFTFilesystem(void);

	ObjectHandle newObject(ByteSpan id, bool throw_construct = true) override;

	std::unique_ptr<File2I> file2(Object o, FILE2_FLAGS flags) override;
};

} // Backends

