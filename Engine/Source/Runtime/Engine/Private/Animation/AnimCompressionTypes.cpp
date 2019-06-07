// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimCompressionTypes.h"
#include "AnimationUtils.h"
#include "AnimEncoding.h"
#include "Interfaces/ITargetPlatform.h"
#include "Animation/AnimCurveCompressionCodec.h"
#include "Animation/AnimCurveCompressionSettings.h"

FCompressibleAnimData::FCompressibleAnimData(class UAnimSequence* InSeq) :
#if WITH_EDITOR
	RequestedCompressionScheme(InSeq->CompressionScheme) ,
#endif
	CurveCompressionSettings(InSeq->CurveCompressionSettings)
	, Skeleton(InSeq->GetSkeleton())
	, TrackToSkeletonMapTable(InSeq->GetRawTrackToSkeletonMapTable())
	, Interpolation(InSeq->Interpolation)
	, SequenceLength(InSeq->SequenceLength)
	, NumFrames(InSeq->GetRawNumberOfFrames())
	, bIsValidAdditive(InSeq->IsValidAdditive())
#if WITH_EDITOR
	, CompressCommandletVersion(InSeq->CompressCommandletVersion)
	, RawDataGuid(InSeq->GetRawDataGuid())
#endif
	, RefFrameIndex(InSeq->RefFrameIndex)
	, RefPoseType(InSeq->RefPoseType)
	, AdditiveAnimType(InSeq->AdditiveAnimType)
	, Name(InSeq->GetName())
	, FullName(InSeq->GetFullName())
	, AnimFName(InSeq->GetFName())
{
#if WITH_EDITOR
	FAnimationUtils::BuildSkeletonMetaData(Skeleton, BoneData);

	const bool bHasVirtualBones = InSeq->GetSkeleton()->GetVirtualBones().Num() > 0;

	if (InSeq->CanBakeAdditive())
	{
		TArray<FName> TempTrackNames;
		InSeq->BakeOutAdditiveIntoRawData(RawAnimationData, TempTrackNames, TrackToSkeletonMapTable, RawCurveData, AdditiveBaseAnimationData);

		if (InSeq->RefPoseSeq)
		{
			AdditiveDataGuid = InSeq->RefPoseSeq->GetRawDataGuid();
		}
	}
	else if (bHasVirtualBones)// If we aren't additive we must bake virtual bones
	{
		TArray<FName> TempTrackNames;
		InSeq->BakeOutVirtualBoneTracks(RawAnimationData, TempTrackNames, TrackToSkeletonMapTable);
		RawCurveData = InSeq->RawCurveData;
	}
	else
	{
		RawAnimationData = InSeq->GetRawAnimationData();
		TrackToSkeletonMapTable = InSeq->GetRawTrackToSkeletonMapTable();
		RawCurveData = InSeq->RawCurveData;
	}

	TypeName = TEXT("AnimSeq");
#endif
}

void FCompressibleAnimData::Update(FCompressedAnimSequence& InOutCompressedData) const
{
	InOutCompressedData.CompressedTrackToSkeletonMapTable = TrackToSkeletonMapTable;
	InOutCompressedData.CompressedRawDataSize = GetApproxRawSize();

	const int32 NumCurves = RawCurveData.FloatCurves.Num();
	InOutCompressedData.CompressedCurveNames.Reset(NumCurves);
	InOutCompressedData.CompressedCurveNames.AddUninitialized(NumCurves);
	for (int32 CurveIndex = 0; CurveIndex < NumCurves; ++CurveIndex)
	{
		const FFloatCurve& Curve = RawCurveData.FloatCurves[CurveIndex];
		InOutCompressedData.CompressedCurveNames[CurveIndex] = Curve.Name;
	}
}

template<typename T>
void WriteArray(FMemoryWriter& MemoryWriter, TArray<T>& Array)
{
	const int64 NumBytes = (Array.GetTypeSize() * Array.Num());
	MemoryWriter.Serialize(Array.GetData(), NumBytes);
}

void FCompressibleAnimDataResult::BuildFinalBuffer(TArray<uint8>& OutBuffer)
{
	OutBuffer.Reset();
	FMemoryWriter MemoryWriter(OutBuffer);

	WriteArray(MemoryWriter, CompressedTrackOffsets);
	WriteArray(MemoryWriter, CompressedScaleOffsets.OffsetData);
	WriteArray(MemoryWriter, CompressedByteStream);
}

template<typename T>
void InitArrayView(TArrayView<T>& View, uint8*& DataPtr)
{
	View = TArrayView<T>((T*)DataPtr, View.Num());
	DataPtr += (View.Num() * View.GetTypeSize());
}

template<typename T>
void ResetArrayView(TArrayView<T>& ArrayView)
{
	ArrayView = TArrayView<T>();
}

void FUECompressedAnimData::Reset()
{
	ResetArrayView(CompressedTrackOffsets);
	ResetArrayView(CompressedScaleOffsets.OffsetData);
	ResetArrayView(CompressedByteStream);

	TranslationCompressionFormat = RotationCompressionFormat = ScaleCompressionFormat = ACF_None;
	TranslationCodec = RotationCodec = ScaleCodec = nullptr;
}

void FUECompressedAnimData::InitViewsFromBuffer(const TArrayView<uint8> BulkData)
{
	check(BulkData.Num() > 0);

	uint8* BulkDataPtr = BulkData.GetData();
	
	InitArrayView(CompressedTrackOffsets, BulkDataPtr);
	InitArrayView(CompressedScaleOffsets.OffsetData, BulkDataPtr);
	InitArrayView(CompressedByteStream, BulkDataPtr);

	check((BulkDataPtr - BulkData.GetData()) == BulkData.Num());
}

template<typename T>
void InitArrayViewSize(TArrayView<T>& Dest, const TArray<T>& Src)
{
	Dest = TArrayView<T>((T*)nullptr, Src.Num());
}

#if WITH_EDITOR
void FUECompressedAnimData::CopyFrom(const FCompressibleAnimDataResult& Other)
{
	InitArrayViewSize(CompressedTrackOffsets, Other.CompressedTrackOffsets);
	InitArrayViewSize(CompressedScaleOffsets.OffsetData, Other.CompressedScaleOffsets.OffsetData);
	InitArrayViewSize(CompressedByteStream, Other.CompressedByteStream);

	CompressedScaleOffsets.StripSize = Other.CompressedScaleOffsets.StripSize;

	CopyFromSettings(Other);
}
#endif

template<typename T>
void SerializeView(class FArchive& Ar, TArrayView<T>& View)
{
	int32 Size = View.Num();
	if (Ar.IsLoading())
	{
		Ar << Size;
		View = TArrayView<T>((T*)nullptr, Size);
	}
	else
	{
		Ar << Size;
	}
}

template<typename EnumType>
void SerializeEnum(FArchive& Ar, EnumType& Val)
{
	uint8 Temp = (uint8)Val;
	if (Ar.IsLoading())
	{
		Ar << Temp;
		Val = (EnumType)Temp;
	}
	else
	{
		Ar << Temp;
	}
}

FArchive& operator<<(FArchive& Ar, AnimationCompressionFormat& Fmt)
{
	SerializeEnum(Ar, Fmt);
	return Ar;
}

FArchive& operator<<(FArchive& Ar, AnimationKeyFormat& Fmt)
{
	SerializeEnum(Ar, Fmt);
	return Ar;
}

void FUECompressedAnimData::SerializeCompressedData(class FArchive& Ar)
{
	Ar << KeyEncodingFormat;
	Ar << TranslationCompressionFormat;
	Ar << RotationCompressionFormat;
	Ar << ScaleCompressionFormat;

	Ar << CompressedNumberOfFrames;

	SerializeView(Ar, CompressedTrackOffsets);
	SerializeView(Ar, CompressedScaleOffsets.OffsetData);
	Ar << CompressedScaleOffsets.StripSize;
	SerializeView(Ar, CompressedByteStream);

	AnimationFormat_SetInterfaceLinks(*this);
}

template<typename TArchive, typename T>
void ByteSwapArray(TArchive& MemoryStream, uint8*& StartOfArray, TArrayView<T>& ArrayView)
{
	for (int32 ItemIndex = 0; ItemIndex < ArrayView.Num(); ++ItemIndex)
	{
		AC_UnalignedSwap(MemoryStream, StartOfArray, ArrayView.GetTypeSize());
	}
}

template<typename TArchive>
void ByteSwapCodecData(class AnimEncoding& Codec, TArchive& MemoryStream, FUECompressedAnimData& CompressedData)
{
	check(false);
}

template<>
void ByteSwapCodecData(class AnimEncoding& Codec, FMemoryWriter& MemoryStream, FUECompressedAnimData& CompressedData)
{
	Codec.ByteSwapOut(CompressedData, MemoryStream);
}

template<>
void ByteSwapCodecData(class AnimEncoding& Codec, FMemoryReader& MemoryStream, FUECompressedAnimData& CompressedData)
{
	Codec.ByteSwapIn(CompressedData, MemoryStream);
}

template<typename TArchive>
void FUECompressedAnimData::ByteSwapData(TArrayView<uint8> CompressedData, TArchive& MemoryStream)
{
	//Handle Array Header
	uint8* MovingCompressedDataPtr = CompressedData.GetData();

	ByteSwapArray(MemoryStream, MovingCompressedDataPtr, CompressedTrackOffsets);
	ByteSwapArray(MemoryStream, MovingCompressedDataPtr, CompressedScaleOffsets.OffsetData);
	
	AnimationFormat_SetInterfaceLinks(*this);
	check(RotationCodec);

	ByteSwapCodecData(*RotationCodec, MemoryStream, *this);
}

template void FUECompressedAnimData::ByteSwapData(TArrayView<uint8> CompressedData, FMemoryReader& MemoryStream);
template void FUECompressedAnimData::ByteSwapData(TArrayView<uint8> CompressedData, FMemoryWriter& MemoryStream);

void FCompressedAnimSequence::SerializeCompressedData(FArchive& Ar, bool bDDCData, UObject* DataOwner, UAnimCurveCompressionSettings* CurveCompressionSettings)
{
	Ar << CompressedTrackToSkeletonMapTable;
	Ar << CompressedCurveNames;

	CompressedDataStructure.SerializeCompressedData(Ar);

	// Serialize the compressed byte stream from the archive to the buffer.
	int32 NumBytes = CompressedByteStream.Num();
	Ar << NumBytes;

	if (Ar.IsLoading())
	{
		bool bUseBulkDataForLoad = false;
		if (!bDDCData && Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::FortMappedCookedAnimation)
		{
			Ar << bUseBulkDataForLoad;
		}
		if (bUseBulkDataForLoad)
		{
#if !WITH_EDITOR
			FByteBulkData OptionalBulk;
#endif
			bool bUseMapping = FPlatformProperties::SupportsMemoryMappedFiles() && FPlatformProperties::SupportsMemoryMappedAnimation();
			OptionalBulk.Serialize(Ar, DataOwner, -1, bUseMapping);

			if (!bUseMapping)
			{
				OptionalBulk.ForceBulkDataResident();
			}

			size_t Size = OptionalBulk.GetBulkDataSize();

			FOwnedBulkDataPtr* OwnedPtr = OptionalBulk.StealFileMapping();

#if WITH_EDITOR
			check(!bUseMapping && !OwnedPtr->GetMappedHandle());
			CompressedByteStream.Empty(Size);
			CompressedByteStream.AddUninitialized(Size);
			if (Size)
			{
				FMemory::Memcpy(&CompressedByteStream[0], OwnedPtr->GetPointer(), Size);
			}
#else
			CompressedByteStream.AcceptOwnedBulkDataPtr(OwnedPtr, Size);
#endif
			delete OwnedPtr;

			CompressedDataStructure.InitViewsFromBuffer(CompressedByteStream);
		}
		else
		{
			CompressedByteStream.Empty(NumBytes);
			CompressedByteStream.AddUninitialized(NumBytes);

			if (CompressedByteStream.Num() > 0)
			{
				CompressedDataStructure.InitViewsFromBuffer(CompressedByteStream);
			}

			if (FPlatformProperties::RequiresCookedData())
			{
				Ar.Serialize(CompressedByteStream.GetData(), NumBytes);
			}
			else
			{
				TArray<uint8> SerializedData;
				SerializedData.Empty(NumBytes);
				SerializedData.AddUninitialized(NumBytes);
				Ar.Serialize(SerializedData.GetData(), NumBytes);

				// Swap the buffer into the byte stream.
				FMemoryReader MemoryReader(SerializedData, true);
				MemoryReader.SetByteSwapping(Ar.ForceByteSwapping());

				CompressedDataStructure.ByteSwapIn(CompressedByteStream, MemoryReader);
			}
		}

		FString CurveCodecPath;
		Ar << CurveCodecPath;

		CurveCompressionCodec = CurveCompressionSettings->GetCodec(CurveCodecPath);

		int32 NumCurveBytes;
		Ar << NumCurveBytes;

		CompressedCurveByteStream.Empty(NumCurveBytes);
		CompressedCurveByteStream.AddUninitialized(NumCurveBytes);
		Ar.Serialize(CompressedCurveByteStream.GetData(), NumCurveBytes);
	}
	else if (Ar.IsSaving() || Ar.IsCountingMemory())
	{
		// Swap the byte stream into a buffer.
		TArray<uint8> SerializedData;

		const bool bIsCooking = !bDDCData && Ar.IsCooking();

		// and then use the codecs to byte swap
		FMemoryWriter MemoryWriter(SerializedData, true);
		MemoryWriter.SetByteSwapping(Ar.ForceByteSwapping());
		CompressedDataStructure.ByteSwapOut(CompressedByteStream, MemoryWriter);

		// Make sure the entire byte stream was serialized.
		check(NumBytes == SerializedData.Num());

		bool bUseBulkDataForSave = NumBytes && bIsCooking && Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::MemoryMappedFiles) && Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::MemoryMappedAnimation);

		bool bSavebUseBulkDataForSave = false;
		if (!bDDCData)
		{
			Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
			if (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::FortMappedCookedAnimation)
			{
				bUseBulkDataForSave = false;
			}
			else
			{
				bSavebUseBulkDataForSave = true;
			}
		}

		// Count compressed data.
		Ar.CountBytes(SerializedData.Num(), SerializedData.Num());

		if (bSavebUseBulkDataForSave)
		{
			Ar << bUseBulkDataForSave;
		}
		else
		{
			check(!bUseBulkDataForSave);
		}

#define TEST_IS_CORRECTLY_FORMATTED_FOR_MEMORY_MAPPING 0 //Need to fix this
#if TEST_IS_CORRECTLY_FORMATTED_FOR_MEMORY_MAPPING
		if (!IsTemplate() && bIsCooking)
		{
			TArray<uint8> TempSerialized;
			FMemoryWriter MemoryWriter(TempSerialized, true);
			MemoryWriter.SetByteSwapping(Ar.ForceByteSwapping());

			check(RotationCodec != NULL);

			FMemoryReader MemoryReader(TempSerialized, true);
			MemoryReader.SetByteSwapping(Ar.ForceByteSwapping());

			TArray<uint8> SavedCompressedByteStream = CompressedByteStream;
			CompressedByteStream.Empty();

			check(CompressedByteStream.Num() == Num);

			check(FMemory::Memcmp(SerializedData.GetData(), CompressedByteStream.GetData(), Num) == 0);

			CompressedByteStream = SavedCompressedByteStream;
		}
#endif

		if (bUseBulkDataForSave)
		{
#if WITH_EDITOR
			OptionalBulk.Lock(LOCK_READ_WRITE);
			void* Dest = OptionalBulk.Realloc(NumBytes);
			FMemory::Memcpy(Dest, &(SerializedData[0]), NumBytes);
			OptionalBulk.Unlock();
			OptionalBulk.SetBulkDataFlags(BULKDATA_PayloadAtEndOfFile | BULKDATA_PayloadInSeperateFile | BULKDATA_Force_NOT_InlinePayload | BULKDATA_MemoryMappedPayload);
			OptionalBulk.ClearBulkDataFlags(BULKDATA_ForceInlinePayload);
			OptionalBulk.Serialize(Ar, DataOwner);
#else
			UE_LOG(LogAnimation, Fatal, TEXT("Can't save animation as bulk data in non-editor builds!"));
#endif
		}
		else
		{
			Ar.Serialize(SerializedData.GetData(), SerializedData.Num());
		}

		FString CurveCodecPath = CurveCompressionCodec->GetPathName();
		Ar << CurveCodecPath;

		int32 NumCurveBytes = CompressedCurveByteStream.Num();
		Ar << NumCurveBytes;
		Ar.Serialize(CompressedCurveByteStream.GetData(), NumCurveBytes);
	}
}
