// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetDataTagMapSerializationDetails.h"
#include "Serialization/LargeMemoryWriter.h"
#include "UObject/NameBatchSerialization.h"

struct FAssetData;
struct FAssetRegistrySerializationOptions;

/** @see FAssetRegistryWriter */
class FAssetRegistryReader : public FArchiveProxy
{
public:
	/// @param NumWorkers > 0 for parallel loading
	FAssetRegistryReader(FArchive& Inner, int32 NumWorkers = 0);
	~FAssetRegistryReader();

	virtual FArchive& operator<<(FName& Value) override;

	void SerializeTagsAndBundles(FAssetData& Out);

	void WaitForTasks();

private:
	TArray<FNameEntryId> Names;
	TRefCountPtr<const FixedTagPrivate::FStore> Tags;
	TFuture<void> Task;

	friend FAssetDataTagMapSharedView LoadTags(FAssetRegistryReader& Reader);
};

#if ALLOW_NAME_BATCH_SAVING

class FAssetRegistryWriterBase
{
protected:
	FLargeMemoryWriter MemWriter;
};

struct FAssetRegistryWriterOptions
{
	FAssetRegistryWriterOptions() = default;
	explicit FAssetRegistryWriterOptions(const FAssetRegistrySerializationOptions& Options);

	FixedTagPrivate::FOptions Tags;
};

/**
 * Indexes FName and tag maps and serializes out deduplicated indices instead.
 *
 * Unlike previous FNameTableArchiveWriter:
 * - Name data stored as name batches, which is faster
 * - Name batch is written as a header instead of footer for faster seek-free loading
 * - Numberless FNames are serialized as a single 32-bit int
 * - Deduplicates all tag values, not just names
 *
 * Use in conjunction with FNameBatchReader.
 *
 * Data is written to inner archive in destructor to achieve seek-free loading.
 */
class FAssetRegistryWriter : public FAssetRegistryWriterBase, public FArchiveProxy
{
public:
	FAssetRegistryWriter(const FAssetRegistryWriterOptions& Options, FArchive& Out);
	~FAssetRegistryWriter();

	virtual FArchive& operator<<(FName& Value) override;

	void SerializeTagsAndBundles(const FAssetData& In);

private:
	TMap<FNameEntryId, uint32> Names;
	FixedTagPrivate::FStoreBuilder Tags;
	FArchive& TargetAr;

	friend void SaveTags(FAssetRegistryWriter& Writer, const FAssetDataTagMapSharedView& Map);
};

#endif
