// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "UObject/NameTypes.h"

#define ALLOW_NAME_BATCH_SAVING (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT) && PLATFORM_LITTLE_ENDIAN

#if ALLOW_NAME_BATCH_SAVING
// Save comparison entries in given order to a name blob and a versioned hash blob.
CORE_API void SaveNameBatch(TArrayView<const FNameEntryId> Names, TArray<uint8>& OutNameData, TArray<uint8>& OutHashData);
#endif

// Reserve memory in preparation for batch loading
//
// @param Bytes for existing and new names.
CORE_API void ReserveNameBatch(uint32 NameDataBytes, uint32 HashDataBytes);

// Load a name blob with precalculated hashes.
//
// Names are rehased if hash algorithm version doesn't match.
//
// @param NameData, HashData must be 8-byte aligned.
CORE_API void LoadNameBatch(TArray<FNameEntryId>& OutNames, TArrayView<const uint8> NameData, TArrayView<const uint8> HashData);