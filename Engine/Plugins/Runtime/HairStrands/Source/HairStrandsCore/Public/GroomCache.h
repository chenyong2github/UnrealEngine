// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GroomCacheData.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "Serialization/BulkData.h"
#include "GroomCache.generated.h"

struct FGroomCacheChunk;

/**
 * Implements an asset that is used to store an animated groom
 */
UCLASS(BlueprintType)
class HAIRSTRANDSCORE_API UGroomCache : public UObject, public IInterface_AssetUserData
{
	GENERATED_BODY()

public:

	//~ Begin UObject Interface.
	virtual void Serialize(FArchive& Ar) override;
	//~ End UObject Interface.

	void Initialize(EGroomCacheType Type);
	int32 GetStartFrame() const;
	int32 GetEndFrame() const;
	float GetDuration() const;

	/** Get the frame number at the specified time within the animation range which might not start at 0 */
	int32  GetFrameNumberAtTime(const float Time) const;

	/** Get the frame index at the specified time with the index 0 being the start of the animation */
	int32  GetFrameIndexAtTime(const float Time) const;

	bool GetGroomDataAtTime(float Time, FGroomCacheAnimationData& AnimData);
	bool GetGroomDataAtFrameIndex(int32 FrameIndex, FGroomCacheAnimationData& AnimData);

	void SetGroomAnimationInfo(const FGroomAnimationInfo& AnimInfo);

	EGroomCacheType GetType() const;

	TArray<FGroomCacheChunk>& GetChunks() { return Chunks; }

#if WITH_EDITORONLY_DATA
	/** Import options used for this GroomCache */
	UPROPERTY(Category = ImportSettings, VisibleAnywhere, Instanced)
	class UAssetImportData* AssetImportData;	
#endif

protected:
	UPROPERTY(VisibleAnywhere, Category = GroomCache)
	FGroomCacheInfo GroomCacheInfo;

	TArray<FGroomCacheChunk> Chunks;

	friend class FGroomCacheProcessor;
};

/**
 * The smallest unit of streamed GroomCache data
 * The BulkData member is loaded on-demand so that loading the GroomCache itself is relatively lightweight
 */
struct FGroomCacheChunk
{
	/** Size of the chunk of data in bytes */
	int32 DataSize = 0;

	/** Frame index of the frame stored in this block */
	int32 FrameIndex = 0;

	/** Bulk data if stored in the package. */
	FByteBulkData BulkData;

	void Serialize(FArchive& Ar, UObject* Owner, int32 ChunkIndex);
};

/** Proxy that processes the HairGroupData into GroomCacheChunks that contain the groom animation data */
class HAIRSTRANDSCORE_API FGroomCacheProcessor
{
public:
	FGroomCacheProcessor(EGroomCacheType InType, EGroomCacheAttributes InAttributes);

	void AddGroomSample(TArray<FHairGroupData>&& GroomData);
	void TransferChunks(UGroomCache* GroomCache);
	EGroomCacheType GetType() const { return Type; }

private:
	TArray<FGroomCacheChunk> Chunks;
	EGroomCacheAttributes Attributes;
	EGroomCacheType Type;
};
