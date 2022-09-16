// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimCompressionTypes.h"
#include "AnimationUtils.h"
#include "AnimEncoding.h"
#include "Misc/SecureHash.h"
#include "Interfaces/ITargetPlatform.h"
#include "Animation/AnimBoneCompressionCodec.h"
#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimCurveCompressionCodec.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "AnimationRuntime.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "Animation/AnimSequenceHelpers.h"
#include "Animation/AnimData/AnimDataModel.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonWriter.h"

static FString GCompressionJsonOutput;
static FAutoConsoleVariableRef CVarCompressionJsonOutput(
	TEXT("a.Compression.CompressibleDataOutput"),
	GCompressionJsonOutput,
	TEXT("Whether to output any JSON file containing the compressible data. (comma delimited)\n")
	TEXT(" position: output track positional data\n")
	TEXT(" rotation: output track rotational data\n")
	TEXT(" scale: output track scale data\n")
	TEXT(" curve: output rich curve data\n"),
	ECVF_Cheat
	);

template<typename ArrayValue>
void StripFramesEven(TArray<ArrayValue>& Keys, const int32 NumFrames)
{
	if (Keys.Num() > 1)
	{
		check(Keys.Num() == NumFrames);

		for (int32 DstKey = 1, SrcKey = 2; SrcKey < NumFrames; ++DstKey, SrcKey += 2)
		{
			Keys[DstKey] = Keys[SrcKey];
		}

		const int32 HalfSize = (NumFrames - 1) / 2;
		const int32 StartRemoval = HalfSize + 1;

		Keys.RemoveAt(StartRemoval, NumFrames - StartRemoval);
	}
}

template<typename ArrayValue>
void StripFramesOdd(TArray<ArrayValue>& Keys, const int32 NumFrames)
{
	if (Keys.Num() > 1)
	{
		const int32 NewNumFrames = NumFrames / 2;

		TArray<ArrayValue> NewKeys;
		NewKeys.Reserve(NewNumFrames);

		check(Keys.Num() == NumFrames);

		NewKeys.Add(Keys[0]); //Always keep first 

		//Always keep first and last
		const int32 NumFramesToCalculate = NewNumFrames - 2;

		// Frame increment is ratio of old frame spaces vs new frame spaces 
		const double FrameIncrement = (double)(NumFrames - 1) / (double)(NewNumFrames - 1);

		for (int32 Frame = 0; Frame < NumFramesToCalculate; ++Frame)
		{
			const double NextFramePosition = FrameIncrement * (Frame + 1);
			const int32 Frame1 = (int32)NextFramePosition;
			const float Alpha = (NextFramePosition - (double)Frame1);

			NewKeys.Add(AnimationCompressionUtils::Interpolate(Keys[Frame1], Keys[Frame1 + 1], Alpha));

		}

		NewKeys.Add(Keys.Last()); // Always Keep Last

		const int32 HalfSize = (NumFrames - 1) / 2;
		const int32 StartRemoval = HalfSize + 1;

		Keys = MoveTemp(NewKeys);
	}
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FCompressibleAnimData::FCompressibleAnimData(const FCompressibleAnimData&) = default;
FCompressibleAnimData& FCompressibleAnimData::operator=(const FCompressibleAnimData&) = default;
ICompressedAnimData::ICompressedAnimData(const ICompressedAnimData&) = default;
ICompressedAnimData& ICompressedAnimData::operator=(const ICompressedAnimData&) = default;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void FCompressibleAnimData::WriteCompressionDataToJSON(TArrayView<FName> OriginalTrackNames, TArrayView<FRawAnimSequenceTrack> FinalRawAnimationData, TArrayView<FName> FinalTrackNames) const
{
	const bool bPositionalData = GCompressionJsonOutput.Contains(TEXT("position"));
	const bool bRotationalData = GCompressionJsonOutput.Contains(TEXT("rotation"));
	const bool bScalingData = GCompressionJsonOutput.Contains(TEXT("scale"));
	const bool bCurveData = GCompressionJsonOutput.Contains(TEXT("curve"));

	if (bPositionalData || bRotationalData || bScalingData || bCurveData)
	{
		FString JSONString;
		TSharedRef<TJsonStringWriter<>> Writer = TJsonStringWriter<>::Create(&JSONString);
		const UEnum* InterpolationEnum = FindObject<UEnum>(nullptr, TEXT("/Script/Engine.EAnimInterpolationType"), true);

		Writer->WriteObjectStart();
		{
			// Name
			Writer->WriteValue(TEXT("name"), Name);

			// Interpolation type
			Writer->WriteValue(TEXT("interpolation"), InterpolationEnum->GetValueAsString(Interpolation));

			// Keys
			Writer->WriteValue(TEXT("number_of_keys"), NumberOfKeys);

			// Length
			Writer->WriteValue(TEXT("length_in_seconds"), SequenceLength);

			// Raw Animation
			if ((bPositionalData || bRotationalData || bScalingData) && FinalRawAnimationData.Num())
			{
				Writer->WriteArrayStart(TEXT("animation_tracks"));
				{
					for (int32 TrackIndex = 0; TrackIndex < FinalRawAnimationData.Num(); ++TrackIndex)
					{
						Writer->WriteObjectStart();
							
						// Track name
						Writer->WriteValue(TEXT("name"), FinalTrackNames[TrackIndex].ToString());

						const FRawAnimSequenceTrack& Track = FinalRawAnimationData[TrackIndex];

						// Position
						if (bPositionalData)
						{
							Writer->WriteArrayStart(TEXT("positional_data"));
							{
								for (int32 KeyIndex = 0; KeyIndex < Track.PosKeys.Num(); ++KeyIndex)
								{
									Writer->WriteValue(Track.PosKeys[KeyIndex].ToString());
								}
							}
							Writer->WriteArrayEnd();
						}
							

						// Rotation
						if (bRotationalData)
						{
							Writer->WriteArrayStart(TEXT("rotational_data"));
							{
								for (int32 KeyIndex = 0; KeyIndex < Track.RotKeys.Num(); ++KeyIndex)
								{
									Writer->WriteValue(Track.RotKeys[KeyIndex].ToString());
								}
							}
							Writer->WriteArrayEnd();
						}

						// Scale
						if (bScalingData)
						{
							Writer->WriteArrayStart(TEXT("scaling_data"));
							{
								for (int32 KeyIndex = 0; KeyIndex < Track.ScaleKeys.Num(); ++KeyIndex)
								{
									Writer->WriteValue(Track.ScaleKeys[KeyIndex].ToString());
								}
							}
							Writer->WriteArrayEnd();
						}

						Writer->WriteObjectEnd();
					}
				}
				Writer->WriteArrayEnd();

				// Additive Animation
				if(bIsValidAdditive && AdditiveBaseAnimationData.Num())
				{
					Writer->WriteArrayStart(TEXT("additive_base_tracks"));
					{
						for (int32 TrackIndex = 0; TrackIndex < AdditiveBaseAnimationData.Num(); ++TrackIndex)
						{
							const FRawAnimSequenceTrack& Track = AdditiveBaseAnimationData[TrackIndex];
							Writer->WriteObjectStart();
							{
								// Track name
								Writer->WriteValue(TEXT("name"), OriginalTrackNames[TrackIndex].ToString());							
									
								// Position
								if (bPositionalData)
								{
									Writer->WriteArrayStart(TEXT("positional_data"));
									{
										for (int32 KeyIndex = 0; KeyIndex < Track.PosKeys.Num(); ++KeyIndex)
										{
											Writer->WriteValue(Track.PosKeys[KeyIndex].ToString());
										}
									}
									Writer->WriteArrayEnd();
								}
							

								// Rotation
								if (bRotationalData)
								{
									Writer->WriteArrayStart(TEXT("rotational_data"));
									{
										for (int32 KeyIndex = 0; KeyIndex < Track.RotKeys.Num(); ++KeyIndex)
										{
											Writer->WriteValue(Track.RotKeys[KeyIndex].ToString());
										}
									}
									Writer->WriteArrayEnd();
								}

								// Scale
								if (bScalingData)
								{
									Writer->WriteArrayStart(TEXT("scaling_data"));
									{
										for (int32 KeyIndex = 0; KeyIndex < Track.ScaleKeys.Num(); ++KeyIndex)
										{
											Writer->WriteValue(Track.ScaleKeys[KeyIndex].ToString());
										}
									}
									Writer->WriteArrayEnd();
								}
							}
							Writer->WriteObjectEnd();
						}
					}
					Writer->WriteArrayEnd();
				}
			}

			if (bCurveData && RawFloatCurves.Num())
			{
				// Num curves
				Writer->WriteValue(TEXT("number_of_curves"), RawFloatCurves.Num());
					
				Writer->WriteArrayStart(TEXT("curve_data"));		
				for (const FFloatCurve& FloatCurve : RawFloatCurves)
				{
					Writer->WriteObjectStart();
					{
						Writer->WriteValue(TEXT("curve_name"), FloatCurve.Name.DisplayName.ToString());
						Writer->WriteValue(TEXT("number_of_keys"), FloatCurve.FloatCurve.GetNumKeys());
							
						if(FloatCurve.FloatCurve.GetConstRefOfKeys().Num())
						{
							const UEnum* CurveInterpolationEnum = FindObject<UEnum>(nullptr, TEXT("/Script/Engine.ERichCurveInterpMode"), true);
							const UEnum* TangentModeEnum = FindObject<UEnum>(nullptr, TEXT("/Script/Engine.ERichCurveTangentMode"), true);
							const UEnum* TangentWeightModeEnum = FindObject<UEnum>(nullptr, TEXT("/Script/Engine.ERichCurveTangentWeightMode"), true);
								
							Writer->WriteArrayStart(TEXT("key_data"));
							for (const FRichCurveKey& Key : FloatCurve.FloatCurve.GetConstRefOfKeys())
							{
								Writer->WriteObjectStart();
								{
									Writer->WriteValue(TEXT("time"), Key.Time);
									Writer->WriteValue(TEXT("value"), Key.Value);

									Writer->WriteValue(TEXT("arrive_tangent"), Key.ArriveTangent);
									Writer->WriteValue(TEXT("arrive_tangent_weight"), Key.ArriveTangentWeight);
									Writer->WriteValue(TEXT("leave_tangent"), Key.LeaveTangent);
									Writer->WriteValue(TEXT("leave_tangent_weight"), Key.LeaveTangentWeight);
										
									Writer->WriteValue(TEXT("interpolation_mode"), CurveInterpolationEnum->GetNameStringByValue(Key.InterpMode));
									Writer->WriteValue(TEXT("tangent_mode"), TangentModeEnum->GetNameStringByValue(Key.TangentMode));
									Writer->WriteValue(TEXT("tangent_weight_mode"), TangentWeightModeEnum->GetNameStringByValue(Key.TangentWeightMode));
								}
								Writer->WriteObjectEnd();
							}
							Writer->WriteArrayEnd();
						}
					}
					Writer->WriteObjectEnd();
				}
				Writer->WriteArrayEnd();
			}
		}
		Writer->WriteObjectEnd();
		Writer->Close();

		const FString BasePath = FPaths::ProjectSavedDir();
		const FString FolderPath = BasePath + TEXT("/CompressibleData/");
		FString NameAsFileName = FullName;
		NameAsFileName = NameAsFileName.Replace(TEXT("/"), TEXT("_"));
		int32 LastFullStop = INDEX_NONE;
		NameAsFileName.FindLastChar('.', LastFullStop);
		ensure(LastFullStop != INDEX_NONE);
		NameAsFileName.RemoveAt(LastFullStop, NameAsFileName.Len() - LastFullStop);
					
		const FString FilePath = FolderPath + NameAsFileName + TEXT(".json");
		FFileHelper::SaveStringToFile(JSONString, *FilePath);
	}
}

FCompressibleAnimData::FCompressibleAnimData(class UAnimSequence* InSeq, const bool bPerformStripping)
	: CurveCompressionSettings(InSeq->CurveCompressionSettings)
	, BoneCompressionSettings(InSeq->BoneCompressionSettings)
	, Interpolation(InSeq->Interpolation)
	, SequenceLength(InSeq->GetPlayLength())
	, NumberOfKeys(InSeq->GetNumberOfSampledKeys())
	, bIsValidAdditive(InSeq->IsValidAdditive())
#if WITH_EDITORONLY_DATA
	, ErrorThresholdScale(InSeq->CompressionErrorThresholdScale)
#else
	, ErrorThresholdScale(1.f)
#endif
	, Name(InSeq->GetName())
	, FullName(InSeq->GetFullName())
	, AnimFName(InSeq->GetFName())
{
#if WITH_EDITOR
	TRACE_CPUPROFILER_EVENT_SCOPE(FCompressibleAnimData::FCompressibleAnimData);

	USkeleton* Skeleton = InSeq->GetSkeleton();
	FAnimationUtils::BuildSkeletonMetaData(Skeleton, BoneData);

	RefLocalPoses = Skeleton->GetRefLocalPoses();
	RefSkeleton = Skeleton->GetReferenceSkeleton();

	const bool bHasVirtualBones = InSeq->GetSkeleton()->GetVirtualBones().Num() > 0;

	/* Always get the resampled data to start off with */
	const TArray<FBoneAnimationTrack>& ResampledTrackData = InSeq->GetResampledTrackData();

	SampledFrameRate = InSeq->GetSamplingFrameRate();

	TArray<FName> OriginalTrackNames;
	RawAnimationData.Empty(ResampledTrackData.Num());
	TrackToSkeletonMapTable.Empty(ResampledTrackData.Num());
	OriginalTrackNames.Empty(ResampledTrackData.Num());

	for (const FBoneAnimationTrack& AnimTrack : ResampledTrackData)
	{
		FRawAnimSequenceTrack& Track = RawAnimationData.Add_GetRef(AnimTrack.InternalTrackData);
		UE::Anim::Compression::SanitizeRawAnimSequenceTrack(Track);
		TrackToSkeletonMapTable.Add(AnimTrack.BoneTreeIndex);
		OriginalTrackNames.Add(AnimTrack.Name);
	}

	const bool bIsAdditiveAnimation = InSeq->CanBakeAdditive();
	if (bIsAdditiveAnimation)
	{
		InSeq->BakeOutAdditiveIntoRawData(RawAnimationData, OriginalTrackNames, TrackToSkeletonMapTable, RawFloatCurves, AdditiveBaseAnimationData);
	}
	else
    {
		// In case we require baking down transform curves, do so now meaning Virtual Bone baking will incorporate the correct bone transforms
	    if (InSeq->GetDataModel()->GetNumberOfTransformCurves() > 0)
	    {
		    InSeq->BakeTrackCurvesToRawAnimationTracks(RawAnimationData, OriginalTrackNames, TrackToSkeletonMapTable);
	    }

		RawFloatCurves = InSeq->GetCurveData().FloatCurves;

		// If we aren't additive we must bake virtual bones
	    if (bHasVirtualBones)
	    {
		    InSeq->BakeOutVirtualBoneTracks(RawAnimationData, OriginalTrackNames, TrackToSkeletonMapTable);
	    }
    } 

	// Apply any key reduction if possible
	if (RawAnimationData.Num())
	{ 
		UE::Anim::Compression::CompressAnimationDataTracks(RawAnimationData, NumberOfKeys, InSeq->GetFName(), -1.f, -1.f);
		UE::Anim::Compression::CompressAnimationDataTracks(RawAnimationData, NumberOfKeys, InSeq->GetFName());
	}

	auto IsKeyArrayValidForRemoval = [](const auto& Keys, const auto& IdentityValue) -> bool
	{
		return Keys.Num() == 0 || (Keys.Num() == 1 && Keys[0].Equals(IdentityValue));
	};

	auto IsRawTrackZeroAdditive = [IsKeyArrayValidForRemoval](const FRawAnimSequenceTrack& Track) -> bool
	{
		return IsKeyArrayValidForRemoval(Track.PosKeys, FVector3f::ZeroVector) &&
			IsKeyArrayValidForRemoval(Track.RotKeys, FQuat4f::Identity) &&
			IsKeyArrayValidForRemoval(Track.ScaleKeys, FVector3f::ZeroVector);
	};

	// Verify bone track names and data, removing any bone that does not exist on the skeleton
    // And for additive animations remove any track deemed not to add any additive animation (identity rotation and zero-vector translation and scale)
	// Note on (TrackIndex > 0) below : deliberately stop before track 0, compression code doesn't like getting a completely empty animation
	TArray<FName> FinalTrackNames;

	// Ensure we have any tracks at all	to begin with
	if (OriginalTrackNames.Num())
	{
		TArray<FRawAnimSequenceTrack> TempRawAnimationData;
		TArray<FRawAnimSequenceTrack> TempAdditiveBaseAnimationData;
		TArray<FTrackToSkeletonMap> TempTrackToSkeletonMapTable;
		TempTrackToSkeletonMapTable.Reserve(OriginalTrackNames.Num());
		TempRawAnimationData.Reserve(OriginalTrackNames.Num());
		FinalTrackNames.Reserve(ResampledTrackData.Num());
		TempAdditiveBaseAnimationData.Reserve(AdditiveBaseAnimationData.Num() ? AdditiveBaseAnimationData.Num() : 0);

		// Include root bone track
		FinalTrackNames.Add(OriginalTrackNames[0]);
		TempTrackToSkeletonMapTable.Add(TrackToSkeletonMapTable[0]);
		TempRawAnimationData.Add(RawAnimationData[0]);
		if (AdditiveBaseAnimationData.Num())
		{
			TempAdditiveBaseAnimationData.Add(AdditiveBaseAnimationData[0]);
		}

		const int32 NumTracks = RawAnimationData.Num();
		for (int32 TrackIndex = 1; TrackIndex < NumTracks; ++TrackIndex)
		{
			const FRawAnimSequenceTrack& Track = RawAnimationData[TrackIndex];
			// Try find correct bone index
			const int32 BoneIndex = RefSkeleton.FindBoneIndex(OriginalTrackNames[TrackIndex]);

			const bool bValidBoneIndex = BoneIndex != INDEX_NONE;
			const bool bValidAdditiveTrack = !IsRawTrackZeroAdditive(Track);

			// Only include track if it contains valid (additive) data and its name corresponds to a bone on the skeleton
			if ((!bIsAdditiveAnimation || bValidAdditiveTrack) && bValidBoneIndex)
			{
				FinalTrackNames.Add(OriginalTrackNames[TrackIndex]);
				TempTrackToSkeletonMapTable.Add(TrackToSkeletonMapTable[TrackIndex]);
				TempRawAnimationData.Add(RawAnimationData[TrackIndex]);

				if (AdditiveBaseAnimationData.Num())
				{
					TempAdditiveBaseAnimationData.Add(AdditiveBaseAnimationData[TrackIndex]);
				}
			}
		}

		// Swap out maintained track data
		Swap(RawAnimationData, TempRawAnimationData);
		Swap(TrackToSkeletonMapTable, TempTrackToSkeletonMapTable);

		if (AdditiveBaseAnimationData.Num())
		{
			Swap(AdditiveBaseAnimationData, TempAdditiveBaseAnimationData);
		}
	}

	// Find or add curve names on skeleton
	const FSmartNameMapping* Mapping = Skeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName);
	for (FFloatCurve& Curve : RawFloatCurves)
	{
		Skeleton->VerifySmartName(USkeleton::AnimCurveMappingName, Curve.Name);
	}
	   
	if (bPerformStripping)
	{
		const int32 NumTracks = RawAnimationData.Num();
		
		// End frame does not count towards "Even framed" calculation
		const bool bIsEvenFramed = ((NumberOfKeys - 1) % 2) == 0;

		//Strip every other frame from tracks
		if (bIsEvenFramed)
		{
			for (FRawAnimSequenceTrack& Track : RawAnimationData)
			{
				StripFramesEven(Track.PosKeys, NumberOfKeys);
				StripFramesEven(Track.RotKeys, NumberOfKeys);
				StripFramesEven(Track.ScaleKeys, NumberOfKeys);
			}

			const int32 ActualKeys = NumberOfKeys - 1; // strip bookmark end frame
			NumberOfKeys = (ActualKeys / 2) + 1;
		}
		else
		{
			for (FRawAnimSequenceTrack& Track : RawAnimationData)
			{
				StripFramesOdd(Track.PosKeys, NumberOfKeys);
				StripFramesOdd(Track.RotKeys, NumberOfKeys);
				StripFramesOdd(Track.ScaleKeys, NumberOfKeys);
			}

			const int32 ActualKeys = NumberOfKeys;
			NumberOfKeys = (ActualKeys / 2);
		}
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	NumberOfFrames = NumberOfKeys;
    PRAGMA_ENABLE_DEPRECATION_WARNINGS

	if (GCompressionJsonOutput.Len())
	{
		WriteCompressionDataToJSON(MakeArrayView(OriginalTrackNames), MakeArrayView(RawAnimationData), MakeArrayView(FinalTrackNames));	
	}
#endif
}

FCompressibleAnimData::FCompressibleAnimData(UAnimBoneCompressionSettings* InBoneCompressionSettings, UAnimCurveCompressionSettings* InCurveCompressionSettings, USkeleton* InSkeleton, EAnimInterpolationType InInterpolation, float InSequenceLength, int32 InNumberOfKeys)
	: CurveCompressionSettings(InCurveCompressionSettings)
	, BoneCompressionSettings(InBoneCompressionSettings)
	, Interpolation(InInterpolation)
	, SequenceLength(InSequenceLength)
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	, NumberOfFrames(InNumberOfKeys)
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	, NumberOfKeys(InNumberOfKeys)
	, bIsValidAdditive(false)
	, ErrorThresholdScale(1.f)
{
#if WITH_EDITOR
	RefLocalPoses = InSkeleton->GetRefLocalPoses();
	RefSkeleton = InSkeleton->GetReferenceSkeleton();
	FAnimationUtils::BuildSkeletonMetaData(InSkeleton, BoneData);
#endif
}

FCompressibleAnimData::FCompressibleAnimData()
: CurveCompressionSettings(nullptr)
, BoneCompressionSettings(nullptr)
, Interpolation((EAnimInterpolationType)0)
, SequenceLength(0.f)
PRAGMA_DISABLE_DEPRECATION_WARNINGS
, NumberOfFrames(0)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
, NumberOfKeys(0)
, bIsValidAdditive(false)
, ErrorThresholdScale(1.f)
{
}

void FCompressibleAnimData::Update(FCompressedAnimSequence& InOutCompressedData) const
{
	InOutCompressedData.CompressedTrackToSkeletonMapTable = TrackToSkeletonMapTable;
	InOutCompressedData.CompressedRawDataSize = GetApproxRawSize();

	const int32 NumCurves = RawFloatCurves.Num();
	InOutCompressedData.CompressedCurveNames.Reset(NumCurves);
	InOutCompressedData.CompressedCurveNames.AddUninitialized(NumCurves);
	for (int32 CurveIndex = 0; CurveIndex < NumCurves; ++CurveIndex)
	{
		const FFloatCurve& Curve = RawFloatCurves[CurveIndex];
		InOutCompressedData.CompressedCurveNames[CurveIndex] = Curve.Name;
	}
}

template<typename T>
void WriteArray(FMemoryWriter& MemoryWriter, TArray<T>& Array)
{
	const int64 NumBytes = (Array.GetTypeSize() * Array.Num());
	MemoryWriter.Serialize(Array.GetData(), NumBytes);
}

template<typename T>
void InitArrayView(TArrayView<T>& View, uint8*& DataPtr)
{
	View = TArrayView<T>((T*)DataPtr, View.Num());
	DataPtr += (View.Num() * View.GetTypeSize());
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

void FUECompressedAnimData::SerializeCompressedData(FArchive& Ar)
{
	ICompressedAnimData::SerializeCompressedData(Ar);

	Ar << KeyEncodingFormat;
	Ar << TranslationCompressionFormat;
	Ar << RotationCompressionFormat;
	Ar << ScaleCompressionFormat;

	SerializeView(Ar, CompressedByteStream);
	SerializeView(Ar, CompressedTrackOffsets);
	SerializeView(Ar, CompressedScaleOffsets.OffsetData);
	Ar << CompressedScaleOffsets.StripSize;

	AnimationFormat_SetInterfaceLinks(*this);
}

FString FUECompressedAnimData::GetDebugString() const
{
	FString TranslationFormat = FAnimationUtils::GetAnimationCompressionFormatString(TranslationCompressionFormat);
	FString RotationFormat = FAnimationUtils::GetAnimationCompressionFormatString(RotationCompressionFormat);
	FString ScaleFormat = FAnimationUtils::GetAnimationCompressionFormatString(ScaleCompressionFormat);
	return FString::Printf(TEXT("[%s, %s, %s]"), *TranslationFormat, *RotationFormat, *ScaleFormat);
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

void ValidateUObjectLoaded(UObject* Obj, UObject* Source)
{
#if WITH_EDITOR
	if (FLinkerLoad* ObjLinker = Obj->GetLinker())
	{
		ObjLinker->Preload(Obj);
	}
#endif
	checkf(!Obj->HasAnyFlags(RF_NeedLoad), TEXT("Failed to load %s in %s"), *Obj->GetFullName(), *Source->GetFullName()); // in non editor should have been preloaded by GetPreloadDependencies
}

void FUECompressedAnimDataMutable::BuildFinalBuffer(TArray<uint8>& OutCompressedByteStream)
{
	OutCompressedByteStream.Reset();

	FMemoryWriter MemoryWriter(OutCompressedByteStream);

	WriteArray(MemoryWriter, CompressedTrackOffsets);
	WriteArray(MemoryWriter, CompressedScaleOffsets.OffsetData);
	WriteArray(MemoryWriter, CompressedByteStream);
}

void ICompressedAnimData::SerializeCompressedData(class FArchive& Ar)
{
	Ar << CompressedNumberOfKeys;

#if WITH_EDITORONLY_DATA
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
    CompressedNumberOfFrames = CompressedNumberOfKeys;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	
	if (!Ar.IsFilterEditorOnly())
	{
		Ar << BoneCompressionErrorStats;
	}
#endif
}

#if WITH_EDITOR
struct FAnimDDCDebugData
{
public:
	FAnimDDCDebugData(FName InOwnerName, const TArray<FRawAnimSequenceTrack>& RawData)
	{
		CompressedRawData = RawData;

		OwnerName = InOwnerName;

		MachineName = FPlatformProcess::ComputerName();
		BuildTime = FPlatformTime::StrTimestamp();
		ExeName = FPlatformProcess::ExecutablePath();
		CmdLine = FCommandLine::Get();
	}

	void Display()
	{
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("\n ANIM DDC DEBUG DATA\nOwner Name:%s\n"), *OwnerName.ToString());
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Build Machine:%s\n"), *MachineName);
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Build At:%s\n"), *BuildTime);
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Executable:%s\n"), *ExeName);
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Cmd Line:%s\n"), *CmdLine);

		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Source Raw Tracks:%i\n"), CompressedRawData.Num());
	}

	FName OwnerName;
	FString MachineName;
	FString BuildTime;
	FString ExeName;
	FString CmdLine;
	TArray<FRawAnimSequenceTrack> CompressedRawData;
};

FArchive& operator<<(FArchive& Ar, FAnimDDCDebugData& DebugData)
{
	Ar << DebugData.OwnerName;
	Ar << DebugData.MachineName;
	Ar << DebugData.BuildTime;
	Ar << DebugData.ExeName;
	Ar << DebugData.CmdLine;
	Ar << DebugData.CompressedRawData;

	return Ar;
}
#endif

void FCompressedAnimSequence::SerializeCompressedData(FArchive& Ar, bool bDDCData, UObject* DataOwner, USkeleton* Skeleton, UAnimBoneCompressionSettings* BoneCompressionSettings, UAnimCurveCompressionSettings* CurveCompressionSettings, bool bCanUseBulkData)
{
	Ar << CompressedRawDataSize;
	Ar << CompressedTrackToSkeletonMapTable;
	Ar << CompressedCurveNames;

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

		TArray<uint8> SerializedData;
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

			// Decompression will crash later if the data failed to load so assert now to make it easier to debug in the future.
			checkf(OwnedPtr->GetPointer() != nullptr || Size == 0, TEXT("Compressed animation data failed to load")); 

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
		}
		else
		{
			CompressedByteStream.Empty(NumBytes);
			CompressedByteStream.AddUninitialized(NumBytes);

			if (FPlatformProperties::RequiresCookedData())
			{
				Ar.Serialize(CompressedByteStream.GetData(), NumBytes);
			}
			else
			{
				SerializedData.Empty(NumBytes);
				SerializedData.AddUninitialized(NumBytes);
				Ar.Serialize(SerializedData.GetData(), NumBytes);
			}
		}

		FString BoneCodecDDCHandle;
		FString CurveCodecPath;

		Ar << BoneCodecDDCHandle;
		Ar << CurveCodecPath;

		check(!BoneCodecDDCHandle.Equals(TEXT("None"), ESearchCase::IgnoreCase)); // Failed DDC data?

		int32 NumCurveBytes;
		Ar << NumCurveBytes;

		CompressedCurveByteStream.Empty(NumCurveBytes);
		CompressedCurveByteStream.AddUninitialized(NumCurveBytes);
		Ar.Serialize(CompressedCurveByteStream.GetData(), NumCurveBytes);

		// Lookup our codecs in our settings assets
		ValidateUObjectLoaded(BoneCompressionSettings, DataOwner);
		ValidateUObjectLoaded(CurveCompressionSettings, DataOwner);
		BoneCompressionCodec = BoneCompressionSettings->GetCodec(BoneCodecDDCHandle);
		CurveCompressionCodec = CurveCompressionSettings->GetCodec(CurveCodecPath);

		if (BoneCompressionCodec != nullptr)
		{
			CompressedDataStructure = BoneCompressionCodec->AllocateAnimData();
			CompressedDataStructure->SerializeCompressedData(Ar);
			CompressedDataStructure->Bind(CompressedByteStream);

			// The codec can be null if we are a default object, a sequence with no raw bone data (just curves),
			// or if we are duplicating the sequence during compression (new settings are assigned)
			if (SerializedData.Num() != 0)
			{
				// Swap the buffer into the byte stream.
				FMemoryReader MemoryReader(SerializedData, true);
				MemoryReader.SetByteSwapping(Ar.ForceByteSwapping());
				BoneCompressionCodec->ByteSwapIn(*CompressedDataStructure, CompressedByteStream, MemoryReader);
			}
		}
	}
	else if (Ar.IsSaving() || Ar.IsCountingMemory())
	{
		// Swap the byte stream into a buffer.
		TArray<uint8> SerializedData;

		const bool bIsCooking = !bDDCData && Ar.IsCooking();

		// The codec can be null if we are a default object or a sequence with no raw data, just curves
		if (BoneCompressionCodec != nullptr)
		{
			FMemoryWriter MemoryWriter(SerializedData, true);
			MemoryWriter.SetByteSwapping(Ar.ForceByteSwapping());
			BoneCompressionCodec->ByteSwapOut(*CompressedDataStructure, CompressedByteStream, MemoryWriter);
		}

		// Make sure the entire byte stream was serialized.
		check(NumBytes == SerializedData.Num());

		bool bUseBulkDataForSave = bCanUseBulkData && NumBytes && bIsCooking && Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::MemoryMappedFiles) && Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::MemoryMappedAnimation);

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

			check(RotationCodec != nullptr);

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
			OptionalBulk.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload | BULKDATA_MemoryMappedPayload);
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

		FString BoneCodecDDCHandle = BoneCompressionCodec != nullptr ? BoneCompressionCodec->GetCodecDDCHandle() : TEXT("");
		check(!BoneCodecDDCHandle.Equals(TEXT("None"), ESearchCase::IgnoreCase)); // Will write broken DDC data to DDC!
		Ar << BoneCodecDDCHandle;

		FString CurveCodecPath = CurveCompressionCodec->GetPathName();
		Ar << CurveCodecPath;

		int32 NumCurveBytes = CompressedCurveByteStream.Num();
		Ar << NumCurveBytes;
		Ar.Serialize(CompressedCurveByteStream.GetData(), NumCurveBytes);

		if (BoneCompressionCodec != nullptr)
		{
			CompressedDataStructure->SerializeCompressedData(Ar);
		}
	}

#if WITH_EDITOR
	if (bDDCData)
	{
		FAnimDDCDebugData DebugData(OwnerName, CompressedRawData);
		Ar << DebugData;

		if (Ar.IsLoading() && Skeleton)
		{
			//Temp DDC debug
			//DebugData.Display();
			// Refresh the compressed curve names since the IDs might have changed since
			for (FSmartName& CurveName : CompressedCurveNames)
			{
				Skeleton->VerifySmartName(USkeleton::AnimCurveMappingName, CurveName);
			}
		}
	}
#endif
}

SIZE_T FCompressedAnimSequence::GetMemorySize() const
{
	return	  CompressedTrackToSkeletonMapTable.GetAllocatedSize()
			+ CompressedCurveNames.GetAllocatedSize()
			+ CompressedCurveByteStream.GetAllocatedSize()
			+ CompressedDataStructure->GetApproxCompressedSize()
			+ sizeof(FCompressedAnimSequence);
}

void FCompressedAnimSequence::ClearCompressedBoneData()
{
	CompressedByteStream.Empty(0);
	CompressedDataStructure.Reset();
	BoneCompressionCodec = nullptr;
}

void FCompressedAnimSequence::ClearCompressedCurveData()
{
	CompressedCurveByteStream.Empty(0);
	CurveCompressionCodec = nullptr;
}

void DecompressPose(FCompactPose& OutPose, const FCompressedAnimSequence& CompressedData, const FAnimExtractContext& ExtractionContext, USkeleton* SourceSkeleton, float SequenceLength, EAnimInterpolationType Interpolation, bool bIsBakedAdditive, FName RetargetSource, FName SourceName, const FRootMotionReset& RootMotionReset)
{
	const TArray<FTransform>& RetargetTransforms = SourceSkeleton->GetRefLocalPoses(RetargetSource);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FAnimSequenceDecompressionContext DecompressionContext(SequenceLength, Interpolation, SourceName, *CompressedData.CompressedDataStructure.Get());
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	UE::Anim::Decompression::DecompressPose(OutPose, CompressedData, ExtractionContext, DecompressionContext, RetargetTransforms, RootMotionReset);
}

void DecompressPose(FCompactPose& OutPose, const FCompressedAnimSequence& CompressedData, const FAnimExtractContext& ExtractionContext, USkeleton* SourceSkeleton, float SequenceLength, EAnimInterpolationType Interpolation, bool bIsBakedAdditive, const TArray<FTransform>& RetargetTransforms, FName SourceName, const FRootMotionReset& RootMotionReset)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FAnimSequenceDecompressionContext DecompressionContext(SequenceLength, Interpolation, SourceName, *CompressedData.CompressedDataStructure.Get());
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	UE::Anim::Decompression::DecompressPose(OutPose, CompressedData, ExtractionContext, DecompressionContext, RetargetTransforms, RootMotionReset);
}

FArchive& operator<<(FArchive& Ar, FCompressedOffsetData& D)
{
	Ar << D.OffsetData << D.StripSize;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimationErrorStats& ErrorStats)
{
	Ar << ErrorStats.AverageError;
	Ar << ErrorStats.MaxError;
	Ar << ErrorStats.MaxErrorTime;
	Ar << ErrorStats.MaxErrorBone;
	return Ar;
}
