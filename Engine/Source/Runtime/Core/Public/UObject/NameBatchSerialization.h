// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "UObject/NameTypes.h"
#include "Templates/Function.h"

#define ALLOW_NAME_BATCH_SAVING PLATFORM_LITTLE_ENDIAN && !PLATFORM_TCHAR_IS_4_BYTES

//////////////////////////////////////////////////////////////////////////

/** 
 * Serialization util that optimizes WITH_CASE_PRESERVING_NAME-loading by reducing comparison id lookups
 * 
 * Stores 32-bit display entry id with an unused bit to indicate if FName::GetComparisonIdFromDisplayId lookup is needed.
 * 
 * Note that only display entries should be saved to make output deterministic.
 */
class FDisplayNameEntryId
{
public:
	explicit FDisplayNameEntryId(FName Name) : FDisplayNameEntryId(Name.GetDisplayIndex(), Name.GetComparisonIndex()) {}
	FORCEINLINE FName ToName(uint32 Number) const { return FName(GetComparisonId(), GetDisplayId(), Number); }

private:
#if WITH_CASE_PRESERVING_NAME
	static constexpr uint32 DifferentIdsFlag = 1u << 31;
	static constexpr uint32 DisplayIdMask = ~DifferentIdsFlag;

	uint32 Value = 0;

	FDisplayNameEntryId(FNameEntryId Id, FNameEntryId CmpId) : Value(Id.ToUnstableInt() | (Id != CmpId) * DifferentIdsFlag) {}
	FORCEINLINE bool SameIds() const { return (Value & DifferentIdsFlag) == 0; }
	FORCEINLINE FNameEntryId GetDisplayId() const	 { return FNameEntryId::FromUnstableInt(Value & DisplayIdMask); }
	FORCEINLINE FNameEntryId GetComparisonId() const { return SameIds() ? GetDisplayId() : FName::GetComparisonIdFromDisplayId(GetDisplayId()); }
	friend bool operator==(FDisplayNameEntryId A, FDisplayNameEntryId B) { return A.Value == B.Value; }
#else
	FNameEntryId Id;

	FDisplayNameEntryId(FNameEntryId InId, FNameEntryId) : Id(InId) {}
	FORCEINLINE FNameEntryId GetDisplayId() const	 { return Id; }
	FORCEINLINE FNameEntryId GetComparisonId() const { return Id; }
	friend bool operator==(FDisplayNameEntryId A, FDisplayNameEntryId B) { return A.Id == B.Id; }
#endif
	friend bool operator==(FNameEntryId A, FDisplayNameEntryId B) { return A == B.GetDisplayId(); }
	friend bool operator==(FDisplayNameEntryId A, FNameEntryId B) { return A.GetDisplayId() == B; }
	friend uint32 GetTypeHash(FDisplayNameEntryId InId) { return GetTypeHash(InId.GetDisplayId()); }

public: // Internal functions for batch serialization code - intentionally lacking CORE_API
	static FDisplayNameEntryId FromComparisonId(FNameEntryId ComparisonId);
	FNameEntryId ToDisplayId() const;
	void SetLoadedComparisonId(FNameEntryId ComparisonId); // Called first
#if WITH_CASE_PRESERVING_NAME
	void SetLoadedDifferentDisplayId(FNameEntryId DisplayId); // Called second if display id differs
	FNameEntryId GetLoadedComparisonId() const; // Get the already loaded comparison id
#endif
};

//////////////////////////////////////////////////////////////////////////

#if ALLOW_NAME_BATCH_SAVING
// Save display entries in given order to a name blob and a versioned hash blob.
CORE_API void SaveNameBatch(TArrayView<const FDisplayNameEntryId> Names, TArray<uint8>& OutNameData, TArray<uint8>& OutHashData);

// Save display entries in given order to an archive
CORE_API void SaveNameBatch(TArrayView<const FDisplayNameEntryId> Names, FArchive& Out);
#endif

//////////////////////////////////////////////////////////////////////////

// Reserve memory in preparation for batch loading
//
// @param Bytes for existing and new names.
CORE_API void ReserveNameBatch(uint32 NameDataBytes, uint32 HashDataBytes);

// Load a name blob with precalculated hashes.
//
// Names are rehashed if hash algorithm version doesn't match.
//
// @param NameData, HashData must be 8-byte aligned.
CORE_API void LoadNameBatch(TArray<FDisplayNameEntryId>& OutNames, TArrayView<const uint8> NameData, TArrayView<const uint8> HashData);

// Load names and precalculated hashes from an archive
//
// Names are rehashed if hash algorithm version doesn't match.
CORE_API TArray<FDisplayNameEntryId> LoadNameBatch(FArchive& Ar);

// Load names and precalculated hashes from an archive using multiple workers
//
// May load synchronously in some cases, like small batches.
//
// Names are rehashed if hash algorithm version doesn't match.
//
// @param Ar is drained synchronously
// @param MaxWorkers > 0
// @return function that waits before returning result, like a simple future.
CORE_API TFunction<TArray<FDisplayNameEntryId>()> LoadNameBatchAsync(FArchive& Ar, uint32 MaxWorkers);

//////////////////////////////////////////////////////////////////////////