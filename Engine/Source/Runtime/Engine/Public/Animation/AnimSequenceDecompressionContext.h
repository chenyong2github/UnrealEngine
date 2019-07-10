// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimEncodingHeapAllocator.h"
#include "AnimationCompression.h"
#include "Containers/StaticArray.h"
#include "Animation/AnimSequence.h"

struct FCompressedOffsetData;
struct FCompressedSegment;
class FAnimEncodingDecompressionContext;
class AnimEncoding;

/**
 * Structure to wrap a trivial key handle. Used by the decompression context and encoders.
 */
struct FTrivialAnimKeyHandle
{
	constexpr FTrivialAnimKeyHandle() : Value(-1) {}
	constexpr explicit FTrivialAnimKeyHandle(int32 InValue) : Value(InValue) {}

	constexpr bool IsValid() const { return Value >= 0; }

	int32 Value;
};

#if !USE_SEGMENTING_CONTEXT
// Lightweight decompression context to match API of when USE_SEGMENT_CONTEXT is 1
struct ENGINE_API FAnimSequenceDecompressionContext
{
public:
#if WITH_EDITOR
	FAnimSequenceDecompressionContext(const FCompressibleAnimData& CompressibleAnimData_, const FUECompressedAnimData& CompressedAnimData_) : SequenceLength(CompressibleAnimData_.SequenceLength), Interpolation(CompressibleAnimData_.Interpolation), AnimName(CompressibleAnimData_.AnimFName), CompressedAnimData(CompressedAnimData_), Time(0.f) {}
#endif
	FAnimSequenceDecompressionContext(float SequenceLength_, EAnimInterpolationType Interpolation_, const FName& AnimName_, const FUECompressedAnimData& CompressedAnimData_) : SequenceLength(SequenceLength_), Interpolation(Interpolation_), AnimName(AnimName_), CompressedAnimData(CompressedAnimData_), Time(0.f) {}
	/**
	* We don't disable copying but we also don't retain any state. Copying is required by the UE serialization
	* but retaining state is not necessary and can cause issues.
	*/
	FAnimSequenceDecompressionContext(const FAnimSequenceDecompressionContext& Other) : SequenceLength(Other.SequenceLength), Interpolation(Other.Interpolation), AnimName(Other.AnimName), CompressedAnimData(Other.CompressedAnimData), Time(Other.Time) {}

	// Anim info
	float SequenceLength;
	EAnimInterpolationType Interpolation;
	FName AnimName;

	const FUECompressedAnimData& CompressedAnimData;
	float Time;
	float RelativePos;
	bool bHasScale;

	void Seek(float SampleAtTime)
	{
		Time = SampleAtTime;
		RelativePos = SampleAtTime / SequenceLength;
		bHasScale = CompressedAnimData.CompressedScaleOffsets.IsValid();
	}

	float GetSequenceLength() const { return SequenceLength; }

	int32 GetCompressedNumberOfFrames() const { return CompressedAnimData.CompressedNumberOfFrames; }

	// Following just expose the properties of the anim sequence, 
	AnimEncoding* GetRotationCodec() const { return CompressedAnimData.RotationCodec; }
	AnimEncoding* GetTranslationCodec() const { return CompressedAnimData.TranslationCodec; }
	AnimEncoding* GetScaleCodec() const { return CompressedAnimData.ScaleCodec; }

	const int32* GetCompressedTrackOffsets() const { return CompressedAnimData.CompressedTrackOffsets.GetData(); }
	const uint8* GetCompressedByteStream() const { return CompressedAnimData.CompressedByteStream.GetData(); }
	const FCompressedOffsetDataBase<TArrayView<int32>>* GetCompressedScaleOffsets() const { return &CompressedAnimData.CompressedScaleOffsets; }

	EAnimInterpolationType GetInterpolation() const { return Interpolation; }

	FName GetAnimFName() const { return AnimName; }
};

#else

/**
 * Structure to hold the data required for decompression.
 * It is created from an UAnimSequence and stores all the intermediary
 * information required. A context is bound to a single sequence. Multiple
 * context instances can be bound to the same sequence.
 * In order to re-use a context with a new sequence, it needs to be bound.
 */
struct ENGINE_API FAnimSequenceDecompressionContext
{
	FAnimSequenceDecompressionContext();
	FAnimSequenceDecompressionContext(const UAnimSequence* AnimSeq_);
	~FAnimSequenceDecompressionContext();

	/**
	 * We don't disable copying but we also don't retain any state. Copying is required by the UE serialization
	 * but retaining state is not necessary and can cause issues.
	 */
	FAnimSequenceDecompressionContext(const FAnimSequenceDecompressionContext& Other);
	FAnimSequenceDecompressionContext& operator=(const FAnimSequenceDecompressionContext& Other);

	/**
	 * Seek into an animation sequence at a particular time.
	 */
	void Seek(float SampleAtTime);

	/**
	 * Returns whether this context is bound to the provided anim sequence.
	 */
	bool IsStale(const UAnimSequence* AnimSeq_) const;

	/**
	 * Binds the context to the provided anim sequence. The sequence can be NULL
	 * in which case the context will be reset.
	 */
	void Bind(const UAnimSequence* AnimSeq_);

	inline FTrivialAnimKeyHandle GetTrivialRotationKeyHandle(int32 TrackIndex) const;
	inline FTrivialAnimKeyHandle GetTrivialTranslationKeyHandle(int32 TrackIndex) const;
	inline FTrivialAnimKeyHandle GetTrivialScaleKeyHandle(int32 TrackIndex) const;
	FORCEINLINE void GetTrivialRotation(FTransform& OutAtom, FTrivialAnimKeyHandle KeyHandle) const;
	FORCEINLINE void GetTrivialTranslation(FTransform& OutAtom, FTrivialAnimKeyHandle KeyHandle) const;
	FORCEINLINE void GetTrivialScale(FTransform& OutAtom, FTrivialAnimKeyHandle KeyHandle) const;

	inline int32 GetRotationValueOffset(int32 TrackIndex) const { return (TrackIndex * NumStreamsPerTrack) + 1; }
	inline int32 GetTranslationValueOffset(int32 TrackIndex) const { return (TrackIndex * NumStreamsPerTrack) + 0; }
	inline int32 GetScaleValueOffset(int32 TrackIndex) const { return (TrackIndex * NumStreamsPerTrack) + 2; }

	AnimEncoding* GetRotationCodec() const { return RotationCodec; }
	AnimEncoding* GetTranslationCodec() const { return TranslationCodec; }
	AnimEncoding* GetScaleCodec() const { return ScaleCodec; }

	const int32* GetCompressedTrackOffsets() const { return CompressedTrackOffsets; }
	const uint8* GetCompressedByteStream() const { return CompressedByteStream; }
	const FCompressedOffsetData* GetCompressedScaleOffsets() const { return CompressedScaleOffsets; }

	const UAnimSequence* AnimSeq;

	const int32* CompressedTrackOffsets;
	const uint8* CompressedByteStream;
	const FCompressedOffsetData* CompressedScaleOffsets;

	float Time;
	float RelativePos;

	bool bHasScale;
	bool NeedsTwoSegments;
	bool NeedsInterpolation;
	bool HasSegments;
	bool bIsSorted;

	uint8 NumStreamsPerTrack;

	int32 NumTracks;
	uint16 SegmentIndex0;
	uint16 SegmentIndex1;

	int32 KeyIndex0;
	int32 KeyIndex1;
	int32 SegmentKeyIndex0;
	int32 SegmentKeyIndex1;
	float KeyAlpha;

	int32 PackedTranslationSize0;
	int32 PackedRotationSize0;
	int32 PackedScaleSize0;

	int32 RangeDataSize0;

	const uint8* TrackFlags;
	const uint8* TrivialTrackKeys;
	const uint8* TrackRangeData[2];

	const FCompressedSegment* Segment0;
	const FCompressedSegment* Segment1;

	FAnimEncodingDecompressionContext* EncodingContext;
	TArray<int32, FAnimEncodingHeapAllocator> TrivialTrackStreamOffsets;
	uint32 SequenceCRC;

#if WITH_EDITOR
	TStaticArray<double, 4> PreviousBindTimeStamps;
#endif

private:
	AnimEncoding * RotationCodec;
	AnimEncoding* TranslationCodec;
	AnimEncoding* ScaleCodec;
};

FTrivialAnimKeyHandle FAnimSequenceDecompressionContext::GetTrivialRotationKeyHandle(int32 TrackIndex) const
{
	const int32 TrivialKeyOffset = TrivialTrackStreamOffsets[GetRotationValueOffset(TrackIndex)];
	return FTrivialAnimKeyHandle(TrivialKeyOffset);
}

FTrivialAnimKeyHandle FAnimSequenceDecompressionContext::GetTrivialTranslationKeyHandle(int32 TrackIndex) const
{
	const int32 TrivialKeyOffset = TrivialTrackStreamOffsets[GetTranslationValueOffset(TrackIndex)];
	return FTrivialAnimKeyHandle(TrivialKeyOffset);
}

FTrivialAnimKeyHandle FAnimSequenceDecompressionContext::GetTrivialScaleKeyHandle(int32 TrackIndex) const
{
	const int32 TrivialKeyOffset = TrivialTrackStreamOffsets[GetScaleValueOffset(TrackIndex)];
	return FTrivialAnimKeyHandle(TrivialKeyOffset);
}

FORCEINLINE void FAnimSequenceDecompressionContext::GetTrivialRotation(FTransform& OutAtom, FTrivialAnimKeyHandle KeyHandle) const
{
	const uint8* TrivialKeyData = TrivialTrackKeys + KeyHandle.Value;
	FQuat R0;
	DecompressRotation<ACF_Float96NoW>(R0, TrivialKeyData, TrivialKeyData);
	OutAtom.SetRotation(R0);
}

FORCEINLINE void FAnimSequenceDecompressionContext::GetTrivialTranslation(FTransform& OutAtom, FTrivialAnimKeyHandle KeyHandle) const
{
	const uint8* TrivialKeyData = TrivialTrackKeys + KeyHandle.Value;
	FVector P0;
	DecompressTranslation<ACF_None>(P0, TrivialKeyData, TrivialKeyData);
	OutAtom.SetTranslation(P0);
}

FORCEINLINE void FAnimSequenceDecompressionContext::GetTrivialScale(FTransform& OutAtom, FTrivialAnimKeyHandle KeyHandle) const
{
	const uint8* TrivialKeyData = TrivialTrackKeys + KeyHandle.Value;
	FVector P0;
	DecompressScale<ACF_None>(P0, TrivialKeyData, TrivialKeyData);
	OutAtom.SetScale3D(P0);
}
#endif