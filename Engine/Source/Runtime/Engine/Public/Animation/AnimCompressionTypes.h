// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "Misc/MemStack.h"
#include "BonePose.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimationAsset.h"

#include "Async/MappedFileHandle.h"
#include "HAL/PlatformFilemanager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/Paths.h"
#include "Serialization/BulkData.h"

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include "ProfilingDebugging/CsvProfiler.h"

#include "AnimCompressionTypes.generated.h"

/**
 * Indicates animation data key format.
 */
UENUM()
enum AnimationKeyFormat
{
	AKF_ConstantKeyLerp,
	AKF_VariableKeyLerp,
	AKF_PerTrackCompression,
	AKF_MAX,
};

class FMemoryReader;
class FMemoryWriter;

class UAnimCompress;
class UAnimCurveCompressionSettings;
class USkeleton;

extern FGuid GenerateGuidFromRawAnimData(const TArray<FRawAnimSequenceTrack>& RawAnimationData, const FRawCurveTracks& RawCurveData);

template<typename ArrayClass>
struct ENGINE_API FCompressedOffsetDataBase
{
	ArrayClass OffsetData;

	int32 StripSize;

	FCompressedOffsetDataBase(int32 InStripSize = 2)
		: StripSize(InStripSize)
	{}

	void SetStripSize(int32 InStripSize)
	{
		ensure(InStripSize > 0);
		StripSize = InStripSize;
	}

	const int32 GetOffsetData(int32 StripIndex, int32 Offset) const
	{
		checkSlow(OffsetData.IsValidIndex(StripIndex * StripSize + Offset));

		return OffsetData[StripIndex * StripSize + Offset];
	}

	void SetOffsetData(int32 StripIndex, int32 Offset, int32 Value)
	{
		checkSlow(OffsetData.IsValidIndex(StripIndex * StripSize + Offset));
		OffsetData[StripIndex * StripSize + Offset] = Value;
	}

	void AddUninitialized(int32 NumOfTracks)
	{
		OffsetData.AddUninitialized(NumOfTracks*StripSize);
	}

	void Empty(int32 NumOfTracks = 0)
	{
		OffsetData.Empty(NumOfTracks*StripSize);
	}

	int32 GetMemorySize() const
	{
		return sizeof(int32)*OffsetData.Num() + sizeof(int32);
	}

	int32 GetNumTracks() const
	{
		return OffsetData.Num() / StripSize;
	}

	bool IsValid() const
	{
		return (OffsetData.Num() > 0);
	}
};

// Helper for buiilding DDC keys of settings
struct FArcToHexString
{
private:
	TArray<uint8> TempBytes;

public:
	FMemoryWriter Ar;

	FArcToHexString()
		: Ar(TempBytes)
	{
		TempBytes.Reserve(64);
	}

	FString MakeString() const
	{
		FString Key;
		const uint8* SettingsAsBytes = TempBytes.GetData();
		Key.Reserve(TempBytes.Num() + 1);
		for (int32 ByteIndex = 0; ByteIndex < TempBytes.Num(); ++ByteIndex)
		{
			ByteToHex(SettingsAsBytes[ByteIndex], Key);
		}
		return Key;
	}
};

struct FCompressedOffsetData : public FCompressedOffsetDataBase<TArray<int32>>
{

};


/**
 * Represents a segment of the anim sequence that is compressed.
 */
USTRUCT()
struct ENGINE_API FCompressedSegment
{
	GENERATED_USTRUCT_BODY()

	// Frame where the segment begins in the anim sequence
	int32 StartFrame;

	// Num of frames contained in the segment
	int32 NumFrames;

	// Segment data offset in CompressedByteStream
	int32 ByteStreamOffset;

	/** The compression format that was used to compress translation tracks. */
	TEnumAsByte<enum AnimationCompressionFormat> TranslationCompressionFormat;

	/** The compression format that was used to compress rotation tracks. */
	TEnumAsByte<enum AnimationCompressionFormat> RotationCompressionFormat;

	/** The compression format that was used to compress rotation tracks. */
	TEnumAsByte<enum AnimationCompressionFormat> ScaleCompressionFormat;

	FCompressedSegment()
		: StartFrame(0)
		, NumFrames(0)
		, ByteStreamOffset(0)
		, TranslationCompressionFormat(ACF_None)
		, RotationCompressionFormat(ACF_None)
		, ScaleCompressionFormat(ACF_None)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FCompressedSegment &Segment)
	{
		return Ar << Segment.StartFrame << Segment.NumFrames << Segment.ByteStreamOffset
			<< Segment.TranslationCompressionFormat << Segment.RotationCompressionFormat << Segment.ScaleCompressionFormat;
	}
};

struct ENGINE_API FCompressibleAnimData
{
public:
	FCompressibleAnimData();

	FCompressibleAnimData(UAnimCompress* InRequestedCompressionScheme, UAnimCurveCompressionSettings* InCurveCompressionSettings, USkeleton* InSkeleton, EAnimInterpolationType InInterpolation, float InSequenceLength, int32 InNumFrames, const float InAltCompressionErrorThreshold);

	FCompressibleAnimData(class UAnimSequence* InSeq, const bool bPerformStripping, const float InAltCompressionErrorThreshold);

	UAnimCompress* RequestedCompressionScheme;

	UAnimCurveCompressionSettings* CurveCompressionSettings;

	USkeleton* Skeleton;

	TArray<FTrackToSkeletonMap> TrackToSkeletonMapTable;

	TArray<FRawAnimSequenceTrack> RawAnimationData;

	TArray<FRawAnimSequenceTrack> AdditiveBaseAnimationData;

	EAnimInterpolationType Interpolation;

	TArray<FBoneData> BoneData;

	FRawCurveTracks RawCurveData;

	float SequenceLength;

	int32 NumFrames;

	bool bIsValidAdditive;

	float AltCompressionErrorThreshold;

	//For Logging
	FString Name;
	FString FullName;
	FName   AnimFName;

	int32 GetApproxRawBoneSize() const
	{
		int32 Total = sizeof(FRawAnimSequenceTrack) * RawAnimationData.Num();
		for (int32 i = 0; i < RawAnimationData.Num(); ++i)
		{
			const FRawAnimSequenceTrack& RawTrack = RawAnimationData[i];
			Total +=
				sizeof(FVector) * RawTrack.PosKeys.Num() +
				sizeof(FQuat) * RawTrack.RotKeys.Num() +
				sizeof(FVector) * RawTrack.ScaleKeys.Num();
		}
		return Total;
	}

	int32 GetApproxRawCurveSize() const
	{
		int32 Total = 0;
		for (const FFloatCurve& Curve : RawCurveData.FloatCurves)
		{
			Total += sizeof(FFloatCurve);
			Total += sizeof(FRichCurveKey) * Curve.FloatCurve.Keys.Num();
		}
		return Total;
	}

	int32 GetApproxRawSize() const
	{
		return GetApproxRawBoneSize() + GetApproxRawCurveSize();
	}

	void Update(struct FCompressedAnimSequence& CompressedData) const;

private:

};

// Wrapper Code
template <typename T>
struct TArrayMaker
{
	using Type = TArray<T>;
};


template <typename T>
struct TNonConstArrayViewMaker
{
	using Type = TArrayView<T>;
};

template <typename T>
struct TArrayViewMaker
{
	using Type = TArrayView<const T>;
};

template <template <typename> class ContainerTypeMakerTemplate>
struct FCompressedAnimDataBase
{
	/**
	 * An array of 4*NumTrack ints, arranged as follows: - PerTrack is 2*NumTrack, so this isn't true any more
	 *   [0] Trans0.Offset
	 *   [1] Trans0.NumKeys
	 *   [2] Rot0.Offset
	 *   [3] Rot0.NumKeys
	 *   [4] Trans1.Offset
	 *   . . .
	 */
	typename ContainerTypeMakerTemplate<int32>::Type CompressedTrackOffsets;

	/**
	 * An array of 2*NumTrack ints, arranged as follows:
		if identity, it is offset
		if not, it is num of keys
	 *   [0] Scale0.Offset or NumKeys
	 *   [1] Scale1.Offset or NumKeys

	 * @TODO NOTE: first implementation is offset is [0], numkeys [1]
	 *   . . .
	 */
	FCompressedOffsetDataBase<typename ContainerTypeMakerTemplate<int32>::Type>  CompressedScaleOffsets;

	/**
	 * ByteStream for compressed animation data.
	 * The memory layout is dependent on the algorithm used to compress the anim sequence.
	 */
	typename ContainerTypeMakerTemplate<uint8>::Type CompressedByteStream;

	/**
	 * The runtime interface to decode and byte swap the compressed animation
	 * May be NULL. Set at runtime - does not exist in editor
	 */
	class AnimEncoding* TranslationCodec;
	class AnimEncoding* RotationCodec;
	class AnimEncoding* ScaleCodec;

	enum AnimationKeyFormat KeyEncodingFormat;

	/** The compression format that was used to compress tracks parts. */
	AnimationCompressionFormat TranslationCompressionFormat;
	AnimationCompressionFormat RotationCompressionFormat;
	AnimationCompressionFormat ScaleCompressionFormat;

	int32 CompressedNumberOfFrames;
	
	template <template <typename> class OtherContainerMaker>
	explicit FCompressedAnimDataBase(FCompressedAnimDataBase<OtherContainerMaker>& InCompressedData)
		: CompressedTrackOffsets(InCompressedData.CompressedTrackOffsets)
		, CompressedByteStream(InCompressedData.CompressedByteStream)

		, TranslationCodec(InCompressedData.TranslationCodec)
		, RotationCodec(InCompressedData.RotationCodec)
		, ScaleCodec(InCompressedData.ScaleCodec)

		, KeyEncodingFormat(InCompressedData.KeyEncodingFormat)
		, TranslationCompressionFormat(InCompressedData.TranslationCompressionFormat)
		, RotationCompressionFormat(InCompressedData.RotationCompressionFormat)
		, ScaleCompressionFormat(InCompressedData.ScaleCompressionFormat)

		, CompressedNumberOfFrames(InCompressedData.CompressedNumberOfFrames)
	{
		CompressedScaleOffsets.OffsetData = InCompressedData.CompressedScaleOffsets.OffsetData;
		CompressedScaleOffsets.StripSize = InCompressedData.CompressedScaleOffsets.StripSize;
	}

	FCompressedAnimDataBase()
		: TranslationCodec(nullptr)
		, RotationCodec(nullptr)
		, ScaleCodec(nullptr)

		, KeyEncodingFormat((AnimationKeyFormat)0)
		, TranslationCompressionFormat((AnimationCompressionFormat)0)
		, RotationCompressionFormat((AnimationCompressionFormat)0)
		, ScaleCompressionFormat((AnimationCompressionFormat)0)

		, CompressedNumberOfFrames(0)
	{

	}

	template <template <typename> class OtherContainerMaker>
	void CopyFromSettings(const FCompressedAnimDataBase<OtherContainerMaker>& Other)
	{
		TranslationCodec = Other.TranslationCodec;
		RotationCodec = Other.RotationCodec;
		ScaleCodec = Other.ScaleCodec;

		KeyEncodingFormat = Other.KeyEncodingFormat;
		TranslationCompressionFormat = Other.TranslationCompressionFormat;
		RotationCompressionFormat = Other.RotationCompressionFormat;
		ScaleCompressionFormat = Other.ScaleCompressionFormat;

		CompressedNumberOfFrames = Other.CompressedNumberOfFrames;
	}

	int64 GetApproxBoneCompressedSize() const
	{
		return (int64)CompressedTrackOffsets.GetTypeSize()*(int64)CompressedTrackOffsets.Num() + (int64)CompressedByteStream.Num() + (int64)CompressedScaleOffsets.GetMemorySize();
	}

	bool IsCompressedDataValid() const
	{
		return CompressedByteStream.Num() > 0 || (TranslationCompressionFormat == ACF_Identity && RotationCompressionFormat == ACF_Identity && ScaleCompressionFormat == ACF_Identity);
	}
};

struct ENGINE_API FCompressibleAnimDataResult : public FCompressedAnimDataBase<TArrayMaker>
{
	class UAnimCompress* CompressionScheme;

	FCompressibleAnimDataResult()
		: CompressionScheme(nullptr)
	{

	}

	template <template <typename> class OtherContainerMaker>
	explicit FCompressibleAnimDataResult(FCompressedAnimDataBase<OtherContainerMaker>& InCompressedData)
		: FCompressedAnimDataBase(InCompressedData)
	{}

	void CopyFrom(const FCompressibleAnimDataResult& Other)
	{
		CompressedTrackOffsets = Other.CompressedTrackOffsets;
		CompressedByteStream = Other.CompressedByteStream;
		CompressedScaleOffsets.OffsetData = Other.CompressedScaleOffsets.OffsetData;
		CompressedScaleOffsets.StripSize = Other.CompressedScaleOffsets.StripSize;

		CopyFromSettings(Other);
	}

	FCompressibleAnimDataResult& operator=(const FCompressibleAnimDataResult& Other)
	{
		CopyFrom(Other);
		CompressionScheme = Other.CompressionScheme;
		return *this;
	}

	void BuildFinalBuffer(TArray<uint8>& OutBuffer);
};

struct ICompressedAnimData
{
public:
	virtual ~ICompressedAnimData() {}

	virtual void SerializeCompressedData(class FArchive& Ar) = 0;

	virtual void ByteSwapIn(TArrayView<uint8> CompressedData, FMemoryReader& MemoryStream) = 0;
	virtual void ByteSwapOut(TArrayView<uint8> CompressedData, FMemoryWriter& MemoryStream) = 0;
};

template<typename T>
TArrayView<T> RebaseTArrayView(const TArrayView<T>& ArrayView, const uint8* OriginalBase, const uint8* NewBase)
{
	if (ArrayView.GetData() != nullptr)
	{
		uint32 Offset = ArrayView.GetData() - OriginalBase;
		T* NewData = (T*)(NewBase + Offset);
		return TArrayView<T>(NewData, ArrayView.Num());
	}
	return ArrayView;
}

struct FUECompressedAnimData : public ICompressedAnimData, public FCompressedAnimDataBase<TNonConstArrayViewMaker>
{
	FUECompressedAnimData() = default;

	template <template <typename> class OtherContainerMaker>
	explicit FUECompressedAnimData(FCompressedAnimDataBase<OtherContainerMaker>& InCompressedData)
		: FCompressedAnimDataBase(InCompressedData)
	{}

	void Reset();

	void InitViewsFromBuffer(const TArrayView<uint8> BulkData);

#if WITH_EDITOR
	void CopyFrom(const FCompressibleAnimDataResult& Other);
#endif

	virtual void SerializeCompressedData(class FArchive& Ar);

	template<typename TArchive>
	void ByteSwapData(TArrayView<uint8> CompresedData, TArchive& MemoryStream);

	virtual void ByteSwapIn(TArrayView<uint8> CompressedData, FMemoryReader& MemoryStream) override { ByteSwapData(CompressedData, MemoryStream); }
	virtual void ByteSwapOut(TArrayView<uint8> CompressedData, FMemoryWriter& MemoryStream) override { ByteSwapData(CompressedData, MemoryStream); }
};

template<uint32 Alignment = DEFAULT_ALIGNMENT>
class TMaybeMappedAllocator
{
public:
	using SizeType = int32;

	enum { NeedsElementType = false };
	enum { RequireRangeCheck = true };

	class ForAnyElementType
	{
	public:

		/** Default constructor. */
		ForAnyElementType()
			: Data(nullptr)
			, MappedHandle(nullptr)
			, MappedRegion(nullptr)
		{}

		/**
		 * Moves the state of another allocator into this one.
		 * Assumes that the allocator is currently empty, i.e. memory may be allocated but any existing elements have already been destructed (if necessary).
		 * @param Other - The allocator to move the state from.  This allocator should be left in a valid empty state.
		 */
		void MoveToEmpty(ForAnyElementType& Other)
		{
			checkSlow(this != &Other);

			Reset();

			Data = Other.Data;
			Other.Data = nullptr;

			MappedRegion = Other.MappedRegion;
			Other.MappedRegion = nullptr;

			MappedHandle = Other.MappedHandle;
			Other.MappedHandle = nullptr;
		}

		/** Destructor. */
		~ForAnyElementType()
		{
			Reset();
		}

		// FContainerAllocatorInterface
		FScriptContainerElement* GetAllocation() const
		{
			return Data;
		}
		void ResizeAllocation(
			SizeType PreviousNumElements,
			SizeType NumElements,
			SIZE_T NumBytesPerElement
		)
		{
			// Avoid calling FMemory::Realloc( nullptr, 0 ) as ANSI C mandates returning a valid pointer which is not what we want.
			if (Data || NumElements)
			{
				check(!MappedHandle && !MappedRegion); // this could be supported, but it probably is never what you want, so we will just assert.
					//checkSlow(((uint64)NumElements*(uint64)ElementTypeInfo.GetSize() < (uint64)INT_MAX));
				Data = (FScriptContainerElement*)FMemory::Realloc(Data, NumElements*NumBytesPerElement, Alignment);
			}
		}
		SizeType CalculateSlackReserve(SizeType NumElements, SIZE_T NumBytesPerElement) const
		{
			check(!MappedHandle && !MappedRegion); // this could be supported, but it probably is never what you want, so we will just assert.
			return DefaultCalculateSlackReserve(NumElements, NumBytesPerElement, true, Alignment);
		}
		SizeType CalculateSlackShrink(SizeType NumElements, SizeType NumAllocatedElements, SIZE_T NumBytesPerElement) const
		{
			check(!MappedHandle && !MappedRegion); // this could be supported, but it probably is never what you want, so we will just assert.
			return DefaultCalculateSlackShrink(NumElements, NumAllocatedElements, NumBytesPerElement, true, Alignment);
		}
		SizeType CalculateSlackGrow(SizeType NumElements, SizeType NumAllocatedElements, SIZE_T NumBytesPerElement) const
		{
			check(!MappedHandle && !MappedRegion); // this could be supported, but it probably is never what you want, so we will just assert.
			return DefaultCalculateSlackGrow(NumElements, NumAllocatedElements, NumBytesPerElement, true, Alignment);
		}

		SIZE_T GetAllocatedSize(SizeType NumAllocatedElements, SIZE_T NumBytesPerElement) const
		{
			return NumAllocatedElements * NumBytesPerElement;
		}

		bool HasAllocation() const
		{
			return !!Data;
		}

		SizeType GetInitialCapacity() const
		{
			return 0;
		}

		void AcceptFileMapping(IMappedFileHandle* InMappedHandle, IMappedFileRegion* InMappedRegion, void *MallocPtr)
		{
			check(!MappedHandle && !Data); // we could support stuff like this, but that usually isn't what we want for streamlined loading
			Reset(); // just in case
			if (InMappedHandle || InMappedRegion)
			{
				MappedHandle = InMappedHandle;
				MappedRegion = InMappedRegion;
				Data = (FScriptContainerElement*)MappedRegion->GetMappedPtr(); //@todo mapped files should probably be const-correct
				check(IsAligned(Data, FPlatformProperties::GetMemoryMappingAlignment()));
			}
			else
			{
				Data = (FScriptContainerElement*)MallocPtr;
			}
		}

		bool IsMapped() const
		{
			return MappedRegion || MappedHandle;
		}
	private:

		FScriptContainerElement* Data;
		IMappedFileHandle* MappedHandle;
		IMappedFileRegion* MappedRegion;

		void Reset()
		{
			if (MappedRegion || MappedHandle)
			{
				delete MappedRegion;
				delete MappedHandle;
				MappedRegion = nullptr;
				MappedHandle = nullptr;
				Data = nullptr; // make sure we don't try to free this pointer
			}
			if (Data)
			{
				FMemory::Free(Data);
				Data = nullptr;
			}
		}


		ForAnyElementType(const ForAnyElementType&);
		ForAnyElementType& operator=(const ForAnyElementType&);
	};

	template<typename ElementType>
	class ForElementType : public ForAnyElementType
	{
	public:

		/** Default constructor. */
		ForElementType()
		{}

		ElementType* GetAllocation() const
		{
			return (ElementType*)ForAnyElementType::GetAllocation();
		}
	};
};

template<typename T, uint32 Alignment = DEFAULT_ALIGNMENT>
class TMaybeMappedArray : public TArray<T, TMaybeMappedAllocator<Alignment>>
{
public:
	TMaybeMappedArray()
	{
	}
	TMaybeMappedArray(TMaybeMappedArray&&) = default;
	TMaybeMappedArray(const TMaybeMappedArray&) = default;
	TMaybeMappedArray& operator=(TMaybeMappedArray&&) = default;
	TMaybeMappedArray& operator=(const TMaybeMappedArray&) = default;

	void AcceptOwnedBulkDataPtr(FOwnedBulkDataPtr* OwnedPtr, int32 Num)
	{
		this->ArrayNum = Num;
		this->ArrayMax = Num;
		this->AllocatorInstance.AcceptFileMapping(OwnedPtr->GetMappedHandle(), OwnedPtr->GetMappedRegion(), (void*)OwnedPtr->GetPointer());
		OwnedPtr->RelinquishOwnership();
	}
};

template <uint32 Alignment>
struct TAllocatorTraits<TMaybeMappedAllocator<Alignment>> : TAllocatorTraitsBase<TMaybeMappedAllocator<Alignment>>
{
	enum { SupportsMove = true };
};

template<typename T, uint32 Alignment>
struct TIsContiguousContainer<TMaybeMappedArray<T, Alignment>>
{
	static constexpr bool Value = TIsContiguousContainer<TArray<T, TMaybeMappedAllocator<Alignment>>>::Value;
};

struct ENGINE_API FCompressedAnimSequence
{
public:

	/**
	 * Version of TrackToSkeletonMapTable for the compressed tracks. Due to baking additive data
	 * we can end up with a different amount of tracks to the original raw animation and so we must index into the
	 * compressed data using this
	 */
	TArray<struct FTrackToSkeletonMap> CompressedTrackToSkeletonMapTable;

	/**
	 * Much like track indices above, we need to be able to remap curve names. The FName inside FSmartName
	 * is serialized and the UID is generated at runtime. Stored in the DDC.
	 */
	TArray<struct FSmartName> CompressedCurveNames;

	/**
	 * ByteStream for compressed animation data.
	 * The memory layout is dependent on the algorithm used to compress the anim sequence.
	 */
#if WITH_EDITOR
	TArray<uint8> CompressedByteStream;
	FByteBulkData OptionalBulk;
#else
	TMaybeMappedArray<uint8> CompressedByteStream;
#endif

	/* Compressed curve data stream used by AnimCurveCompressionCodec */
	TArray<uint8> CompressedCurveByteStream;

	FUECompressedAnimData CompressedDataStructure;

	/** The codec used by the compressed data as determined by the compression settings. */
	class UAnimCurveCompressionCodec* CurveCompressionCodec;

	// The size of the raw data used to create the compressed data
	int32 CompressedRawDataSize;

	void SerializeCompressedData(FArchive& Ar, bool bDDCData, UObject* DataOwner, USkeleton* Skeleton, class UAnimCurveCompressionSettings* CurveCompressionSettings, bool bCanUseBulkData=true);

	int32 GetSkeletonIndexFromTrackIndex(const int32 TrackIndex) const
	{
		return CompressedTrackToSkeletonMapTable[TrackIndex].BoneTreeIndex;
	}

	// Return the number of bytes used
	SIZE_T GetMemorySize() const;
};

struct FRootMotionReset
{

	FRootMotionReset(bool bInEnableRootMotion, ERootMotionRootLock::Type InRootMotionRootLock, bool bInForceRootLock, FTransform InAnimFirstFrame, bool bInIsValidAdditive)
		: bEnableRootMotion(bInEnableRootMotion)
		, RootMotionRootLock(InRootMotionRootLock)
		, bForceRootLock(bInForceRootLock)
		, AnimFirstFrame(InAnimFirstFrame)
		, bIsValidAdditive(bInIsValidAdditive)
	{
	}

	bool bEnableRootMotion;

	ERootMotionRootLock::Type RootMotionRootLock;

	bool bForceRootLock;

	FTransform AnimFirstFrame;

	bool bIsValidAdditive;

	void ResetRootBoneForRootMotion(FTransform& BoneTransform, const FBoneContainer& RequiredBones) const
	{
		switch (RootMotionRootLock)
		{
		case ERootMotionRootLock::AnimFirstFrame: BoneTransform = AnimFirstFrame; break;
		case ERootMotionRootLock::Zero: BoneTransform = FTransform::Identity; break;
		default:
		case ERootMotionRootLock::RefPose: BoneTransform = RequiredBones.GetRefPoseArray()[0]; break;
		}

		if (bIsValidAdditive && RootMotionRootLock != ERootMotionRootLock::AnimFirstFrame)
		{
			//Need to remove default scale here for additives
			BoneTransform.SetScale3D(BoneTransform.GetScale3D() - FVector(1.f));
		}
	}
};

extern void DecompressPose(	FCompactPose& OutPose,
							const FCompressedAnimSequence& CompressedData,
							const FAnimExtractContext& ExtractionContext,
							USkeleton* Skeleton,
							float SequenceLength,
							EAnimInterpolationType Interpolation,
							bool bIsBakedAdditive,
							FName RetargetSource,
							FName SourceName,
							const FRootMotionReset& RootMotionReset);