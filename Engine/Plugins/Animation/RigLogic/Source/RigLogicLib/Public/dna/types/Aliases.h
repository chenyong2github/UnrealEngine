// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "dna/types/ArrayView.h"
#include "dna/types/StringView.h"

#include <pma/MemoryResource.h>
#include <pma/ScopedPtr.h>
#include <status/Status.h>
#include <status/StatusCode.h>
#include <trio/Stream.h>
#include <trio/streams/FileStream.h>

namespace dna {

template<typename T>
using ArrayView = trust::ArrayView<T>;

template<typename T>
using ConstArrayView = trust::ConstArrayView<T>;

using trio::BoundedIOStream;
using trio::FileStream;
using sc::Status;

using namespace pma;

}  // namespace dna
