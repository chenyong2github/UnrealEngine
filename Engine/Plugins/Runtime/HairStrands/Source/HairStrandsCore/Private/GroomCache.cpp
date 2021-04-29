// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomCache.h"
#include "GroomAsset.h"

void UGroomCache::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	int32 NumChunks = Chunks.Num();
	Ar << NumChunks;

	if (Ar.IsLoading())
	{
		Chunks.SetNum(NumChunks);
	}

	for (int32 ChunkId = 0; ChunkId < NumChunks; ++ChunkId)
	{
		Chunks[ChunkId].Serialize(Ar, this, ChunkId);
	}
}

void UGroomCache::Initialize(EGroomCacheType Type)
{
	GroomCacheInfo.Type = Type;
}

int32 UGroomCache::GetStartFrame() const
{
	return GroomCacheInfo.AnimationInfo.StartFrame;
}

int32 UGroomCache::GetEndFrame() const
{
	return GroomCacheInfo.AnimationInfo.EndFrame;
}

float UGroomCache::GetDuration() const
{
	return GroomCacheInfo.AnimationInfo.Duration;
}

int32 UGroomCache::GetFrameNumberAtTime(const float Time) const
{
	return GetStartFrame() + GetFrameIndexAtTime(Time); 
}

int32 UGroomCache::GetFrameIndexAtTime(const float Time) const
{
	const float FrameTime = GroomCacheInfo.AnimationInfo.SecondsPerFrame;
	if (FrameTime == 0.0f)
	{
		return 0;
	}
	const int32 NumberOfFrames = GroomCacheInfo.AnimationInfo.NumFrames;
	const int32 NormalizedFrame = FMath::Clamp(FMath::RoundToInt(Time / FrameTime), 0, NumberOfFrames - 1);
	return NormalizedFrame; 
}

bool UGroomCache::GetGroomDataAtTime(float Time, FGroomCacheAnimationData& AnimData)
{
	const int32 FrameIndex = GetFrameIndexAtTime(Time);
	return GetGroomDataAtFrameIndex(FrameIndex, AnimData);
}

bool UGroomCache::GetGroomDataAtFrameIndex(int32 FrameIndex, FGroomCacheAnimationData& AnimData)
{
	if (!Chunks.IsValidIndex(FrameIndex))
	{
		return false;
	}

	// This is the reverse operation of how the GroomCacheAnimationData is processed into a GroomCacheChunk
	FGroomCacheChunk& Chunk = Chunks[FrameIndex];

	TArray<uint8> TempBytes;
	TempBytes.SetNum(Chunk.DataSize);

	// This is where the bulk data is loaded from disk
	void* Buffer = TempBytes.GetData();
	Chunk.BulkData.GetCopy(&Buffer, true);

	// The bulk data buffer is then serialized into GroomCacheAnimationData
	FMemoryReader Ar(TempBytes, true);
	AnimData.Serialize(Ar);

	return true;
}

void UGroomCache::SetGroomAnimationInfo(const FGroomAnimationInfo& AnimInfo)
{
	GroomCacheInfo.AnimationInfo = AnimInfo;

	// Ensure that the guides groom cache serialize only positions
	if (GroomCacheInfo.Type == EGroomCacheType::Guides)
	{
		GroomCacheInfo.AnimationInfo.Attributes &= EGroomCacheAttributes::Position;
	}
}

EGroomCacheType UGroomCache::GetType() const
{
	return GroomCacheInfo.Type;
}

void FGroomCacheChunk::Serialize(FArchive& Ar, UObject* Owner, int32 ChunkIndex)
{
	Ar << DataSize;
	Ar << FrameIndex;

	// Forced not inline means the bulk data won't automatically be loaded when we deserialize
	// but only when we explicitly take action to load it
	BulkData.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload | BULKDATA_SerializeCompressed);
	BulkData.Serialize(Ar, Owner, ChunkIndex, false);
}

FGroomCacheProcessor::FGroomCacheProcessor(EGroomCacheType InType, EGroomCacheAttributes InAttributes)
: Attributes(InAttributes)
, Type(InType)
{
}

void FGroomCacheProcessor::AddGroomSample(TArray<FHairGroupData>&& GroupData)
{
	TArray<uint8> TempBytes;
	FMemoryWriter Ar(TempBytes, true);

	// The HairGroupData is converted into GroomCacheAnimationData and serialized to a buffer
	FGroomCacheAnimationData AnimData(MoveTemp(GroupData), FGroomCacheInfo::GetCurrentVersion(), Type, Attributes);
	AnimData.Serialize(Ar);

	FGroomCacheChunk& Chunk = Chunks.AddDefaulted_GetRef();

	// The buffer is then stored into bulk data
	const int32 DataSize = TempBytes.Num();
	Chunk.DataSize = DataSize;
	Chunk.FrameIndex = Chunks.Num() - 1;
	Chunk.BulkData.Lock(LOCK_READ_WRITE);
	void* ChunkBuffer = Chunk.BulkData.Realloc(DataSize);
	FMemory::Memcpy(ChunkBuffer, TempBytes.GetData(), DataSize);
	Chunk.BulkData.Unlock();
}

void FGroomCacheProcessor::TransferChunks(UGroomCache* GroomCache)
{
	GroomCache->Chunks = MoveTemp(Chunks);
}
