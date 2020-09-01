// Copyright Epic Games, Inc. All Rights Reserved.

#include "trio/Concepts.h"

namespace trio {

Readable::~Readable() = default;
Writable::~Writable() = default;
Seekable::~Seekable() = default;
Openable::~Openable() = default;
Closeable::~Closeable() = default;
Controllable::~Controllable() = default;

}  // namespace trio
