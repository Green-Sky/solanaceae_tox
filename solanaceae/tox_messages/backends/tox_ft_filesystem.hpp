#pragma once

#include <solanaceae/object_store/object_store.hpp>

namespace Backends {

struct ToxFTFilesystem : public StorageBackendI {
	ToxFTFilesystem(
		ObjectStore2& os
	);
	~ToxFTFilesystem(void);

	ObjectHandle newObject(ByteSpan id) override;

	std::unique_ptr<File2I> file2(Object o, FILE2_FLAGS flags) override;
};

} // Backends

