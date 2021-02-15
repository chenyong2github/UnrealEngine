// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AnimSequence.cpp: Skeletal mesh animation functions.
=============================================================================*/ 

#include "Animation/AnimSequence.h"
#include "Misc/MessageDialog.h"
#include "Logging/LogScopedVerbosityOverride.h"
#include "UObject/FrameworkObjectVersion.h"
#include "UObject/UE5MainStreamObjectVersion.h"
#include "Serialization/MemoryReader.h"
#include "UObject/UObjectIterator.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UObjectBase.h"
#include "CoreGlobals.h"
#include "EngineUtils.h"
#include "AnimEncoding.h"
#include "AnimationUtils.h"
#include "BonePose.h"
#include "AnimationRuntime.h"
#include "Animation/AnimCompress.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/BlendSpace.h"
#include "Animation/Rig.h"
#include "Animation/AnimationSettings.h"
#include "Animation/AnimBoneCompressionCodec.h"
#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimCurveCompressionCodec.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "EditorFramework/AssetImportData.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "DerivedDataCacheInterface.h"
#include "Interfaces/ITargetPlatform.h"
#include "Animation/AnimCompressionDerivedData.h"
#include "Animation/AnimCompressionDerivedDataPublic.h"
#include "UObject/UObjectThreadContext.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "Animation/AnimStreamable.h"
#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/CookStats.h"
#include "Animation/CustomAttributesRuntime.h"
#include "Stats/StatsHierarchical.h"
#include "Animation/AnimationPoseData.h"
#include "ITimeManagementModule.h"
#include "CommonFrameRates.h"

#include "HAL/FileManager.h"

#if WITH_EDITOR
#include "Animation/AnimData/AnimDataController.h"
#include "Animation/AnimData/AnimDataModel.h"
#endif // WITH_EDITOR

#include "Animation/AnimSequenceHelpers.h"

#include "Algo/Transform.h"
#include "Math/UnrealMathUtility.h"
#include "Animation/AnimCurveTypes.h"

#define USE_SLERP 0
#define LOCTEXT_NAMESPACE "AnimSequence"

DECLARE_CYCLE_STAT(TEXT("AnimSeq GetBonePose"), STAT_AnimSeq_GetBonePose, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("AnimSeq EvalCurveData"), STAT_AnimSeq_EvalCurveData, STATGROUP_Anim);

#if ENABLE_COOK_STATS
namespace AnimSequenceCookStats
{
	static FCookStats::FDDCResourceUsageStats UsageStats;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("AnimSequence.Usage"), TEXT(""));
	});
}
#endif

CSV_DECLARE_CATEGORY_MODULE_EXTERN(ENGINE_API, Animation);

int32 GPerformFrameStripping = 0;
int32 GPerformFrameStrippingOddFramedAnimations = 0;

static const TCHAR* StripFrameCVarName = TEXT("a.StripFramesOnCompression");
static const TCHAR* OddFrameStripStrippingCVarName = TEXT("a.StripOddFramesWhenFrameStripping");

static FAutoConsoleVariableRef CVarFrameStripping(
	StripFrameCVarName,
	GPerformFrameStripping,
	TEXT("1 = Strip every other frame on animations that have an even number of frames. 0 = off"));

static FAutoConsoleVariableRef CVarOddFrameStripping(
	OddFrameStripStrippingCVarName,
	GPerformFrameStrippingOddFramedAnimations,
	TEXT("1 = When frame stripping apply to animations with an odd number of frames too. 0 = only even framed animations"));



#if WITH_EDITOR

template <typename ArrayType>
FGuid GetArrayGuid(TArrayView<const ArrayType> Array)
{
	FSHA1 Sha;
	Sha.Update((uint8*)Array.GetData(), Array.Num() * Array.GetTypeSize());

	Sha.Final();

	uint32 Hash[5];
	Sha.GetHash((uint8*)Hash);
	FGuid Guid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]);
	return Guid;
}

void OnCVarsChanged()
{
	if (GIsInitialLoad)
	{
		return; // not initialized
	}

	/*static TArray<UAnimSequence*> SequenceCache;
	static FString OutputMessage;
	
	SequenceCache.Reset();

	for (TObjectIterator<UAnimSequence> It; It; ++It)
	{
		SequenceCache.Add(*It);
	}

	SequenceCache.Sort([](const UAnimSequence& A, const UAnimSequence& B)
	{
		return A.GetFName() > B.GetFName();
	});

	OutputMessage.Reset();

	for (UAnimSequence* Seq : SequenceCache)
	{
		const FCompressedAnimSequence& AnimData = Seq->CompressedData;
		const FUECompressedAnimData& UEAnimData = AnimData.CompressedDataStructure;
		const int32 Additive = Seq->IsValidAdditive() ? 1 : 0;
		OutputMessage += FString::Printf(TEXT("%s - %.2f Fr:%i Add:%i TO:%i SO:%i CBS:%i\n"), *Seq->GetName(), Seq->SequenceLength, Seq->GetRawNumberOfFrames(), Additive, UEAnimData.CompressedTrackOffsets.Num(), UEAnimData.CompressedScaleOffsets.OffsetData.Num(), UEAnimData.CompressedByteStream.Num());
		OutputMessage += FString::Printf(TEXT("\t K:%i (%i : %i : %i)\n"), (int32)UEAnimData.KeyEncodingFormat, (int32)UEAnimData.TranslationCompressionFormat, (int32)UEAnimData.RotationCompressionFormat, (int32)UEAnimData.ScaleCompressionFormat);
		OutputMessage += FString::Printf(TEXT("\t Curve Codec:%s\n"), AnimData.CurveCompressionCodec ? *AnimData.CurveCompressionCodec->GetPathName() : TEXT("nullptr"));
		OutputMessage += FString::Printf(TEXT("\t TrackOff:%s\n"), *GetArrayGuid<int32>(UEAnimData.CompressedTrackOffsets).ToString());
		OutputMessage += FString::Printf(TEXT("\t ScaleOff:%s\n"), *GetArrayGuid<int32>(UEAnimData.CompressedScaleOffsets.OffsetData).ToString());
		OutputMessage += FString::Printf(TEXT("\t BoneByteStream:%s\n"), *GetArrayGuid<uint8>(UEAnimData.CompressedByteStream).ToString());
		OutputMessage += FString::Printf(TEXT("\t CurveByteStream:%s\n"), *GetArrayGuid<uint8>(AnimData.CompressedCurveByteStream).ToString());
	}

	OutputMessage += FString::Printf(TEXT("\n\nTotalAnims: %i"), SequenceCache.Num());
	FPlatformApplicationMisc::ClipboardCopy(*OutputMessage);*/

	static bool bFirstRun = true;

	static bool bCompressionFrameStrip = (GPerformFrameStripping == 1);
	static bool bOddFramedStrip = (GPerformFrameStrippingOddFramedAnimations == 1);

	static TArray<UAnimSequence*> SequenceCache;
	static FString OutputMessage;

	const bool bCurrentFrameStrip = (GPerformFrameStripping == 1);
	const bool bCurrentOddFramedStrip = (GPerformFrameStrippingOddFramedAnimations == 1);

	const bool bFrameStripChanged = bCompressionFrameStrip != bCurrentFrameStrip;
	const bool bOddFrameStripChanged = bOddFramedStrip != bCurrentOddFramedStrip;

	if (bFrameStripChanged || bOddFrameStripChanged)
	{
		bCompressionFrameStrip = bCurrentFrameStrip;
		bOddFramedStrip = bCurrentOddFramedStrip;

		SequenceCache.Reset();

		if (!bFirstRun) // No need to do this on the first run, only subsequent runs as temp anim sequences from compression may still be around
		{
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
		bFirstRun = false;

		for (TObjectIterator<UAnimSequence> It; It; ++It)
		{
			SequenceCache.Add(*It);
		}

		if (SequenceCache.Num() == 0)
		{
			return; // Nothing to do
		}

		TArray< TPair<int32, UAnimSequence*> > Sizes;
		
		// Rebake/compress the animations
		for (UAnimSequence* Seq : SequenceCache)
		{
			Seq->RequestSyncAnimRecompression();

			Sizes.Emplace(Seq->GetApproxCompressedSize(), Seq);
		}

		Sizes.Sort([](const TPair<int32, UAnimSequence*>& A, const TPair<int32, UAnimSequence*>& B)
		{
			return A.Key > B.Key;
		});

		OutputMessage.Reset();

		const TCHAR* StripMessage = bCompressionFrameStrip ? TEXT("Stripping: On") : TEXT("Stripping: Off");
		const TCHAR* OddMessage = bOddFramedStrip ? TEXT("Odd Frames: On") : TEXT("Odd Frames: Off");

		OutputMessage += FString::Printf(TEXT("%s - %s\n\n"), StripMessage, OddMessage);

		int32 TotalSize = 0;
		int32 NumAnimations = 0;
		for (const TPair<int32, UAnimSequence*>& Pair : Sizes)
		{
			const bool bIsOddFramed = (Pair.Value->GetNumberOfSampledKeys() % 2) == 0;
			if (bIsOddFramed)
			{
				OutputMessage += FString::Printf(TEXT("%s - %.1fK\n"), *Pair.Value->GetPathName(), (float)Pair.Key / 1000.f);
				TotalSize += Pair.Key;
				NumAnimations++;
			}
		}

		OutputMessage += FString::Printf(TEXT("\n\nTotalAnims: %i TotalSize = %.1fK"), NumAnimations, ((float)TotalSize / 1000.f));
		FPlatformApplicationMisc::ClipboardCopy(*OutputMessage);
	}
}

FAutoConsoleVariableSink AnimationCVarSink(FConsoleCommandDelegate::CreateStatic(&OnCVarsChanged));

FString GetAnimSequenceSpecificCacheKeySuffix(const UAnimSequence& Seq, bool bPerformStripping, float CompressionErrorThresholdScale)
{
	//Make up our content key consisting of:
	//	* Global animation compression version
	//  * Whether to strip frames
	//	* Our raw data GUID
	//	* Our skeleton GUID: If our skeleton changes our compressed data may now be stale
	//	* Baked Additive Flag
	//	* Additive ref pose GUID or hardcoded string if not available
	//	* Compression Settings
	//	* Curve compression settings

	bool bIsValidAdditive = Seq.IsValidAdditive();
	char AdditiveType = bIsValidAdditive ? NibbleToTChar(Seq.AdditiveAnimType) : '0';
	char RefType = bIsValidAdditive ? NibbleToTChar(Seq.RefPoseType) : '0';

	FArcToHexString ArcToHexString;

	ArcToHexString.Ar << CompressionErrorThresholdScale;
	ArcToHexString.Ar << bPerformStripping;
	Seq.BoneCompressionSettings->PopulateDDCKey(ArcToHexString.Ar);
	Seq.CurveCompressionSettings->PopulateDDCKey(ArcToHexString.Ar);

	FString Ret = FString::Printf(TEXT("%i_%s%s%s_%c%c%i_%s_%s"),
		Seq.CompressCommandletVersion,
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		*Seq.GetRawDataGuid().ToString(),
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		*Seq.GetSkeleton()->GetGuid().ToString(),
		*Seq.GetSkeleton()->GetVirtualBoneGuid().ToString(),
		AdditiveType,
		RefType,
		Seq.RefFrameIndex,
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		(bIsValidAdditive && Seq.RefPoseSeq) ? *Seq.RefPoseSeq->GetRawDataGuid().ToString() : TEXT("NoAdditiveGuid"),
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		*ArcToHexString.MakeString()
	);

	return Ret;
}
#endif

/////////////////////////////////////////////////////
// FRequestAnimCompressionParams
FRequestAnimCompressionParams::FRequestAnimCompressionParams(bool bInAsyncCompression, bool bInAllowAlternateCompressor, bool bInOutput)
	: bAsyncCompression(bInAsyncCompression)
	, CompressContext(MakeShared<FAnimCompressContext>(bInAllowAlternateCompressor, bInOutput))
{
	InitFrameStrippingFromCVar();
}

FRequestAnimCompressionParams::FRequestAnimCompressionParams(bool bInAsyncCompression, TSharedPtr<FAnimCompressContext> InCompressContext)
	: bAsyncCompression(bInAsyncCompression)
	, CompressContext(InCompressContext)
{
	InitFrameStrippingFromCVar();
}

void FRequestAnimCompressionParams::InitFrameStrippingFromCVar()
{
	bPerformFrameStripping = (GPerformFrameStripping == 1);
	bPerformFrameStrippingOnOddNumberedFrames = (GPerformFrameStrippingOddFramedAnimations == 1);
}

void FRequestAnimCompressionParams::InitFrameStrippingFromPlatform(const class ITargetPlatform* TargetPlatform)
{
#if WITH_EDITOR
	bPerformFrameStripping = false;

	if (UDeviceProfile* DeviceProfile = UDeviceProfileManager::Get().FindProfile(TargetPlatform->IniPlatformName()))
	{
		int32 CVarPlatformFrameStrippingValue = 0;
		if (DeviceProfile->GetConsolidatedCVarValue(StripFrameCVarName, CVarPlatformFrameStrippingValue))
		{
			bPerformFrameStripping = CVarPlatformFrameStrippingValue == 1;
		}

		int32 CVarPlatformOddAnimFrameStrippingValue = 0;
		if (DeviceProfile->GetConsolidatedCVarValue(OddFrameStripStrippingCVarName, CVarPlatformOddAnimFrameStrippingValue))
		{
			bPerformFrameStrippingOnOddNumberedFrames = CVarPlatformOddAnimFrameStrippingValue == 1;
		}
	}
#endif
}

/////////////////////////////////////////////////////
// FRawAnimSequenceTrackNativeDeprecated

//@deprecated with VER_REPLACED_LAZY_ARRAY_WITH_UNTYPED_BULK_DATA
struct FRawAnimSequenceTrackNativeDeprecated
{
	TArray<FVector> PosKeys;
	TArray<FQuat> RotKeys;
	friend FArchive& operator<<(FArchive& Ar, FRawAnimSequenceTrackNativeDeprecated& T)
	{
		return	Ar << T.PosKeys << T.RotKeys;
	}
};

/////////////////////////////////////////////////////
// FCurveTrack

/** Returns true if valid curve weight exists in the array*/
bool FCurveTrack::IsValidCurveTrack()
{
	bool bValid = false;

	if ( CurveName != NAME_None )
	{
		for (int32 I=0; I<CurveWeights.Num(); ++I)
		{
			// it has valid weight
			if (CurveWeights[I]>KINDA_SMALL_NUMBER)
			{
				bValid = true;
				break;
			}
		}
	}

	return bValid;
}

/** This is very simple cut to 1 key method if all is same since I see so many redundant same value in every frame 
 *  Eventually this can get more complicated 
 *  Will return true if compressed to 1. Return false otherwise
 **/
bool FCurveTrack::CompressCurveWeights()
{
	// if always 1, no reason to do this
	if (CurveWeights.Num() > 1)
	{
		bool bCompress = true;
		// first weight
		float FirstWeight = CurveWeights[0];

		for (int32 I=1; I<CurveWeights.Num(); ++I)
		{
			// see if my key is same as previous
			if (fabs(FirstWeight - CurveWeights[I]) > SMALL_NUMBER)
			{
				// if not same, just get out, you don't like to compress this to 1 key
				bCompress = false;
				break;
			}
		} 

		if (bCompress)
		{
			CurveWeights.Empty();
			CurveWeights.Add(FirstWeight);
			CurveWeights.Shrink();
		}

		return bCompress;
	}

	// nothing changed
	return false;
}

/////////////////////////////////////////////////////////////

// since we want this change for hot fix, I can't change header file, 
// next time move this to the header
float GetIntervalPerKey(int32 NumFrames, float SequenceLength) 
{
	return (NumFrames > 1) ? (SequenceLength / (NumFrames-1)) : MINIMUM_ANIMATION_LENGTH;
}

/////////////////////////////////////////////////////
// UAnimSequence

UAnimSequence::UAnimSequence(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Interpolation(EAnimInterpolationType::Linear)
	, bEnableRootMotion(false)
	, RootMotionRootLock(ERootMotionRootLock::RefPose)
	, bUseNormalizedRootMotionScale(true)
	, bRootMotionSettingsCopiedFromMontage(false)
	, bUseRawDataOnly(!FPlatformProperties::RequiresCookedData())
#if WITH_EDITOR
	, bCompressionInProgress(false)
	, bBlockCompressionRequests(false)
#endif
{
	RateScale = 1.0;

#if WITH_EDITORONLY_DATA
	ImportFileFramerate = 0.0f;
	ImportResampleFramerate = 0;
	bAllowFrameStripping = true;
	CompressionErrorThresholdScale = 1.f;

	CustomAttributesGuid.Invalidate();
	BakedCustomAttributesGuid.Invalidate();

	if (!HasAllFlags(EObjectFlags::RF_ClassDefaultObject))
	{
		check(DataModel);
	}
#endif
}

void UAnimSequence::PostInitProperties()
{
#if WITH_EDITORONLY_DATA
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
	MarkerDataUpdateCounter = 0;
#endif
	Super::PostInitProperties();
}

void UAnimSequence::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
#if WITH_EDITORONLY_DATA
	if (AssetImportData)
	{
		OutTags.Add( FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden) );
	}

	OutTags.Add(FAssetRegistryTag(TEXT("Compression Ratio"), FString::Printf(TEXT("%.03f"), (float)GetApproxCompressedSize() / (float)GetUncompressedRawSize()), FAssetRegistryTag::TT_Numerical));
	if (DataModel)
	{
		OutTags.Add(FAssetRegistryTag(TEXT("Source Frame Rate"), FString::Printf(TEXT("%.2f"), GetDataModel()->GetFrameRate().AsDecimal()), FAssetRegistryTag::TT_Numerical));
		OutTags.Add(FAssetRegistryTag(TEXT("Number of Frames"), FString::Printf(TEXT("%.i"), GetDataModel()->GetNumberOfFrames()), FAssetRegistryTag::TT_Numerical));
		OutTags.Add(FAssetRegistryTag(TEXT("Number of Keys"), FString::Printf(TEXT("%i"), GetDataModel()->GetNumberOfKeys()), FAssetRegistryTag::TT_Numerical));
	}
	else
	{
		OutTags.Add(FAssetRegistryTag(TEXT("Source Frame Rate"), TEXT("0.0"), FAssetRegistryTag::TT_Numerical));
		OutTags.Add(FAssetRegistryTag(TEXT("Number of Frames"), TEXT("0"), FAssetRegistryTag::TT_Numerical));
		OutTags.Add(FAssetRegistryTag(TEXT("Number of Keys"), TEXT("0"), FAssetRegistryTag::TT_Numerical));
	}
#endif

	OutTags.Add(FAssetRegistryTag(TEXT("Compressed Size (KB)"), FString::Printf(TEXT("%.02f"), (float)GetApproxCompressedSize() / 1024.0f), FAssetRegistryTag::TT_Numerical));
	OutTags.Add(FAssetRegistryTag(TEXT("Target Frame Rate"), FString::Printf(TEXT("%.2f"), TargetFrameRate.AsDecimal()), FAssetRegistryTag::TT_Numerical));
	Super::GetAssetRegistryTags(OutTags);
}

void UAnimSequence::AddReferencedObjects(UObject* This, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(This, Collector);

	UAnimSequence* AnimSeq = CastChecked<UAnimSequence>(This);
	Collector.AddReferencedObject(AnimSeq->CompressedData.BoneCompressionCodec);
	Collector.AddReferencedObject(AnimSeq->CompressedData.CurveCompressionCodec);
}

int32 UAnimSequence::GetUncompressedRawSize() const
{
#if WITH_EDITOR
	if (DataModel)
	{
		const int32 BoneRawSize = FRawAnimSequenceTrack::SingleKeySize * DataModel->GetNumBoneTracks() * DataModel->GetNumberOfKeys();
	int32 CurveRawSize = 0;

		for (const FFloatCurve& Curve : DataModel->GetCurveData().FloatCurves)
	{
		CurveRawSize += sizeof(FFloatCurve);
		CurveRawSize += sizeof(FRichCurveKey) * Curve.FloatCurve.Keys.Num();
	}
	return BoneRawSize + CurveRawSize;
}

#endif // WITH_EDITOR

	return 0;
}

int32 UAnimSequence::GetApproxRawSize() const
{
#if WITH_EDITOR
	if (DataModel)
	{
		int32 Total = sizeof(FRawAnimSequenceTrack) * DataModel->GetNumBoneTracks();
		for (const FBoneAnimationTrack& AnimationTrack : DataModel->GetBoneAnimationTracks())
	{
			Total += AnimationTrack.InternalTrackData.PosKeys.Num() * AnimationTrack.InternalTrackData.PosKeys.GetTypeSize();
			Total += AnimationTrack.InternalTrackData.RotKeys.Num() * AnimationTrack.InternalTrackData.RotKeys.GetTypeSize();
			Total += AnimationTrack.InternalTrackData.ScaleKeys.Num() * AnimationTrack.InternalTrackData.ScaleKeys.GetTypeSize();
	}

		for (const FFloatCurve& Curve : DataModel->GetCurveData().FloatCurves)
	{
		Total += sizeof(FFloatCurve);
		Total += sizeof(FRichCurveKey) * Curve.FloatCurve.Keys.Num();
	}
	return Total;
}
#endif // WITH_EDITOR

	return 0;
}

int32 UAnimSequence::GetApproxBoneCompressedSize() const
{
	return CompressedData.CompressedDataStructure != nullptr ? CompressedData.CompressedDataStructure->GetApproxCompressedSize() : 0;
}

int32 UAnimSequence::GetApproxCompressedSize() const
{
	int32 BoneTotal = GetApproxBoneCompressedSize();
	int32 CurveTotal = CompressedData.CompressedCurveByteStream.Num();
	return BoneTotal + CurveTotal;
}

/**
 * Deserializes old compressed track formats from the specified archive.
 */
static void LoadOldCompressedTrack(FArchive& Ar, FCompressedTrack& Dst, int32 ByteStreamStride)
{
	// Serialize from the archive to a buffer.
	int32 NumBytes = 0;
	Ar << NumBytes;

	TArray<uint8> SerializedData;
	SerializedData.Empty( NumBytes );
	SerializedData.AddUninitialized( NumBytes );
	Ar.Serialize( SerializedData.GetData(), NumBytes );

	// Serialize the key times.
	Ar << Dst.Times;

	// Serialize mins and ranges.
	Ar << Dst.Mins[0] << Dst.Mins[1] << Dst.Mins[2];
	Ar << Dst.Ranges[0] << Dst.Ranges[1] << Dst.Ranges[2];
}

void UAnimSequence::Serialize(FArchive& Ar)
{
	LLM_SCOPE(ELLMTag::Animation);
	
	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);
	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);

	Super::Serialize(Ar);

	FStripDataFlags StripFlags( Ar );
	if( !StripFlags.IsEditorDataStripped() )
	{
#if WITH_EDITORONLY_DATA
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Ar << RawAnimationData;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		if (!Ar.IsCooking())
		{
			if (Ar.UE4Ver() >= VER_UE4_ANIMATION_ADD_TRACKCURVES)
			{
				if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::RemovingSourceAnimationData)
				{
					PRAGMA_DISABLE_DEPRECATION_WARNINGS
					Ar << SourceRawAnimationData_DEPRECATED;

					if (SourceRawAnimationData_DEPRECATED.Num())
					{
						// Set RawAnimationData to Source
						RawAnimationData = SourceRawAnimationData_DEPRECATED;
						SourceRawAnimationData_DEPRECATED.Empty();
						MarkRawDataAsModified();
					}
					PRAGMA_ENABLE_DEPRECATION_WARNINGS
				}
			}
		}
#endif // WITH_EDITORONLY_DATA
	}

	if (Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::MoveCompressedAnimDataToTheDDC)
	{
		// Serialize the compressed byte stream from the archive to the buffer.
		int32 NumBytes;
		Ar << NumBytes;

		TArray<uint8> SerializedData;
		SerializedData.AddUninitialized(NumBytes);
		Ar.Serialize(SerializedData.GetData(), NumBytes);
	}
	else
	{
		const bool bIsCooking = Ar.IsCooking();
		const bool bIsDuplicating = Ar.HasAnyPortFlags(PPF_DuplicateForPIE) || Ar.HasAnyPortFlags(PPF_Duplicate);
		const bool bIsTransacting = Ar.IsTransacting();
		const bool bIsCookingForDedicatedServer = bIsCooking && Ar.CookingTarget()->IsServerOnly();
		const bool bIsCountingMemory = Ar.IsCountingMemory();
		const bool bCookingTargetNeedsCompressedData = bIsCooking && (!UAnimationSettings::Get()->bStripAnimationDataOnDedicatedServer || !bIsCookingForDedicatedServer || bEnableRootMotion);

		bool bSerializeCompressedData = bCookingTargetNeedsCompressedData || bIsDuplicating || bIsTransacting || bIsCountingMemory;
		Ar << bSerializeCompressedData;

		if (bCookingTargetNeedsCompressedData)
		{
			if(GetSkeleton())
			{
				// Validate that we are cooking valid compressed data.
				checkf(Ar.IsObjectReferenceCollector() || (GetSkeletonVirtualBoneGuid() == GetSkeleton()->GetVirtualBoneGuid()), TEXT("Attempting to cook animation '%s' containing invalid virtual bone guid! Animation:%s Skeleton:%s"), *GetFullName(), *GetSkeletonVirtualBoneGuid().ToString(EGuidFormats::HexValuesInBraces), *GetSkeleton()->GetVirtualBoneGuid().ToString(EGuidFormats::HexValuesInBraces));
			}
		}

		if (bSerializeCompressedData)
		{
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			SerializeCompressedData(Ar,false);
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
			Ar << bUseRawDataOnly;
		}
	}

#if WITH_EDITORONLY_DATA
	if (Ar.IsLoading() && Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::RenamingAnimationNumFrames)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		NumberOfKeys = NumFrames;

		// Validate the actual number of keys that is stored, by looking at the maximum number of keys for any given animation track 
		int32 MaxNumberOfTrackKeys = 0;
		for (const FRawAnimSequenceTrack& Track : RawAnimationData)
		{
			MaxNumberOfTrackKeys = FMath::Max(MaxNumberOfTrackKeys, Track.PosKeys.Num());
			MaxNumberOfTrackKeys = FMath::Max(MaxNumberOfTrackKeys, Track.RotKeys.Num());
			MaxNumberOfTrackKeys = FMath::Max(MaxNumberOfTrackKeys, Track.ScaleKeys.Num());
		}

		// Test whether or not there are more track keys than the value stored, only check for greater then as uniform tracks will have a single key and identity scaling with result in zero keys
		if (MaxNumberOfTrackKeys > NumberOfKeys)
		{
			UE_LOG(LogAnimation, Warning, TEXT("Animation %s needs resaving - Invalid number of keys %i stored according to maximum number animation data track keys. Setting new number of keys %i."), *GetName(), NumberOfKeys, MaxNumberOfTrackKeys);
			NumberOfKeys = MaxNumberOfTrackKeys;
		}

		// Update stored frame rate according to number of keys and play length
		UpdateFrameRate();

		// In case there is any animation data available (not valid for curve only animations), verify that the new frame-rate matches up with the expected number of frames/keys and vice versa
		if (RawAnimationData.Num())
		{
			const int32 NumberOfFrames = FMath::Max(NumberOfKeys - 1, 1);
			const float SampledTime = SamplingFrameRate.AsSeconds(NumberOfFrames);

			// Validate that provided the number of keys and frame-rate the sequence length is accurate
			if (!FMath::IsNearlyEqual(SampledTime, SequenceLength, KINDA_SMALL_NUMBER))
			{
				UE_LOG(LogAnimation, Warning, TEXT("Animation %s needs resaving - Inaccurate sequence length %5.5f according to number of frames and frame rate (delta of %f). Setting new length %5.5f."), *GetName(), SequenceLength, SampledTime - SequenceLength, SampledTime);
				SequenceLength = SampledTime;
			}
		}
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	if ( Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_ASSET_IMPORT_DATA_AS_JSON && !AssetImportData)
	{
		// AssetImportData should always be valid
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}

	// SourceFilePath and SourceFileTimestamp were moved into a subobject
	if ( Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_ADDED_FBX_ASSET_IMPORT_DATA && AssetImportData)
	{
		// AssetImportData should always have been set up in the constructor where this is relevant
		FAssetImportInfo Info;
		Info.Insert(FAssetImportInfo::FSourceFile(SourceFilePath_DEPRECATED));
		AssetImportData->SourceData = MoveTemp(Info);
		
		SourceFilePath_DEPRECATED = TEXT("");
		SourceFileTimestamp_DEPRECATED = TEXT("");
	}
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
bool UAnimSequence::IsValidToPlay() const
{
	// make sure sequence length is valid and raw animation data exists, and compressed
	return (GetPlayLength() > 0.f);
}

void UAnimSequence::UpdateFrameRate()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const int32 NumberOfFrames = FMath::Max(NumberOfKeys - 1, 1);

	// Generate the frame-rate according to the number of frames and sequence length
	const double DecimalFrameRate = (double)NumberOfFrames / ((double)SequenceLength > 0.0 ? (double)SequenceLength : 1.0);

	// Account for non-whole number frame rates using large denominator
	const double Denominator = 1000000.0;
	SamplingFrameRate = FFrameRate(DecimalFrameRate * Denominator, Denominator);	 

	// Try to simplifiy the frame rate, in case it is a multiple of the commonly used frame rates e.g. 10000/300000 -> 1/30
	TArrayView<const FCommonFrameRateInfo> CommonFrameRates = FModuleManager::LoadModulePtr<ITimeManagementModule>("TimeManagement")->GetAllCommonFrameRates();
	for (const FCommonFrameRateInfo& Info : CommonFrameRates)
	{
		const bool bDoesNotAlreadyMatch = Info.FrameRate.Denominator != SamplingFrameRate.Denominator && Info.FrameRate.Numerator != SamplingFrameRate.Numerator;
		
		if (bDoesNotAlreadyMatch && FMath::IsNearlyEqual(SamplingFrameRate.AsInterval(), Info.FrameRate.AsInterval()))
		{
			SamplingFrameRate = Info.FrameRate;
			break;
		}
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}
#endif

void UAnimSequence::SortSyncMarkers()
{
	// First make sure all SyncMarkers are within a valid range
	for (auto& SyncMarker : AuthoredSyncMarkers)
	{
		SyncMarker.Time = FMath::Clamp(SyncMarker.Time, 0.f, GetPlayLength());
	}

	// Then sort
	AuthoredSyncMarkers.Sort();

	// Then refresh data
	RefreshSyncMarkerDataFromAuthored();
}

void UAnimSequence::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	Super::GetPreloadDependencies(OutDeps);

	// We preload the compression settings because we need them loaded during Serialize to lookup the proper codec
	// which is stored as a path/string.
	if (CurveCompressionSettings != nullptr)
	{
		OutDeps.Add(CurveCompressionSettings);
	}

	if (BoneCompressionSettings != nullptr)
	{
		OutDeps.Add(BoneCompressionSettings);
	}
}

void UAnimSequence::PreSave(const class ITargetPlatform* TargetPlatform)
{
#if WITH_EDITOR
	// Could already be compressing
	WaitOnExistingCompression();

	// make sure if it does contain transform curvesm it contains source data
	// empty track animation still can be made by retargeting to invalid skeleton
	// make sure to not trigger ensure if RawAnimationData is also null
	
	if (DoesNeedRecompress())
	{
		RequestSyncAnimRecompression(); // Update Normal data

		ensureAlwaysMsgf(!bUseRawDataOnly,  TEXT("Animation : %s failed to compress"), *GetName());
	}

	if (TargetPlatform)
	{
		// Update compressed data for platform
		FRequestAnimCompressionParams Params(false, false, false);
		Params.InitFrameStrippingFromPlatform(TargetPlatform);
		RequestAnimCompression(Params);
	}

	WaitOnExistingCompression(); // Wait on updated data

	const bool bIsCooking = (TargetPlatform != nullptr);
	if (!bIsCooking)
	{
		UpdateRetargetSourceAsset();
	}
#endif

	Super::PreSave(TargetPlatform);
}

void UAnimSequence::PostLoad()
{
	//Parent PostLoad will ensure that skeleton is fully loaded
	//before we do anything further in PostLoad
	Super::PostLoad();

#if WITH_EDITOR
	if (RefPoseSeq && RefPoseSeq->HasAnyFlags(EObjectFlags::RF_NeedPostLoad))
	{
		RefPoseSeq->ConditionalPostLoad();
	}

	EnsureValidRawDataGuid();

	// I have to do this first thing in here
	// so that remove all NaNs before even being read
	if(GetLinkerUE4Version() < VER_UE4_ANIMATION_REMOVE_NANS)
	{
		RemoveNaNTracks();
	}

	Controller->RemoveBoneTracksMissingFromSkeleton(GetSkeleton());
#endif // WITH_EDITOR

	// if valid additive, but if base additive isn't 
	// this seems to happen from retargeting sometimes, which we still have to investigate why, 
	// but this causes issue since once this happens this is unrecoverable until you delete from outside of editor
	if (IsValidAdditive())
	{
		if (RefPoseSeq && RefPoseSeq->GetSkeleton() != GetSkeleton())
		{
			// if this happens, there was a issue with retargeting, 
			UE_LOG(LogAnimation, Warning, TEXT("Animation %s - Invalid additive animation base animation (%s)"), *GetName(), *RefPoseSeq->GetName());
			RefPoseSeq = nullptr;
		}
	}

#if WITH_EDITOR
	// Generate transient animation data, used to fallback on whenever the animation modified and not yet compressed (what previously used to be the raw data)
	ResampleAnimationTrackData();

	static bool ForcedRecompressionSetting = FAnimationUtils::GetForcedRecompressionSetting();

	if (ForcedRecompressionSetting)
	{
		//Force recompression
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		RawDataGuid = FGuid::NewGuid();
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		bUseRawDataOnly = true;
	}

	if (bUseRawDataOnly)
	{
		RequestAnimCompression(FRequestAnimCompressionParams(true, false, false));
	}
#endif

	// Ensure notifies are sorted.
	SortNotifies();

	// No animation data is found. Warn - this should check before we check CompressedTrackOffsets size
	// Otherwise, we'll see empty data set crashing game due to no CompressedTrackOffsets
	// You can't check RawAnimationData size since it gets removed during cooking
#if WITH_EDITOR
	if (DataModel->GetNumberOfKeys() == 0 && DataModel->GetNumberOfFloatCurves() == 0 )
	{
		UE_LOG(LogAnimation, Warning, TEXT("No animation data exists for sequence %s (%s)"), *GetName(), (GetOuter() ? *GetOuter()->GetFullName() : *GetFullName()) );

		if (!IsRunningGame())
		{
			static FName NAME_LoadErrors("LoadErrors");
			FMessageLog LoadErrors(NAME_LoadErrors);

			TSharedRef<FTokenizedMessage> Message = LoadErrors.Warning();
			Message->AddToken(FTextToken::Create(LOCTEXT("EmptyAnimationData1", "The Animation ")));
			Message->AddToken(FAssetNameToken::Create(GetPathName(), FText::FromString(GetName())));
			Message->AddToken(FTextToken::Create(LOCTEXT("EmptyAnimationData2", " has no animation data. Recommend to remove.")));
			LoadErrors.Notify();
		}
	}
	// @remove temp hack for fixing length
	// @todo need to fix importer/editing feature
	else if (GetPlayLength() == 0.f )
	{
		ensure(DataModel->GetNumberOfKeys() == 1);
		Controller->SetPlayLength(MINIMUM_ANIMATION_LENGTH);
	}
	// Raw data exists, but missing compress animation data
	else
#endif
	if( GetSkeleton() && !IsCompressedDataValid()
#if WITH_EDITOR
		&& !bCompressionInProgress
#endif
		)
	{
		UE_LOG(LogAnimation, Fatal, TEXT("No animation compression exists for sequence %s (%s)"), *GetName(), (GetOuter() ? *GetOuter()->GetFullName() : *GetFullName()) );
	}

	// Update the virtual bone GUID according to the skeleton
	if (FPlatformProperties::RequiresCookedData())
	{
		if (GetSkeleton())
		{
			SetSkeletonVirtualBoneGuid(GetSkeleton()->GetVirtualBoneGuid());
		}
	}

	for(FAnimNotifyEvent& Notify : Notifies)
	{
		if(Notify.DisplayTime_DEPRECATED != 0.0f)
		{
			Notify.Clear();
			Notify.LinkSequence(this, Notify.DisplayTime_DEPRECATED);
		}
		else
		{
			Notify.LinkSequence(this, Notify.GetTime());
		}
	
		if(Notify.Duration != 0.0f)
		{
			Notify.EndLink.LinkSequence(this, Notify.GetTime() + Notify.Duration);
		}
	}

	if (USkeleton* CurrentSkeleton = GetSkeleton())
	{
		for (FSmartName& CurveName : CompressedData.CompressedCurveNames)
		{
			CurrentSkeleton->VerifySmartName(USkeleton::AnimCurveMappingName, CurveName);
		}
#if WITH_EDITOR
		static const bool DoNotTransactAction = false;
		Controller->FindOrAddCurveNamesOnSkeleton(CurrentSkeleton, ERawCurveTrackTypes::RCT_Transform, DoNotTransactAction);

		for (const FAnimSyncMarker& SyncMarker : AuthoredSyncMarkers)
		{
			CurrentSkeleton->RegisterMarkerName(SyncMarker.MarkerName);
		}
#endif
	}
}

#if WITH_EDITOR
void ShowResaveMessage(const UAnimSequence* Sequence)
{
	if (IsRunningCommandlet())
	{
		UE_LOG(LogAnimation, Log, TEXT("Resave Animation Required(%s, %s): Fixing track data and recompressing."), *GetNameSafe(Sequence), *Sequence->GetPathName());

		static FName NAME_LoadErrors("LoadErrors");
		FMessageLog LoadErrors(NAME_LoadErrors);

		TSharedRef<FTokenizedMessage> Message = LoadErrors.Warning();
		Message->AddToken(FTextToken::Create(LOCTEXT("AnimationNeedsResave1", "The Animation ")));
		Message->AddToken(FAssetNameToken::Create(Sequence->GetPathName(), FText::FromString(GetNameSafe(Sequence))));
		Message->AddToken(FTextToken::Create(LOCTEXT("AnimationNeedsResave2", " needs resave.")));
		LoadErrors.Notify();
	}
}

void UAnimSequence::VerifyTrackMap(USkeleton* MySkeleton)
{
	ValidateModel();

	USkeleton* UseSkeleton = (MySkeleton) ? MySkeleton : GetSkeleton();

	if(Controller->RemoveBoneTracksMissingFromSkeleton(UseSkeleton))
	{
		ShowResaveMessage(this);
	}
}

#endif // WITH_EDITOR
void UAnimSequence::BeginDestroy()
{
#if WITH_EDITOR
	// Could already be compressing
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	WaitOnExistingCompression(false);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif // WITH_EDITOR

	Super::BeginDestroy();

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	ClearCompressedCurveData();
	ClearCompressedBoneData();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

#if WITH_EDITOR
void UAnimSequence::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if(!IsTemplate())
	{
		// Make sure package is marked dirty when doing stuff like adding/removing notifies
		MarkPackageDirty();
	}

	if (AdditiveAnimType != AAT_None)
	{
		if (RefPoseType == ABPT_None)
		{
			// slate will take care of change
			RefPoseType = ABPT_RefPose;
		}
	}

	if (RefPoseSeq != NULL)
	{
		if (RefPoseSeq->GetSkeleton() != GetSkeleton()) // @todo this may require to be changed when hierarchy of skeletons is introduced
		{
			RefPoseSeq = NULL;
		}
	}

	bool bAdditiveSettingsChanged = false;
	bool bCompressionAffectingSettingsChanged = false;
	bool bShouldResample = false;

	if(PropertyChangedEvent.Property)
	{
		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimSequence, RetargetSourceAsset))
		{
			UpdateRetargetSourceAsset();
		}

		const bool bChangedRefFrameIndex = PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimSequence, RefFrameIndex);

		if (bChangedRefFrameIndex)
		{
			bUseRawDataOnly = true;
		}

		if ((bChangedRefFrameIndex && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive) ||
			PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimSequence, AdditiveAnimType) ||
			PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimSequence, RefPoseSeq) ||
			PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimSequence, RefPoseType))
		{
			bAdditiveSettingsChanged = true;
		}
		
		bCompressionAffectingSettingsChanged =   PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimSequence, bAllowFrameStripping)
											  || PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimSequence, CompressionErrorThresholdScale)
											  || PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimSequence, BoneCompressionSettings)
											  || PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimSequence, BoneCompressionSettings);

		bShouldResample = PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimSequence, TargetFrameRate);
	}

	if (bShouldResample)
	{
		ResampleAnimationTrackData();
	}

	const bool bNeedPostProcess = !IsCompressedDataValid() || bAdditiveSettingsChanged || bCompressionAffectingSettingsChanged || bShouldResample;

	// @Todo fix me: This is temporary fix to make sure they always have compressed data
	ValidateModel();
	if (DataModel->GetNumBoneTracks() && bNeedPostProcess)
	{
		// QQ need new function here to queue async compression
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		MarkRawDataAsModified(false);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		RecompressAnimationData();
	}
}
#endif // WITH_EDITOR

// @todo DB: Optimize!
template<typename TimeArray>
static int32 FindKeyIndex(float Time, const TimeArray& Times)
{
	int32 FoundIndex = 0;
	for ( int32 Index = 0 ; Index < Times.Num() ; ++Index )
	{
		const float KeyTime = Times(Index);
		if ( Time >= KeyTime )
		{
			FoundIndex = Index;
		}
		else
		{
			break;
		}
	}
	return FoundIndex;
}

void UAnimSequence::GetBoneTransform(FTransform& OutAtom, int32 TrackIndex, float Time, bool bUseRawData) const
{
	// If the caller didn't request that raw animation data be used . . .
	if ( !bUseRawData && IsCompressedDataValid() )
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		FAnimSequenceDecompressionContext DecompContext(SequenceLength, Interpolation, GetFName(), *CompressedData.CompressedDataStructure);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		DecompContext.Seek(Time);
		if (CompressedData.BoneCompressionCodec)
		{
			CompressedData.BoneCompressionCodec->DecompressBone(DecompContext, TrackIndex, OutAtom);
			return;
		}
	}

#if WITH_EDITOR
	ValidateModel();
	UE::Anim::GetBoneTransformFromModel(DataModel, OutAtom, TrackIndex, Time, Interpolation);
#endif
}

void UAnimSequence::GetBoneTransform(FTransform& OutAtom, int32 TrackIndex, FAnimSequenceDecompressionContext& DecompContext, bool bUseRawData) const
{
	// If the caller didn't request that raw animation data be used . . .
	if (!bUseRawData && IsCompressedDataValid())
	{
		if (CompressedData.BoneCompressionCodec)
		{
			CompressedData.BoneCompressionCodec->DecompressBone(DecompContext, TrackIndex, OutAtom);
			return;
		}
	}

#if WITH_EDITOR
	ValidateModel();
	UE::Anim::GetBoneTransformFromModel(DataModel, OutAtom, TrackIndex, DecompContext.Time, Interpolation);
#endif
}

void UAnimSequence::ExtractBoneTransform(const TArray<struct FRawAnimSequenceTrack>& InRawAnimationData, FTransform& OutAtom, int32 TrackIndex, float Time) const
{
	// Bail out if the animation data doesn't exists (e.g. was stripped by the cooker).
	if (InRawAnimationData.IsValidIndex(TrackIndex))
	{
		UE_LOG(LogAnimation, Log, TEXT("UAnimSequence::GetBoneTransform : No valid anim data track!"));
		OutAtom.SetIdentity();
		return;
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return ExtractBoneTransform(InRawAnimationData[TrackIndex], OutAtom, Time);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void UAnimSequence::ExtractBoneTransform(const struct FRawAnimSequenceTrack& RawTrack, FTransform& OutAtom, int32 KeyIndex) const
{
	UE::Anim::ExtractBoneTransform(RawTrack, OutAtom, KeyIndex);
}

void UAnimSequence::ExtractBoneTransform(const struct FRawAnimSequenceTrack& RawTrack, FTransform& OutAtom, float Time) const
{
#if WITH_EDITOR
	FAnimationUtils::ExtractTransformFromTrack(Time, NumberOfSampledKeys, GetPlayLength(), RawTrack, Interpolation, OutAtom);
#else
	FAnimationUtils::ExtractTransformFromTrack(Time, CompressedData.CompressedDataStructure->CompressedNumberOfKeys, GetPlayLength(), RawTrack, Interpolation, OutAtom);
#endif
}

void UAnimSequence::HandleAssetPlayerTickedInternal(FAnimAssetTickContext &Context, const float PreviousTime, const float MoveDelta, const FAnimTickRecord &Instance, struct FAnimNotifyQueue& NotifyQueue) const
{
	Super::HandleAssetPlayerTickedInternal(Context, PreviousTime, MoveDelta, Instance, NotifyQueue);

	if (bEnableRootMotion)
	{
		Context.RootMotionMovementParams.Accumulate(ExtractRootMotion(PreviousTime, MoveDelta, Instance.bLooping));
	}
}

FTransform UAnimSequence::ExtractRootTrackTransform(float Pos, const FBoneContainer * RequiredBones) const
{
	const int32 RootBoneIndex = 0;
	const int32 TrackIndex = [this, RootBoneIndex]() -> int32
	{
#if WITH_EDITOR
		if (bUseRawDataOnly)
		{
			ValidateModel();
			if (const FBoneAnimationTrack* RootTrack = DataModel->FindBoneTrackByIndex(RootBoneIndex))
			{
				return DataModel->GetBoneTrackIndex(*RootTrack);
			}
		}
		else
#endif // WITH_EDITOR
		{
			const TArray<FTrackToSkeletonMap>& TrackToSkeletonMap = CompressedData.CompressedTrackToSkeletonMapTable;
			// we assume root is in first data if available = SkeletonIndex == 0 && BoneTreeIndex == 0)
			if ((TrackToSkeletonMap.Num() > 0) && (TrackToSkeletonMap[0].BoneTreeIndex == RootBoneIndex))
			{
				return 0;
			}
		}

		return INDEX_NONE;
	}();

	if(TrackIndex != INDEX_NONE)
	{
		// if we do have root data, then return root data
		FTransform RootTransform;
		GetBoneTransform(RootTransform, TrackIndex, Pos, bUseRawDataOnly);
		return RootTransform;
	}

	// Fallback to root bone from reference skeleton.
	if( RequiredBones )
	{
		const FReferenceSkeleton& RefSkeleton = RequiredBones->GetReferenceSkeleton();
		if( RefSkeleton.GetNum() > 0 )
		{
			return RefSkeleton.GetRefBonePose()[RootBoneIndex];
		}
	}

	USkeleton * MySkeleton = GetSkeleton();
	// If we don't have a RequiredBones array, get root bone from default skeleton.
	if( !RequiredBones &&  MySkeleton )
	{
		const FReferenceSkeleton& RefSkeleton = MySkeleton->GetReferenceSkeleton();
		if( RefSkeleton.GetNum() > 0 )
		{
			return RefSkeleton.GetRefBonePose()[RootBoneIndex];
		}
	}

	// Otherwise, use identity.
	return FTransform::Identity;
}

FTransform UAnimSequence::ExtractRootMotion(float StartTime, float DeltaTime, bool bAllowLooping) const
{
	FRootMotionMovementParams RootMotionParams;

	if (DeltaTime != 0.f)
	{
		bool const bPlayingBackwards = (DeltaTime < 0.f);

		float PreviousPosition = StartTime;
		float CurrentPosition = StartTime;
		float DesiredDeltaMove = DeltaTime;

		do
		{
			// Disable looping here. Advance to desired position, or beginning / end of animation 
			const ETypeAdvanceAnim AdvanceType = FAnimationRuntime::AdvanceTime(false, DesiredDeltaMove, CurrentPosition, GetPlayLength());

			// Verify position assumptions
			ensureMsgf(bPlayingBackwards ? (CurrentPosition <= PreviousPosition) : (CurrentPosition >= PreviousPosition), TEXT("in Animation %s(Skeleton %s) : bPlayingBackwards(%d), PreviousPosition(%0.2f), Current Position(%0.2f)"),
				*GetName(), *GetNameSafe(GetSkeleton()), bPlayingBackwards, PreviousPosition, CurrentPosition);

			RootMotionParams.Accumulate(ExtractRootMotionFromRange(PreviousPosition, CurrentPosition));

			// If we've hit the end of the animation, and we're allowed to loop, keep going.
			if ((AdvanceType == ETAA_Finished) && bAllowLooping)
			{
				const float ActualDeltaMove = (CurrentPosition - PreviousPosition);
				DesiredDeltaMove -= ActualDeltaMove;

				PreviousPosition = bPlayingBackwards ? GetPlayLength() : 0.f;
				CurrentPosition = PreviousPosition;
			}
			else
			{
				break;
			}
		} while (true);
	}

	return RootMotionParams.GetRootMotionTransform();
}

FTransform UAnimSequence::ExtractRootMotionFromRange(float StartTrackPosition, float EndTrackPosition) const
{
	const FVector DefaultScale(1.f);

	FTransform InitialTransform = ExtractRootTrackTransform(0.f, NULL);
	FTransform StartTransform = ExtractRootTrackTransform(StartTrackPosition, NULL);
	FTransform EndTransform = ExtractRootTrackTransform(EndTrackPosition, NULL);

	// Use old calculation if needed.
	if (bUseNormalizedRootMotionScale)
	{
		//Clear scale as it will muck up GetRelativeTransform
		StartTransform.SetScale3D(FVector(1.f));
		EndTransform.SetScale3D(FVector(1.f));
	}
	else
	{
		if (IsValidAdditive())
		{
			StartTransform.SetScale3D(StartTransform.GetScale3D() + DefaultScale);
			EndTransform.SetScale3D(EndTransform.GetScale3D() + DefaultScale);
		}
	}

	// Transform to Component Space Rotation (inverse root transform from first frame)
	StartTransform = StartTransform.GetRelativeTransform(InitialTransform);
    EndTransform = EndTransform.GetRelativeTransform(InitialTransform);

	return EndTransform.GetRelativeTransform(StartTransform);
}

#if WITH_EDITOR
TArray<const UAnimSequence*> CurrentBakingAnims;
#endif

#define DEBUG_POSE_OUTPUT 0

#if DEBUG_POSE_OUTPUT
void DebugPrintBone(const FCompactPose& OutPose, const FCompactPoseBoneIndex& BoneIndex, int32 OutIndent)
{
	for (int i = 0; i < OutIndent; ++i)
	{
		FPlatformMisc::LowLevelOutputDebugString(TEXT("  "));
	}
	const FBoneContainer& Cont = OutPose.GetBoneContainer();

	FName BoneName = Cont.GetReferenceSkeleton().GetBoneName(Cont.MakeMeshPoseIndex(BoneIndex).GetInt());

	FVector T = OutPose[BoneIndex].GetTranslation();

	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("%s - (%.2f, %.2f,%.2f)\n"), *BoneName.ToString(), T.X, T.Y, T.Z);
}
#endif

void UAnimSequence::GetAnimationPose(FAnimationPoseData& OutAnimationPoseData, const FAnimExtractContext& ExtractionContext) const
{
	SCOPE_CYCLE_COUNTER(STAT_GetAnimationPose);

	const FCompactPose& OutPose = OutAnimationPoseData.GetPose();

	// @todo anim: if compressed and baked in the future, we don't have to do this 
	if (UseRawDataForPoseExtraction(OutPose.GetBoneContainer()) && IsValidAdditive())
	{
		if (AdditiveAnimType == AAT_LocalSpaceBase)
		{
			GetBonePose_Additive(OutAnimationPoseData, ExtractionContext);
		}
		else if (AdditiveAnimType == AAT_RotationOffsetMeshSpace)
		{
			GetBonePose_AdditiveMeshRotationOnly(OutAnimationPoseData, ExtractionContext);
		}
	}
	else
	{
		GetBonePose(OutAnimationPoseData, ExtractionContext);
	}

	// Check that all bone atoms coming from animation are normalized
#if DO_CHECK && WITH_EDITORONLY_DATA
	check(OutPose.IsNormalized());
#endif

#if DEBUG_POSE_OUTPUT
	TArray<TArray<int32>> ParentLevel;
	ParentLevel.Reserve(64);
	for (int32 i = 0; i < 64; ++i)
	{
		ParentLevel.Add(TArray<int32>());
	}
	ParentLevel[0].Add(0);

	FPlatformMisc::LowLevelOutputDebugString(TEXT("\nGetAnimationPose\n"));
	
	DebugPrintBone(OutPose, FCompactPoseBoneIndex(0), 0);
	for (FCompactPoseBoneIndex BoneIndex(1); BoneIndex < OutPose.GetNumBones(); ++BoneIndex)
	{
		FCompactPoseBoneIndex ParentIndex = OutPose.GetBoneContainer().GetParentBoneIndex(BoneIndex);
		int32 Indent = 0;
		for (; Indent < ParentLevel.Num(); ++Indent)
		{
			if (ParentLevel[Indent].Contains(ParentIndex.GetInt()))
			{
				break;
			}
		}
		Indent += 1;
		check(Indent < 64);
		ParentLevel[Indent].Add(BoneIndex.GetInt());

		DebugPrintBone(OutPose, BoneIndex, Indent);
	}
#endif
}

void UAnimSequence::GetBonePose(FCompactPose& OutPose, FBlendedCurve& OutCurve, const FAnimExtractContext& ExtractionContext, bool bForceUseRawData) const
{
	FStackCustomAttributes TempAttributes;
	FAnimationPoseData OutAnimationPoseData(OutPose, OutCurve, TempAttributes);
	GetBonePose(OutAnimationPoseData, ExtractionContext, bForceUseRawData);
}

void UAnimSequence::GetBonePose(FAnimationPoseData& OutAnimationPoseData, const FAnimExtractContext& ExtractionContext, bool bForceUseRawData /*= false*/) const
{
	SCOPE_CYCLE_COUNTER(STAT_AnimSeq_GetBonePose);
	CSV_SCOPED_TIMING_STAT(Animation, AnimSeq_GetBonePose);

	FCompactPose& OutPose = OutAnimationPoseData.GetPose();

	const FBoneContainer& RequiredBones = OutPose.GetBoneContainer();
	const bool bUseRawDataForPoseExtraction = bForceUseRawData || UseRawDataForPoseExtraction(RequiredBones);

	const bool bIsBakedAdditive = !bUseRawDataForPoseExtraction && IsValidAdditive();

	const USkeleton* MySkeleton = GetSkeleton();
	if (!MySkeleton)
	{
		if (bIsBakedAdditive)
		{
			OutPose.ResetToAdditiveIdentity(); 
		}
		else
		{
			OutPose.ResetToRefPose();
		}
		return;
	}

	const bool bDisableRetargeting = RequiredBones.GetDisableRetargeting();

	// initialize with ref-pose
	if (bIsBakedAdditive)
	{
		//When using baked additive ref pose is identity
		OutPose.ResetToAdditiveIdentity();
	}
	else
	{
		// if retargeting is disabled, we initialize pose with 'Retargeting Source' ref pose.
		if (bDisableRetargeting)
		{
			TArray<FTransform> const& AuthoredOnRefSkeleton = GetRetargetTransforms();
			TArray<FBoneIndexType> const& RequireBonesIndexArray = RequiredBones.GetBoneIndicesArray();

			int32 const NumRequiredBones = RequireBonesIndexArray.Num();
			for (FCompactPoseBoneIndex PoseBoneIndex : OutPose.ForEachBoneIndex())
			{
				int32 const& SkeletonBoneIndex = RequiredBones.GetSkeletonIndex(PoseBoneIndex);

				// Pose bone index should always exist in Skeleton
				checkSlow(SkeletonBoneIndex != INDEX_NONE);
				OutPose[PoseBoneIndex] = AuthoredOnRefSkeleton[SkeletonBoneIndex];
			}
		}
		else
		{
			OutPose.ResetToRefPose();
		}
	}

	// extract curve data . Even if no track, it can contain curve data
	EvaluateCurveData(OutAnimationPoseData.GetCurve(), ExtractionContext.CurrentTime, bUseRawDataForPoseExtraction);

#if WITH_EDITOR
	ValidateModel();
	const int32 NumTracks = bUseRawDataForPoseExtraction ? DataModel->GetNumBoneTracks() : CompressedData.CompressedTrackToSkeletonMapTable.Num();
#else
	const int32 NumTracks = CompressedData.CompressedTrackToSkeletonMapTable.Num();
#endif 
	if (NumTracks == 0)
	{
		return;
	}

	const bool bTreatAnimAsAdditive = (IsValidAdditive() && !bUseRawDataForPoseExtraction); // Raw data is never additive
	FRootMotionReset RootMotionReset(bEnableRootMotion, RootMotionRootLock, bForceRootLock, ExtractRootTrackTransform(0.f, &RequiredBones), bTreatAnimAsAdditive);

#if WITH_EDITOR
	// this happens only with editor data
	// Slower path for disable retargeting, that's only used in editor and for debugging.
	if (bUseRawDataForPoseExtraction)
	{
		// Warning if we have invalid data
		for (int32 TrackIndex = 0; TrackIndex < NumTracks; TrackIndex++)
		{
			const FBoneAnimationTrack& BoneAnimationTrack = DataModel->GetBoneTrackByIndex(TrackIndex);
			const FRawAnimSequenceTrack& TrackToExtract = BoneAnimationTrack.InternalTrackData;

			// Bail out (with rather wacky data) if data is empty for some reason.
			if (TrackToExtract.PosKeys.Num() == 0 || TrackToExtract.RotKeys.Num() == 0)
			{
				UE_LOG(LogAnimation, Warning, TEXT("UAnimSequence::GetBoneTransform : No anim data in AnimSequence '%s' Track '%s'"), *GetPathName(), 
					*BoneAnimationTrack.Name.ToString());
			}
		}

		UE::Anim::BuildPoseFromModel(DataModel, OutPose, ExtractionContext.CurrentTime, Interpolation, GetRetargetTransformsSourceName(), GetRetargetTransforms());

		if ((ExtractionContext.bExtractRootMotion && RootMotionReset.bEnableRootMotion) || RootMotionReset.bForceRootLock)
		{
			RootMotionReset.ResetRootBoneForRootMotion(OutPose[FCompactPoseBoneIndex(0)], RequiredBones);
		}

		GetCustomAttributes(OutAnimationPoseData, ExtractionContext, true);

		return;
	}
#endif // WITH_EDITOR

	DecompressPose(OutPose, CompressedData, ExtractionContext, GetSkeleton(), GetPlayLength(), Interpolation, bIsBakedAdditive, GetRetargetTransforms(), GetRetargetTransformsSourceName(), RootMotionReset);

	GetCustomAttributes(OutAnimationPoseData, ExtractionContext, false);
}

const TArray<FRawAnimSequenceTrack>& UAnimSequence::GetRawAnimationData() const
{
#if WITH_EDITOR
	ValidateModel();
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return DataModel->GetTransientRawAnimationTracks();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#else
	static const TArray<FRawAnimSequenceTrack> Tracks;
	check(false);
	return Tracks;
#endif
}


#if WITH_EDITORONLY_DATA
const TArray<FName>& UAnimSequence::GetAnimationTrackNames() const
{
#if WITH_EDITOR
	ValidateModel();
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return DataModel->GetTransientRawAnimationTrackNames();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#else
	static const TArray<FName> Names;
	check(false);
	return Names;
#endif 
}
#endif 

#if WITH_EDITOR
void UAnimSequence::UpdateCompressedCurveName(SmartName::UID_Type CurveUID, const struct FSmartName& NewCurveName)
{
	for (FSmartName& CurveName : CompressedData.CompressedCurveNames)
	{
		if (CurveName.UID == CurveUID)
		{
			CurveName = NewCurveName;
			break;
		}
	}
}

int32 UAnimSequence::AddNewRawTrack(FName TrackName, FRawAnimSequenceTrack* TrackData)
{
	ValidateModel();

	int32 NewTrackIndex = INDEX_NONE;
	Controller->OpenBracket(LOCTEXT("AddNewRawTrack_Bracket", "Adding new Bone Animation Track"));
	{
		NewTrackIndex = Controller->AddBoneTrack(TrackName);
		if (NewTrackIndex != INDEX_NONE && TrackData)
		{
			Controller->SetBoneTrackKeys(TrackName, TrackData->PosKeys, TrackData->RotKeys, TrackData->ScaleKeys);
		}
	}
	Controller->CloseBracket();

	if (NewTrackIndex == INDEX_NONE)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("TrackName"), FText::FromName(TrackName));
		FNotificationInfo Info(FText::Format(LOCTEXT("FailedToAddTracks", "Failed to add track with name \"{TrackName}\"."), Args));

		Info.bUseLargeFont = false;
		Info.ExpireDuration = 5.0f;

		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}

	return NewTrackIndex;
}
#endif

void UAnimSequence::GetBonePose_Additive(FCompactPose& OutPose, FBlendedCurve& OutCurve, const FAnimExtractContext& ExtractionContext) const
{
	FStackCustomAttributes TempAttributes;
	FAnimationPoseData OutAnimationPoseData(OutPose, OutCurve, TempAttributes);

	GetBonePose_Additive(OutAnimationPoseData, ExtractionContext);
}

void UAnimSequence::GetBonePose_Additive(FAnimationPoseData& OutAnimationPoseData, const FAnimExtractContext& ExtractionContext) const
{
	FCompactPose& OutPose = OutAnimationPoseData.GetPose();
	FBlendedCurve& OutCurve = OutAnimationPoseData.GetCurve();
	FStackCustomAttributes& OutAttributes = OutAnimationPoseData.GetAttributes();

	if (!IsValidAdditive())
	{
		OutPose.ResetToAdditiveIdentity();
		return;
	}

	// Extract target pose
	GetBonePose(OutAnimationPoseData, ExtractionContext);

	// Extract base pose
	FCompactPose BasePose;
	FBlendedCurve BaseCurve;
	FStackCustomAttributes BaseAttributes;
	
	BasePose.SetBoneContainer(&OutPose.GetBoneContainer());
	BaseCurve.InitFrom(OutCurve);	

	FAnimationPoseData BasePoseData(BasePose, BaseCurve, BaseAttributes);

	GetAdditiveBasePose(BasePoseData, ExtractionContext);

	// Create Additive animation
	FAnimationRuntime::ConvertPoseToAdditive(OutPose, BasePose);
	OutCurve.ConvertToAdditive(BaseCurve);

	FCustomAttributesRuntime::SubtractAttributes(BaseAttributes, OutAttributes);
}

void UAnimSequence::GetAdditiveBasePose(FCompactPose& OutPose, FBlendedCurve& OutCurve, const FAnimExtractContext& ExtractionContext) const
{
	FStackCustomAttributes TempAttributes;
	FAnimationPoseData OutAnimationPoseData(OutPose, OutCurve, TempAttributes);

	GetAdditiveBasePose(OutAnimationPoseData, ExtractionContext);
}

void UAnimSequence::GetAdditiveBasePose(FAnimationPoseData& OutAnimationPoseData, const FAnimExtractContext& ExtractionContext) const
{
	switch (RefPoseType)
	{
		// use whole animation as a base pose. Need BasePoseSeq.
		case ABPT_AnimScaled:
		{
			// normalize time to fit base seq
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			const float Fraction = (SequenceLength > 0.f)? FMath::Clamp<float>(ExtractionContext.CurrentTime / SequenceLength, 0.f, 1.f) : 0.f;
			const float BasePoseTime = RefPoseSeq->SequenceLength * Fraction;
			PRAGMA_ENABLE_DEPRECATION_WARNINGS

			FAnimExtractContext BasePoseExtractionContext(ExtractionContext);
			BasePoseExtractionContext.CurrentTime = BasePoseTime;
			RefPoseSeq->GetBonePose(OutAnimationPoseData, BasePoseExtractionContext, true);
			break;
		}
		// use animation as a base pose. Need BasePoseSeq and RefFrameIndex (will clamp if outside).
		case ABPT_AnimFrame:
		{
			const float Fraction = (RefPoseSeq->GetNumberOfSampledKeys() > 0) ? FMath::Clamp<float>((float)RefFrameIndex / (float)RefPoseSeq->GetNumberOfSampledKeys(), 0.f, 1.f) : 0.f;
			const float BasePoseTime = RefPoseSeq->GetPlayLength() * Fraction;
			FAnimExtractContext BasePoseExtractionContext(ExtractionContext);
			BasePoseExtractionContext.CurrentTime = BasePoseTime;
			RefPoseSeq->GetBonePose(OutAnimationPoseData, BasePoseExtractionContext, true);
			break;
		}
		// use ref pose of Skeleton as base
		case ABPT_RefPose:
		default:
			OutAnimationPoseData.GetPose().ResetToRefPose();
			break;
	}
}

void UAnimSequence::GetBonePose_AdditiveMeshRotationOnly(FCompactPose& OutPose, FBlendedCurve& OutCurve, const FAnimExtractContext& ExtractionContext) const
{
	FStackCustomAttributes TempAttributes;
	FAnimationPoseData OutAnimationPoseData(OutPose, OutCurve, TempAttributes);

	GetBonePose_AdditiveMeshRotationOnly(OutAnimationPoseData, ExtractionContext);
}

void UAnimSequence::GetBonePose_AdditiveMeshRotationOnly(FAnimationPoseData& OutAnimationPoseData, const FAnimExtractContext& ExtractionContext) const
{
	FCompactPose& OutPose = OutAnimationPoseData.GetPose();
	FBlendedCurve& OutCurve = OutAnimationPoseData.GetCurve();
	FStackCustomAttributes& OutAttributes = OutAnimationPoseData.GetAttributes();

	if (!IsValidAdditive())
	{
		// since this is additive, need to initialize to identity
		OutPose.ResetToAdditiveIdentity();
		return;
	}

	// Get target pose
	GetBonePose(OutAnimationPoseData, ExtractionContext, true);

	// get base pose
	FCompactPose BasePose;
	FBlendedCurve BaseCurve;
	FStackCustomAttributes BaseAttributes;

	FAnimationPoseData BasePoseData(BasePose, BaseCurve, BaseAttributes);

	BasePose.SetBoneContainer(&OutPose.GetBoneContainer());
	BaseCurve.InitFrom(OutCurve);

	GetAdditiveBasePose(BasePoseData, ExtractionContext);

	// Convert them to mesh rotation.
	FAnimationRuntime::ConvertPoseToMeshRotation(OutPose);
	FAnimationRuntime::ConvertPoseToMeshRotation(BasePose);

	// Turn into Additive
	FAnimationRuntime::ConvertPoseToAdditive(OutPose, BasePose);
	OutCurve.ConvertToAdditive(BaseCurve);

	FCustomAttributesRuntime::SubtractAttributes(BaseAttributes, OutAttributes);
}

#if WITH_EDITORONLY_DATA
void UAnimSequence::UpdateRetargetSourceAsset()
{
	USkeletalMesh* SourceReferenceMesh = RetargetSourceAsset.LoadSynchronous();
	const USkeleton* MySkeleton = GetSkeleton();
	if (SourceReferenceMesh && MySkeleton)
	{
		FAnimationRuntime::MakeSkeletonRefPoseFromMesh(SourceReferenceMesh, MySkeleton, RetargetSourceAssetReferencePose);
	}
	else
	{
		RetargetSourceAssetReferencePose.Empty();
	}
}
#endif // WITH_EDITORONLY_DATA

const TArray<FTransform>& UAnimSequence::GetRetargetTransforms() const
{
	if (RetargetSource.IsNone() && RetargetSourceAssetReferencePose.Num() > 0)
	{
		return RetargetSourceAssetReferencePose;
	}
	else
	{
		const USkeleton* MySkeleton = GetSkeleton();
		if (MySkeleton)
		{
			return MySkeleton->GetRefLocalPoses(RetargetSource);
		}
		else
		{
			static TArray<FTransform> EmptyTransformArray;
			return EmptyTransformArray;
		}
	}
}

FName UAnimSequence::GetRetargetTransformsSourceName() const
{
	if (RetargetSource.IsNone() && RetargetSourceAssetReferencePose.Num() > 0)
	{
		return GetOutermost()->GetFName();
	}
	else
	{
		return RetargetSource;
	}
}

const TArray<FTrackToSkeletonMap>& UAnimSequence::GetRawTrackToSkeletonMapTable() const
{
#if WITH_EDITOR
	ValidateModel();
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return DataModel->GetTransientRawAnimationTrackSkeletonMappings();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif

	static const TArray<FTrackToSkeletonMap> TempMap;
	check(false);
	return TempMap;
}

const FRawAnimSequenceTrack& UAnimSequence::GetRawAnimationTrack(int32 TrackIndex) const
{
#if WITH_EDITOR
	ValidateModel();
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return DataModel->GetBoneTrackByIndex(TrackIndex).InternalTrackData;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS 
#endif
	static const FRawAnimSequenceTrack TempTrack;
	check(false);
	return TempTrack;
}

FRawAnimSequenceTrack& UAnimSequence::GetRawAnimationTrack(int32 TrackIndex)
{
#if WITH_EDITOR
	ValidateModel();
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return DataModel->GetNonConstRawAnimationTrackByIndex(TrackIndex);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
		static FRawAnimSequenceTrack TempTrack;
	check(false);
	return TempTrack;
}

void UAnimSequence::RetargetBoneTransform(FTransform& BoneTransform, const int32 SkeletonBoneIndex, const FCompactPoseBoneIndex& BoneIndex, const FBoneContainer& RequiredBones, const bool bIsBakedAdditive) const
{
	FAnimationRuntime::RetargetBoneTransform(GetSkeleton(), GetRetargetTransformsSourceName(), GetRetargetTransforms(), BoneTransform, SkeletonBoneIndex, BoneIndex, RequiredBones, bIsBakedAdditive);
}

#if WITH_EDITOR
bool UAnimSequence::InsertFramesToRawAnimData( int32 StartFrame, int32 EndFrame, int32 CopyFrame)
{
	ValidateModel();

	// make sure the copyframe is valid and start frame is valid
	const int32 NumFramesToInsert = EndFrame-StartFrame;
	if ((CopyFrame>=0 && CopyFrame< DataModel->GetNumberOfKeys()) && (StartFrame >= 0 && StartFrame <= DataModel->GetNumberOfKeys()) && NumFramesToInsert > 0)
	{
		UAnimDataController::FScopedBracket ScopedBracket(Controller, LOCTEXT("InsertFramesToRawAnimData", "Inserting Frames into Animation Track Data"));
		UE::Anim::AnimationData::DuplicateKeys(this, StartFrame, NumFramesToInsert, CopyFrame);

		UE_LOG(LogAnimation, Log, TEXT("\tPlay Length: %f, Number of Keys: %d"), GetPlayLength(), DataModel->GetNumberOfKeys());

		return true;
	}

	return false;
}

bool UAnimSequence::CropRawAnimData( float CurrentTime, bool bFromStart )
{
	// Save Total Number of Frames before crop
	const int32 TotalNumOfKeys = GetDataModel()->GetNumberOfKeys();
	// if current frame is 1, do not try crop. There is nothing to crop
	if (TotalNumOfKeys <= 1 )
	{
		return false;
	}
	
	// If you're end or beginning, you can't cut all nor nothing. 
	// Avoiding ambiguous situation what exactly we would like to cut 
	// Below it clamps range to 1, TotalNumOfFrames-1
	// causing if you were in below position, it will still crop 1 frame. 
	// To be clearer, it seems better if we reject those inputs. 
	// If you're a bit before/after, we assume that you'd like to crop
	if ( CurrentTime == 0.f || CurrentTime == GetPlayLength() )
	{
		return false;
	}

	const FFrameTime FrameTime = DataModel->GetFrameRate().AsFrameTime(CurrentTime);
	const FFrameNumber FrameNumber = FrameTime.CeilToFrame();
	// determine which keys need to be removed.
	const int32 StartKey = bFromStart ? 0 : FrameNumber.Value;
	const int32 NumKeysToRemove = bFromStart ? FrameNumber.Value : TotalNumOfKeys - (StartKey + 1);

	// Recalculate number of keys and frames
	const int32 NewNumKeys = TotalNumOfKeys - NumKeysToRemove;
	const int32 NewNumFrames = NewNumKeys - 1;

	UE_LOG(LogAnimation, Log, TEXT("UAnimSequence::CropRawAnimData %s - CurrentTime: %f, bFromStart: %d, TotalNumOfKeys: %d, KeyIndex: %d, StartKey: %d, NumKeys: %d"), *GetName(), CurrentTime, bFromStart, TotalNumOfKeys, FrameNumber.Value, StartKey, NumKeysToRemove);

	ValidateModel();

	Controller->OpenBracket(LOCTEXT("CropRawAnimation_Bracket", "Cropping Animation Track Data"));
	UE::Anim::AnimationData::RemoveKeys(this, StartKey, NumKeysToRemove);
	Controller->CloseBracket();

	UE_LOG(LogAnimation, Log, TEXT("\tPlay Length: %f, Number of Keys: %d"), GetPlayLength(), GetDataModel()->GetNumberOfKeys());

	MarkPackageDirty();

	return true;
}

void UAnimSequence::FlipRotationWForNonRoot(USkeletalMesh* SkelMesh)
{
	if (!GetSkeleton())
	{
		return;
	}

	ValidateModel();
	Controller->OpenBracket(LOCTEXT("FlipRotationWForNonRoot_Bracket", "Flipping rotation W component for non-root tracks"));
	{
		for (const FBoneAnimationTrack& Track : DataModel->GetBoneAnimationTracks())
		{
			// Only apply to non-root bones
			if (Track.BoneTreeIndex > 0)
			{
				TArray<FQuat> NewRotationalKeys = Track.InternalTrackData.RotKeys;
				for (FQuat& RotationalKey : NewRotationalKeys)
				{
					RotationalKey.W *= -1.f;
				}

				Controller->SetBoneTrackKeys(Track.Name, Track.InternalTrackData.PosKeys, NewRotationalKeys, Track.InternalTrackData.ScaleKeys);
			}
		}
	}
	Controller->CloseBracket();
}
#endif 

#if WITH_EDITOR
bool UAnimSequence::ShouldPerformStripping(const bool bPerformFrameStripping, const bool bPerformStrippingOnOddFramedAnims) const
{
	const bool bShouldPerformFrameStripping = bPerformFrameStripping && bAllowFrameStripping;

	// Can only do stripping on animations that have an even number of frames once the end frame is removed)
	const bool bIsEvenFramed = ((NumberOfSampledKeys - 1) % 2) == 0;
	const bool bIsValidForStripping = bIsEvenFramed || bPerformStrippingOnOddFramedAnims;

	const bool bStripCandidate = (NumberOfSampledKeys > 10) && bIsValidForStripping;

	return bStripCandidate && bShouldPerformFrameStripping;
}

FString UAnimSequence::GetDDCCacheKeySuffix(const bool bPerformStripping) const
{
	return GetAnimSequenceSpecificCacheKeySuffix(*this, bPerformStripping, CompressionErrorThresholdScale);
}
#endif

void UAnimSequence::WaitOnExistingCompression(const bool bWantResults)
{
#if WITH_EDITOR
	check(IsInGameThread());
	if (bCompressionInProgress)
	{
		COOK_STAT(auto Timer = AnimSequenceCookStats::UsageStats.TimeAsyncWait());
		FAsyncCompressedAnimationsManagement::Get().WaitOnExistingCompression(this, bWantResults);
		bCompressionInProgress = false;
		COOK_STAT(Timer.TrackCyclesOnly()); // Need to get hit/miss and size from WaitOnExistingCompression!
	}
#endif
}

void UAnimSequence::RequestAnimCompression(FRequestAnimCompressionParams Params)
{
#if WITH_EDITOR
	check(IsInGameThread());
	USkeleton* CurrentSkeleton = GetSkeleton();
	if (CurrentSkeleton == nullptr)
	{
		bUseRawDataOnly = true;
		return;
	}

	if (FPlatformProperties::RequiresCookedData() || bBlockCompressionRequests)
	{
		return;
	}

	WaitOnExistingCompression(false);

	if (BoneCompressionSettings == nullptr || !BoneCompressionSettings->AreSettingsValid())
	{
		BoneCompressionSettings = FAnimationUtils::GetDefaultAnimationBoneCompressionSettings();
	}

	if (CurveCompressionSettings == nullptr || !CurveCompressionSettings->AreSettingsValid())
	{
		CurveCompressionSettings = FAnimationUtils::GetDefaultAnimationCurveCompressionSettings();
	}

	// Make sure all our required dependencies are loaded
	FAnimationUtils::EnsureAnimSequenceLoaded(*this);

	EnsureValidRawDataGuid();

	bUseRawDataOnly = true;

	check(!bCompressionInProgress);
	bCompressionInProgress = true;

	Params.CompressContext->GatherPreCompressionStats(GetName(), GetApproxRawSize(), GetApproxCompressedSize());

	const double CompressionStartTime = FPlatformTime::Seconds();

	const bool bPerformStripping = ShouldPerformStripping(Params.bPerformFrameStripping, Params.bPerformFrameStrippingOnOddNumberedFrames);
	const FString AssetDDCKey = GetDDCCacheKeySuffix(bPerformStripping);

	bool bCompressedDataFromDDC = false;

	TArray<uint8> OutData;
	{
		COOK_STAT(auto Timer = AnimSequenceCookStats::UsageStats.TimeSyncWork());

		FDerivedDataAnimationCompression* AnimCompressor = new FDerivedDataAnimationCompression(TEXT("AnimSeq"), AssetDDCKey, Params.CompressContext);

		const FString FinalDDCKey = FDerivedDataCacheInterface::BuildCacheKey(AnimCompressor->GetPluginName(), AnimCompressor->GetVersionString(), *AnimCompressor->GetPluginSpecificCacheKeySuffix());

		// For debugging DDC/Compression issues		
		const bool bSkipDDC = false;

		if (!bSkipDDC && GetDerivedDataCacheRef().GetSynchronous(*FinalDDCKey, OutData, AnimCompressor->GetDebugContextString()))
		{
			COOK_STAT(Timer.AddHit(OutData.Num()));
			bCompressedDataFromDDC = true;
		}
		else
		{
			// Data does not exist, need to build it.
			FCompressibleAnimRef CompressibleData = MakeShared<FCompressibleAnimData, ESPMode::ThreadSafe>(this, bPerformStripping);
			AnimCompressor->SetCompressibleData(CompressibleData);

			if (bSkipDDC || (CompressCommandletVersion == INDEX_NONE))
			{
				AnimCompressor->Build(OutData);
				COOK_STAT(Timer.AddMiss(OutData.Num()));
			}
			else if (AnimCompressor->CanBuild())
			{
				if (Params.bAsyncCompression)
				{
					FAsyncCompressedAnimationsManagement::Get().RequestAsyncCompression(*AnimCompressor, this, bPerformStripping, OutData);
					COOK_STAT(Timer.TrackCyclesOnly());
				}
				else
				{
					bool bBuilt = false;
					const bool bSuccess = GetDerivedDataCacheRef().GetSynchronous(AnimCompressor, OutData, &bBuilt);
					COOK_STAT(Timer.AddHitOrMiss(!bSuccess || bBuilt ? FCookStats::CallStats::EHitOrMiss::Miss : FCookStats::CallStats::EHitOrMiss::Hit, OutData.Num()));
				}
				AnimCompressor = nullptr;
			}
			else
			{
				COOK_STAT(Timer.TrackCyclesOnly());
			}
		}

		if (AnimCompressor)
		{
			// Would really like to do auto mem management but GetDerivedDataCacheRef().GetSynchronous expects a pointer it can delete
			delete AnimCompressor;
			AnimCompressor = nullptr;
		}
	}

	if (OutData.Num() > 0) // Haven't async compressed
	{
		ApplyCompressedData(OutData);

		if(bCompressedDataFromDDC)
		{
			const double CompressionEndTime = FPlatformTime::Seconds();
			const double CompressionTime = CompressionEndTime - CompressionStartTime;

			TArray<FBoneData> BoneData;
			FAnimationUtils::BuildSkeletonMetaData(GetSkeleton(), BoneData);
			Params.CompressContext->GatherPostCompressionStats(CompressedData, BoneData, GetFName(), CompressionTime, false);
		}

	}
#endif
}

#if WITH_EDITOR
void UAnimSequence::ApplyCompressedData(const FString& DataCacheKeySuffix, const bool bPerformFrameStripping, const TArray<uint8>& Data)
{
	if (GetDDCCacheKeySuffix(bPerformFrameStripping) == DataCacheKeySuffix)
	{
		ApplyCompressedData(Data);
	}
	else
	{
		bCompressionInProgress = false;
	}
}
#endif

void UAnimSequence::ApplyCompressedData(const TArray<uint8>& Data)
{
#if WITH_EDITOR
	bCompressionInProgress = false;
	
	SynchronousCustomAttributesCompression();
#endif
	if(Data.Num() > 0)
	{
		FMemoryReader MemAr(Data);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		SerializeCompressedData(MemAr, true);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		//This is only safe during sync anim compression
		SetSkeletonVirtualBoneGuid(GetSkeleton()->GetVirtualBoneGuid());
		bUseRawDataOnly = false;
	}
}

void UAnimSequence::SerializeCompressedData(FArchive& Ar, bool bDDCData)
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		CompressedData.SerializeCompressedData(Ar, bDDCData, this, this->GetSkeleton(), BoneCompressionSettings, CurveCompressionSettings);
		}
	}

int32 UAnimSequence::GetSkeletonIndexFromRawDataTrackIndex(const int32 TrackIndex) const
{
#if WITH_EDITOR
	ValidateModel();
	return DataModel->GetBoneTrackByIndex(TrackIndex).BoneTreeIndex;
#else
	check(false);
	return INDEX_NONE;
#endif
}

#if WITH_EDITOR

bool UAnimSequence::CanBakeAdditive() const
{
	return	(NumberOfSampledKeys > 0) &&
			IsValidAdditive() &&
			GetSkeleton();
}

bool UAnimSequence::DoesSequenceContainZeroScale() const
{
	ValidateModel();

	const TArray<FBoneAnimationTrack>& BoneAnimationTracks = DataModel->GetBoneAnimationTracks();
	for (const FBoneAnimationTrack& Track : BoneAnimationTracks)
	{
		for (const FVector& ScaleKey : Track.InternalTrackData.ScaleKeys)
		{
			if (ScaleKey.IsZero())
			{
				return true;
			}
		}
	}

	return false;
}

FGuid UAnimSequence::GenerateGuidFromRawData() const
{
	ValidateModel();
	return DataModel->GenerateGuid();

}

void CopyTransformToRawAnimationData(const FTransform& BoneTransform, FRawAnimSequenceTrack& Track, int32 Frame)
{
	Track.PosKeys[Frame] = BoneTransform.GetTranslation();
	Track.RotKeys[Frame] = BoneTransform.GetRotation();
	Track.RotKeys[Frame].Normalize();
	Track.ScaleKeys[Frame] = BoneTransform.GetScale3D();
}

struct FByFramePoseEvalContext
{
public:
	FBoneContainer RequiredBones;

	// Length of one frame.
	const float IntervalTime;

	TArray<FBoneIndexType> RequiredBoneIndexArray;

	FByFramePoseEvalContext(const UAnimSequence* InAnimToEval)
		: FByFramePoseEvalContext(InAnimToEval->GetPlayLength(), InAnimToEval->GetNumberOfSampledKeys(), InAnimToEval->GetSkeleton())
	{}
		
	FByFramePoseEvalContext(float InSequenceLength, int32 InRawNumOfFrames, USkeleton* InSkeleton)
		: IntervalTime(InSequenceLength / ((float)FMath::Max(InRawNumOfFrames - 1, 1)))
	{
		// Initialize RequiredBones for pose evaluation
		RequiredBones.SetUseRAWData(true);

		check(InSkeleton);

		RequiredBoneIndexArray.AddUninitialized(InSkeleton->GetReferenceSkeleton().GetNum());
		for (int32 BoneIndex = 0; BoneIndex < RequiredBoneIndexArray.Num(); ++BoneIndex)
		{
			RequiredBoneIndexArray[BoneIndex] = BoneIndex;
		}

		RequiredBones.InitializeTo(RequiredBoneIndexArray, FCurveEvaluationOption(true), *InSkeleton);
	}

};

void UAnimSequence::BakeOutVirtualBoneTracks(TArray<FRawAnimSequenceTrack>& NewRawTracks, TArray<FName>& NewAnimationTrackNames, TArray<FTrackToSkeletonMap>& NewTrackToSkeletonMapTable)
{
	ValidateModel();

	const int32 NumVirtualBonesOnSkeleton = GetSkeleton()->GetVirtualBones().Num();

	TArray<int> VirtualBonesToAdd;
	VirtualBonesToAdd.Reserve(NumVirtualBonesOnSkeleton);

	TArray<int32> SourceParents;

	const FReferenceSkeleton& RefSkeleton = GetSkeleton()->GetReferenceSkeleton();
	
	const TArray<FBoneAnimationTrack>& BoneAnimationTracks = ResampledAnimationTrackData;

	// Presize outgoing arrays
	const int32 NumAnimationTracks = DataModel->GetNumBoneTracks();
	NewRawTracks.Reset(NumAnimationTracks);
	NewAnimationTrackNames.Reset(NumAnimationTracks);
	NewTrackToSkeletonMapTable.Reset(NumAnimationTracks);

	for (const FBoneAnimationTrack& Track : BoneAnimationTracks)
	{
		FRawAnimSequenceTrack& RawTrack = NewRawTracks.Add_GetRef(Track.InternalTrackData);
		NewAnimationTrackNames.Add(Track.Name);
		NewTrackToSkeletonMapTable.Add(Track.BoneTreeIndex);

		UE::Anim::Compression::SanitizeRawAnimSequenceTrack(RawTrack);
	}

	const TArray<FVirtualBone>& VirtualBones = GetSkeleton()->GetVirtualBones();
	for (int32 VBIndex = 0; VBIndex < NumVirtualBonesOnSkeleton; ++VBIndex)
	{
		const FVirtualBone& VirtualBone = VirtualBones[VBIndex];
		if (!NewAnimationTrackNames.Contains(VirtualBone.VirtualBoneName))
		{
			//Need to test if we will animation virtual bone. This involves seeing if any bone that can affect the position
			//of the target relative to the source is animated by this animation. A bone that can affect the relative position
			//is any both that is a child of the common ancestor of the target and source

			SourceParents.Reset();
			bool bBuildVirtualBone = false;

			// First get all the bones that form the chain to the source bone. 
			int32 CurrentBone = RefSkeleton.FindBoneIndex(VirtualBone.SourceBoneName);
			while (CurrentBone != INDEX_NONE)
			{
				SourceParents.Add(CurrentBone);
				CurrentBone = RefSkeleton.GetParentIndex(CurrentBone);
			}

			// Now start checking every bone in the target bones hierarchy until a common ancestor is reached. 
			CurrentBone = RefSkeleton.FindBoneIndex(VirtualBone.TargetBoneName);

			while (!SourceParents.Contains(CurrentBone))
			{
				if (Algo::FindBy(NewTrackToSkeletonMapTable, CurrentBone, &FTrackToSkeletonMap::BoneTreeIndex) != nullptr)
				{
					//We animate this bone so the virtual bone is needed
					bBuildVirtualBone = true;
					break;
				}

				CurrentBone = RefSkeleton.GetParentIndex(CurrentBone);
				check(CurrentBone != INDEX_NONE);
			}

			// Now we have all the non common bones from the target chain we need the same check from the source chain
			const int32 FirstCommon = SourceParents.IndexOfByKey(CurrentBone);
			for (int32 i = FirstCommon - 1; i >= 0; --i)
			{
				if (Algo::FindBy(NewTrackToSkeletonMapTable, i, &FTrackToSkeletonMap::BoneTreeIndex) != nullptr)
				{
					//We animate this bone so the virtual bone is needed
					bBuildVirtualBone = true;
					break;
				}
			}

			if (bBuildVirtualBone)
			{
				VirtualBonesToAdd.Add(VBIndex);
			}
		}
	}

	const int32 NumVirtualBonesToAdd = VirtualBonesToAdd.Num();

	const USkeleton* TargetSkeleton = GetSkeleton();

	const int32 VirtualBoneStartIndex = NewRawTracks.Num();

	const int32 NumTrackKeys = NumberOfSampledKeys;

	// Generate track data for virtual bones
	for (int32 VBIndex : VirtualBonesToAdd)
	{
		const FVirtualBone& VirtualBone = VirtualBones[VBIndex];

		FRawAnimSequenceTrack& RawTrack = NewRawTracks.AddDefaulted_GetRef();

		//Init new tracks
		RawTrack.PosKeys.SetNumUninitialized(NumTrackKeys);
		RawTrack.RotKeys.SetNumUninitialized(NumTrackKeys);
		RawTrack.ScaleKeys.SetNumUninitialized(NumTrackKeys);

		NewAnimationTrackNames.Add(VirtualBone.VirtualBoneName);
		const int32 VirtualBoneSkeletonIndex = TargetSkeleton->GetReferenceSkeleton().GetRequiredVirtualBones()[VBIndex];
		NewTrackToSkeletonMapTable.Add(VirtualBoneSkeletonIndex);
	}

	// Setup data for evaluating bone transforms
	FMemMark Mark(FMemStack::Get());
	FByFramePoseEvalContext EvalContext(this);

	//Pose evaluation data
	FCompactPose Pose;
	Pose.SetBoneContainer(&EvalContext.RequiredBones);

	FAnimExtractContext ExtractContext;
	const TArray<FVirtualBoneRefData>& VBRefData = GetSkeleton()->GetReferenceSkeleton().GetVirtualBoneRefData();
	for (int32 Frame = 0; Frame < NumTrackKeys; ++Frame)
	{
		// Initialise curve data from Skeleton
		FBlendedCurve Curve;
		Curve.InitFrom(EvalContext.RequiredBones);

		//Grab pose for this frame
		const float CurrentFrameTime = Frame * EvalContext.IntervalTime;
		ExtractContext.CurrentTime = CurrentFrameTime;

		FStackCustomAttributes TempAttributes;
		FAnimationPoseData AnimPoseData(Pose, Curve, TempAttributes);
		GetAnimationPose(AnimPoseData, ExtractContext);

		// Copy the retrieved frame data into the virtual bone track keys
		for (int32 VBToAddIndex = 0; VBToAddIndex < VirtualBonesToAdd.Num(); ++VBToAddIndex)
		{
			const int32 VBIndex = VirtualBonesToAdd[VBToAddIndex];
			const FVirtualBoneRefData& VB = VBRefData[VBIndex];
			CopyTransformToRawAnimationData(Pose[FCompactPoseBoneIndex(VB.VBRefSkelIndex)], NewRawTracks[VBToAddIndex + VirtualBoneStartIndex], Frame);
			UE::Anim::Compression::SanitizeRawAnimSequenceTrack(NewRawTracks[VBToAddIndex + VirtualBoneStartIndex]);
		}
	}
}

bool IsIdentity(const FVector& Pos)
{
	return Pos.Equals(FVector::ZeroVector);
}

bool IsIdentity(const FQuat& Rot)
{
	return Rot.Equals(FQuat::Identity);
}

template<class KeyType>
bool IsKeyArrayValidForRemoval(const TArray<KeyType>& Keys)
{
	return Keys.Num() == 0 || (Keys.Num() == 1 && IsIdentity(Keys[0]));
}

bool IsRawTrackValidForRemoval(const FRawAnimSequenceTrack& Track)
{
	return	IsKeyArrayValidForRemoval(Track.PosKeys) &&
			IsKeyArrayValidForRemoval(Track.RotKeys) &&
			IsKeyArrayValidForRemoval(Track.ScaleKeys);
}

void UAnimSequence::TestEvalauteAnimation() const
{
	FMemMark Mark(FMemStack::Get());
	FByFramePoseEvalContext EvalContext(this);
	EvalContext.RequiredBones.SetUseRAWData(false);

	FCompactPose Pose;
	Pose.SetBoneContainer(&EvalContext.RequiredBones);

	FAnimExtractContext ExtractContext;

	for (int32 Frame = 0; Frame < NumberOfSampledKeys; ++Frame)
	{
		// Initialise curve data from Skeleton
		FBlendedCurve Curve;
		Curve.InitFrom(EvalContext.RequiredBones);

		//Grab pose for this frame
		const float CurrentFrameTime = Frame * EvalContext.IntervalTime;
		ExtractContext.CurrentTime = CurrentFrameTime;

		FStackCustomAttributes TempAttributes;
		FAnimationPoseData AnimPoseData(Pose, Curve, TempAttributes);
		GetAnimationPose(AnimPoseData, ExtractContext);
	}
}

FFloatCurve* GetFloatCurve(TArray<FFloatCurve>& FloatCurves, const USkeleton::AnimCurveUID& CurveUID)
{
	for (FFloatCurve& Curve : FloatCurves)
	{
		if (Curve.Name.UID == CurveUID)
		{
			return &Curve;
		}
	}

	return nullptr;
}

FFloatCurve* FindOrAddCurve(TArray<FFloatCurve>& FloatCurves, const FSmartName& CurveName)
{
	FFloatCurve* ReturnCurve = GetFloatCurve(FloatCurves, CurveName.UID);

	if (ReturnCurve == nullptr)
	{
		FFloatCurve& NewCurve = FloatCurves.Add_GetRef(FFloatCurve(CurveName, 0));
		ReturnCurve = &NewCurve;
	}

	return ReturnCurve;
	
}

bool IsNewKeyDifferent(const FRichCurveKey& LastKey, float NewValue)
{
	return LastKey.Value != NewValue;
}

void UAnimSequence::BakeOutAdditiveIntoRawData(TArray<FRawAnimSequenceTrack>& NewRawTracks, TArray<FName>& NewAnimationTrackNames, TArray<FTrackToSkeletonMap>& NewTrackToSkeletonMapTable, TArray<FFloatCurve>& NewFloatCurves, TArray<FRawAnimSequenceTrack>& AdditiveBaseAnimationData)
{
	if (!CanBakeAdditive())
	{
		return; // Nothing to do
	}

	USkeleton* MySkeleton = GetSkeleton();
	check(MySkeleton);

	const bool bRequiresValidRefPoseAsset = RefPoseType == EAdditiveBasePoseType::ABPT_AnimScaled || RefPoseType == EAdditiveBasePoseType::ABPT_AnimFrame;
	ensure(!bRequiresValidRefPoseAsset || (RefPoseSeq && !RefPoseSeq->HasAnyFlags(EObjectFlags::RF_NeedPostLoad)));
	
	const FSmartNameMapping* CurveMapping = MySkeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName);
	TArray<FName> SkeletonCurveNames;
	CurveMapping->FillNameArray(SkeletonCurveNames);
	for (const FFloatCurve& FloatCurve : DataModel->GetFloatCurves())
	{
		ensure(SkeletonCurveNames.Contains(FloatCurve.Name.DisplayName));
	}

	FMemMark Mark(FMemStack::Get());

	FByFramePoseEvalContext EvalContext(this);

	NewRawTracks.Reset(EvalContext.RequiredBoneIndexArray.Num());
	NewRawTracks.SetNum(EvalContext.RequiredBoneIndexArray.Num());

	for (FRawAnimSequenceTrack& RawTrack : NewRawTracks)
	{
		RawTrack.PosKeys.SetNumUninitialized(NumberOfSampledKeys);
		RawTrack.RotKeys.SetNumUninitialized(NumberOfSampledKeys);
		RawTrack.ScaleKeys.SetNumUninitialized(NumberOfSampledKeys);
	}

	// keep the same buffer size
	AdditiveBaseAnimationData = NewRawTracks;

	NewTrackToSkeletonMapTable.Reset(EvalContext.RequiredBoneIndexArray.Num());
	NewTrackToSkeletonMapTable.SetNumUninitialized(EvalContext.RequiredBoneIndexArray.Num());

	NewAnimationTrackNames.SetNumUninitialized(EvalContext.RequiredBoneIndexArray.Num());
	NewAnimationTrackNames.SetNumUninitialized(EvalContext.RequiredBoneIndexArray.Num());

	for (int32 TrackIndex = 0; TrackIndex < EvalContext.RequiredBoneIndexArray.Num(); ++TrackIndex)
	{
		NewTrackToSkeletonMapTable[TrackIndex].BoneTreeIndex = TrackIndex;
		NewAnimationTrackNames[TrackIndex] = GetSkeleton()->GetReferenceSkeleton().GetBoneName(TrackIndex);
	}

	//Pose evaluation data
	FCompactPose Pose;
	Pose.SetBoneContainer(&EvalContext.RequiredBones);
	FCompactPose BasePose;
	BasePose.SetBoneContainer(&EvalContext.RequiredBones);

	FAnimExtractContext ExtractContext;

	for (int32 Frame = 0; Frame < NumberOfSampledKeys; ++Frame)
	{
		// Initialise curve data from Skeleton
		FBlendedCurve Curve;
		Curve.InitFrom(EvalContext.RequiredBones);

		FBlendedCurve DummyBaseCurve;
		DummyBaseCurve.InitFrom(EvalContext.RequiredBones);

		//Grab pose for this frame
		const float PreviousFrameTime = (Frame - 1) * EvalContext.IntervalTime;
		const float CurrentFrameTime = Frame * EvalContext.IntervalTime;
		ExtractContext.CurrentTime = CurrentFrameTime;

		FStackCustomAttributes BaseAttributes;
		FAnimationPoseData AnimPoseData(Pose, Curve, BaseAttributes);
		GetAnimationPose(AnimPoseData, ExtractContext);

		FStackCustomAttributes AdditiveAttributes;
		FAnimationPoseData AnimBasePoseData(BasePose, DummyBaseCurve, AdditiveAttributes);
		GetAdditiveBasePose(AnimBasePoseData, ExtractContext);

		//Write out every track for this frame
		for (FCompactPoseBoneIndex TrackIndex(0); TrackIndex < NewRawTracks.Num(); ++TrackIndex)
		{
			CopyTransformToRawAnimationData(Pose[TrackIndex], NewRawTracks[TrackIndex.GetInt()], Frame);
			CopyTransformToRawAnimationData(BasePose[TrackIndex], AdditiveBaseAnimationData[TrackIndex.GetInt()], Frame);
		}

		//Write out curve data for this frame
		for (int32 CurveIndex = 0; CurveIndex < Curve.UIDToArrayIndexLUT->Num(); ++CurveIndex)
		{
			USkeleton::AnimCurveUID CurveUID = (USkeleton::AnimCurveUID) CurveIndex;
			int32 ArrayIndex = Curve.GetArrayIndexByUID(CurveUID);
			if (ArrayIndex != INDEX_NONE)
			{
				float CurveWeight = Curve.CurveWeights[ArrayIndex];
				FFloatCurve* RawCurve = GetFloatCurve(NewFloatCurves, CurveUID);
				if (!RawCurve && !FMath::IsNearlyZero(CurveWeight)) //Only make a new curve if we are going to give it data
				{
					FSmartName NewCurveName;
					// if we don't have name, there is something wrong here. 
					ensureAlways(MySkeleton->GetSmartNameByUID(USkeleton::AnimCurveMappingName, CurveUID, NewCurveName));
					// curve flags don't matter much for compressed curves
					RawCurve = FindOrAddCurve(NewFloatCurves, NewCurveName);
				}

				if (RawCurve)
				{
					const bool bHasKeys = RawCurve->FloatCurve.GetNumKeys() > 0;
					if (!bHasKeys)
					{
						//Add pre key of 0
						if (Frame > 0)
						{
							RawCurve->UpdateOrAddKey(0.f, PreviousFrameTime);
						}

					}

					if (!bHasKeys || IsNewKeyDifferent(RawCurve->FloatCurve.GetLastKey(), CurveWeight))
					{
						RawCurve->UpdateOrAddKey(CurveWeight, CurrentFrameTime);
						TArray<FRichCurveKey>& CurveKeys = RawCurve->FloatCurve.Keys;
						if (CurveKeys.Num() > 1)
						{
							FRichCurveKey& PrevKey = CurveKeys.Last(1);
							if (PrevKey.Time < (PreviousFrameTime - SMALL_NUMBER)) // Did we skip a frame, if so need to make previous key const
							{
								PrevKey.InterpMode = RCIM_Constant;
							}
						}
					}
				}
			}
		}
	}
}

void UAnimSequence::FlagDependentAnimationsAsRawDataOnly() const
{
	for (TObjectIterator<UAnimSequence> Iter; Iter; ++Iter)
	{
		UAnimSequence* Seq = *Iter;
		if (Seq->RefPoseSeq == this)
		{
			Seq->bUseRawDataOnly = true;
		}
	}
}

void UAnimSequence::UpdateDependentStreamingAnimations() const
{
	for (TObjectIterator<UAnimStreamable> Iter; Iter; ++Iter)
	{
		UAnimStreamable* Seq = *Iter;
		if (Seq->SourceSequence == this)
		{
			Seq->InitFrom(this);
		}
	}
}

#endif

void UAnimSequence::RecycleAnimSequence()
{
#if WITH_EDITOR
	ResetAnimation();
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
void UAnimSequence::CleanAnimSequenceForImport()
{
	ValidateModel();
	DeleteBoneAnimationData();
	RawDataGuid.Invalidate();
}

void UAnimSequence::ResetAnimation()
{
	// clear everything. Making new animation, so need to reset all the things that belong here
	ValidateModel();

	Controller->OpenBracket(LOCTEXT("ResetAnimation_Bracket", "Resetting Animation Sequence"));
	{
		DeleteBoneAnimationData();
		DeleteNotifyTrackData();
	}
	Controller->CloseBracket();

	Notifies.Empty();
	AuthoredSyncMarkers.Empty();
	UniqueMarkerNames.Empty();
	AnimNotifyTracks.Empty();
	RawDataGuid.Invalidate();
}

void UAnimSequence::DeleteDeprecatedRawAnimationData()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	RawAnimationData.Empty();
	AnimationTrackNames.Empty();
	TrackToSkeletonMapTable.Empty();
	SourceRawAnimationData_DEPRECATED.Empty();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void UAnimSequence::DeleteNotifyTrackData()
{
	AuthoredSyncMarkers.Empty();
	UniqueMarkerNames.Empty();
	Notifies.Empty();
	AnimNotifyTracks.Empty();

	InitializeNotifyTrack();
}

void UAnimSequence::DeleteBoneAnimationData()
{
	ValidateModel();
	Controller->ResetModel();
}
#endif // WITH_EDITOR
		

bool UAnimSequence::CopyNotifies(UAnimSequence* SourceAnimSeq, UAnimSequence* DestAnimSeq, bool bShowDialogs /*= true */)
{
#if WITH_EDITOR
	return UE::Anim::CopyNotifies(SourceAnimSeq, DestAnimSeq, bShowDialogs);
#endif // WITH_EDITOR
	return false;
}

bool UAnimSequence::IsValidAdditive() const		
{ 
	if (AdditiveAnimType != AAT_None)
	{
		switch (RefPoseType)
		{
		case ABPT_RefPose:
			return true;
		case ABPT_AnimScaled:
			return (RefPoseSeq != NULL);
		case ABPT_AnimFrame:
			return (RefPoseSeq != NULL) && (RefFrameIndex >= 0);
		default:
			return false;
		}
	}

	return false;
}

#if WITH_EDITOR

int32 FindMeshBoneIndexFromBoneName(USkeleton * Skeleton, const FName &BoneName)
{
	USkeletalMesh * PreviewMesh = Skeleton->GetPreviewMesh();
	const int32& SkeletonBoneIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);

	int32 BoneIndex = INDEX_NONE;

	if(SkeletonBoneIndex != INDEX_NONE)
	{
		BoneIndex = Skeleton->GetMeshBoneIndexFromSkeletonBoneIndex(PreviewMesh, SkeletonBoneIndex);
	}

	return BoneIndex;
}
void FillUpTransformBasedOnRig(USkeleton* Skeleton, TArray<FTransform>& NodeSpaceBases, TArray<FTransform> &Rotations, TArray<FVector>& Translations, TArray<bool>& TranslationParentFlags)
{
	TArray<FTransform> SpaceBases;
	FAnimationRuntime::FillUpComponentSpaceTransformsRetargetBasePose(Skeleton, SpaceBases);

	const URig* Rig = Skeleton->GetRig();

	if (Rig)
	{
		// this one has to collect all Nodes in Rig data
		// since we're comparing two of them together. 
		int32 NodeNum = Rig->GetNodeNum();

		if (NodeNum > 0)
		{
			NodeSpaceBases.Empty(NodeNum);
			NodeSpaceBases.AddUninitialized(NodeNum);

			Rotations.Empty(NodeNum);
			Rotations.AddUninitialized(NodeNum);

			Translations.Empty(NodeNum);
			Translations.AddUninitialized(NodeNum);

			TranslationParentFlags.Empty(Translations.Num());
			TranslationParentFlags.AddZeroed(Translations.Num());

			const USkeletalMesh* PreviewMesh = Skeleton->GetPreviewMesh();

			for (int32 Index = 0; Index < NodeNum; ++Index)
			{
				const FName NodeName = Rig->GetNodeName(Index);
				const FName& BoneName = Skeleton->GetRigBoneMapping(NodeName);
				const int32& BoneIndex = FindMeshBoneIndexFromBoneName(Skeleton, BoneName);

				if (BoneIndex == INDEX_NONE)
				{
					// add identity
					NodeSpaceBases[Index].SetIdentity();
					Rotations[Index].SetIdentity();
					Translations[Index] = FVector::ZeroVector;
				}
				else
				{
					// initialize with SpaceBases - assuming World Based
					NodeSpaceBases[Index] = SpaceBases[BoneIndex];
					Rotations[Index] = SpaceBases[BoneIndex];
					Translations[Index] = SpaceBases[BoneIndex].GetLocation();

					const FTransformBase* TransformBase = Rig->GetTransformBaseByNodeName(NodeName);

					if (TransformBase != NULL)
					{
						// orientation constraint			
						const auto& RotConstraint = TransformBase->Constraints[EControlConstraint::Type::Orientation];

						if (RotConstraint.TransformConstraints.Num() > 0)
						{
							const FName& ParentBoneName = Skeleton->GetRigBoneMapping(RotConstraint.TransformConstraints[0].ParentSpace);
							const int32& ParentBoneIndex = FindMeshBoneIndexFromBoneName(Skeleton, ParentBoneName);

							if (ParentBoneIndex != INDEX_NONE)
							{
								Rotations[Index] = SpaceBases[BoneIndex].GetRelativeTransform(SpaceBases[ParentBoneIndex]);
							}
						}

						// translation constraint
						const auto& TransConstraint = TransformBase->Constraints[EControlConstraint::Type::Translation];

						if (TransConstraint.TransformConstraints.Num() > 0)
						{
							const FName& ParentBoneName = Skeleton->GetRigBoneMapping(TransConstraint.TransformConstraints[0].ParentSpace);
							const int32& ParentBoneIndex = FindMeshBoneIndexFromBoneName(Skeleton, ParentBoneName);

							if (ParentBoneIndex != INDEX_NONE)
							{
								// I think translation has to include rotation, otherwise it won't work
								Translations[Index] = SpaceBases[BoneIndex].GetLocation() - SpaceBases[ParentBoneIndex].GetLocation();
								TranslationParentFlags[Index] = true;
							}
						}
					}
				}
			}
		}
	}
}

int32 FindValidTransformParentTrack(const URig* Rig, int32 NodeIndex, bool bTranslate, const TArray<FName>& ValidNodeNames)
{
	int32 ParentIndex = Rig->FindTransformParentNode(NodeIndex, bTranslate);

	// verify if it exists in ValidNodeNames
	if (ParentIndex != INDEX_NONE)
	{
		FName NodeName = Rig->GetNodeName(ParentIndex);

		return ValidNodeNames.Find(NodeName);
	}

	return INDEX_NONE;
}


void UAnimSequence::RemapTracksToNewSkeleton( USkeleton* NewSkeleton, bool bConvertSpaces )
{
	// this is not cheap, so make sure it only happens in editor

	ValidateModel();

	UAnimDataController::FScopedBracket ScopedBracket(Controller, LOCTEXT("RemapTrackToNewSkeleton_Bracket", "Remapping Bone Animation tracks to new Skeleton"));

	// @Todo : currently additive will work fine since we don't bake anything except when we extract
	// but in the future if we bake this can be problem
	if (bConvertSpaces)
	{
		USkeleton* OldSkeleton = GetSkeleton();

		// first check if both has same rig, if so, we'll retarget using it
		if (OldSkeleton && OldSkeleton->GetRig() != NULL && NewSkeleton->GetRig() == OldSkeleton->GetRig() && OldSkeleton->GetPreviewMesh() && NewSkeleton->GetPreviewMesh())
		{
			const URig* Rig = OldSkeleton->GetRig();

			// we'll have to save the relative space bases transform from old ref pose to new refpose
			TArray<FTransform> RelativeToNewSpaceBases;
			// save the ratio of translation change
			TArray<float> OldToNewTranslationRatio;
			// create relative transform in component space between old skeleton and new skeleton
			{
				// first calculate component space ref pose to get the relative transform between
				// two ref poses. It is very important update ref pose before getting here. 
				TArray<FTransform> NewRotations, OldRotations, NewSpaceBases, OldSpaceBases;
				TArray<FVector> NewTranslations, OldTranslations;
				TArray<bool> NewTranslationParentFlags, OldTranslationParentFlags;
				// get the spacebases transform
				FillUpTransformBasedOnRig(NewSkeleton, NewSpaceBases, NewRotations, NewTranslations, NewTranslationParentFlags);
				FillUpTransformBasedOnRig(OldSkeleton, OldSpaceBases, OldRotations, OldTranslations, OldTranslationParentFlags);

				// now we'd like to get the relative transform from old to new ref pose in component space
				// PK2*K2 = PK1*K1*theta where theta => P1*R1*theta = P2*R2 
				// where	P1 - parent transform in component space for original skeleton
				//			R1 - local space of the current bone for original skeleton
				//			P2 - parent transform in component space for new skeleton
				//			R2 - local space of the current bone for new skeleton
				// what we're looking for is theta, so that we can apply that to animated transform
				// this has to have all of nodes since comparing two skeletons, that might have different configuration
				int32 NumNodes = Rig->GetNodeNum();
				// saves the theta data per node
				RelativeToNewSpaceBases.AddUninitialized(NumNodes);
				// saves the translation conversion datao
				OldToNewTranslationRatio.AddUninitialized(NumNodes);

				const TArray<FNode>& Nodes = Rig->GetNodes();
				// calculate the relative transform to new skeleton
				// so that we can apply the delta in component space
				for (int32 NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
				{
					// theta (RelativeToNewTransform) = (P1*R1)^(-1) * P2*R2 where theta => P1*R1*theta = P2*R2
					RelativeToNewSpaceBases[NodeIndex] = NewSpaceBases[NodeIndex].GetRelativeTransform(OldSpaceBases[NodeIndex]); 

					// also savees the translation difference between old to new
					FVector OldTranslation = OldTranslations[NodeIndex];
					FVector NewTranslation = NewTranslations[NodeIndex];

					// skip root because we don't really have clear relative point to test with it
					if (NodeIndex != 0 && NewTranslationParentFlags[NodeIndex] == OldTranslationParentFlags[NodeIndex])
					{
						// only do this if parent status matches, otherwise, you'll have invalid state 
						// where one is based on shoulder, where the other is missing the shoulder node
						float OldTranslationSize = OldTranslation.Size();
						float NewTranslationSize = NewTranslation.Size();

						OldToNewTranslationRatio[NodeIndex] = (FMath::IsNearlyZero(OldTranslationSize)) ? 1.f/*do not touch new translation size*/ : NewTranslationSize / OldTranslationSize;
					}
					else
					{
						OldToNewTranslationRatio[NodeIndex] = 1.f; // set to be 1, we don't know what it is
					}

					UE_LOG(LogAnimation, Verbose, TEXT("Retargeting (%s : %d) : OldtoNewTranslationRatio (%0.2f), Relative Transform (%s)"), *Nodes[NodeIndex].Name.ToString(), NodeIndex, 
						OldToNewTranslationRatio[NodeIndex], *RelativeToNewSpaceBases[NodeIndex].ToString());
					UE_LOG(LogAnimation, Verbose, TEXT("\tOldSpaceBase(%s), NewSpaceBase(%s)"), *OldSpaceBases[NodeIndex].ToString(), *NewSpaceBases[NodeIndex].ToString());
				}
			}

			FAnimSequenceTrackContainer RiggingAnimationData;

			// now convert animation data to rig data
			ConvertAnimationDataToRiggingData(RiggingAnimationData);

			// here we have to watch out the index
			// The RiggingAnimationData will contain only the nodes that are mapped to source skeleton
			// and here we convert everything that is in RiggingAnimationData which means based on source data
			// when mapped back to new skeleton, it will discard results that are not mapped to target skeleton
			
			TArray<FName> SrcValidNodeNames;
			int32 SrcNumTracks = OldSkeleton->GetMappedValidNodes(SrcValidNodeNames);

			// now convert to space bases animation 
			TArray< TArray<FTransform> > ComponentSpaceAnimations, ConvertedLocalSpaceAnimations, ConvertedSpaceAnimations;
			ComponentSpaceAnimations.AddZeroed(SrcNumTracks);
			ConvertedSpaceAnimations.AddZeroed(SrcNumTracks);
			ConvertedLocalSpaceAnimations.AddZeroed(SrcNumTracks);

			const int32 NumKeys = DataModel->GetNumberOfKeys();
			float Interval = DataModel->GetFrameRate().AsInterval();

			// allocate arrays
			for(int32 SrcTrackIndex=0; SrcTrackIndex<SrcNumTracks; ++SrcTrackIndex)
			{
				ComponentSpaceAnimations[SrcTrackIndex].AddUninitialized(NumKeys);
				ConvertedLocalSpaceAnimations[SrcTrackIndex].AddUninitialized(NumKeys);
				ConvertedSpaceAnimations[SrcTrackIndex].AddUninitialized(NumKeys);
			}

			for (int32 SrcTrackIndex=0; SrcTrackIndex<SrcNumTracks; ++SrcTrackIndex)		
			{
				int32 NodeIndex = Rig->FindNode(SrcValidNodeNames[SrcTrackIndex]);
				check (NodeIndex != INDEX_NONE);
				auto& RawAnimation = RiggingAnimationData.AnimationTracks[SrcTrackIndex];

				// find rotation parent node
				int32 RotParentTrackIndex = FindValidTransformParentTrack(Rig, NodeIndex, false, SrcValidNodeNames);
				int32 TransParentTrackIndex = FindValidTransformParentTrack(Rig, NodeIndex, true, SrcValidNodeNames);
				// fill up keys - calculate PK1 * K1
				for(int32 Key=0; Key<NumKeys; ++Key)
				{
					FTransform AnimatedLocalKey;
					//ExtractBoneTransform(RiggingAnimationData.AnimationTracks[SrcTrackIndex], AnimatedLocalKey, Interval * Key);
					FAnimationUtils::ExtractTransformFromTrack(Interval* Key, NumberOfSampledKeys, GetPlayLength(), RiggingAnimationData.AnimationTracks[SrcTrackIndex], Interpolation, AnimatedLocalKey);

					AnimatedLocalKey.ScaleTranslation(OldToNewTranslationRatio[NodeIndex]);

					if(RotParentTrackIndex != INDEX_NONE)
					{
						FQuat ComponentSpaceRotation = ComponentSpaceAnimations[RotParentTrackIndex][Key].GetRotation() * AnimatedLocalKey.GetRotation();
						ComponentSpaceAnimations[SrcTrackIndex][Key].SetRotation(ComponentSpaceRotation);
					}
					else
					{
						ComponentSpaceAnimations[SrcTrackIndex][Key].SetRotation(AnimatedLocalKey.GetRotation());
					}

					if (TransParentTrackIndex != INDEX_NONE)
					{
						FVector ComponentSpaceTranslation = ComponentSpaceAnimations[TransParentTrackIndex][Key].TransformPosition(AnimatedLocalKey.GetTranslation());
						ComponentSpaceAnimations[SrcTrackIndex][Key].SetTranslation(ComponentSpaceTranslation);
						FVector ParentComponentSpaceScale3D = ComponentSpaceAnimations[TransParentTrackIndex][Key].GetScale3D();
						ComponentSpaceAnimations[SrcTrackIndex][Key].SetScale3D(ParentComponentSpaceScale3D * AnimatedLocalKey.GetScale3D());
					}
					else
					{
						ComponentSpaceAnimations[SrcTrackIndex][Key].SetTranslation(AnimatedLocalKey.GetTranslation());
						ComponentSpaceAnimations[SrcTrackIndex][Key].SetScale3D(AnimatedLocalKey.GetScale3D());
					}
				}
			}

			// now animation is converted to component space
			TArray<struct FRawAnimSequenceTrack> NewRawAnimationData = RiggingAnimationData.AnimationTracks;
			for (int32 SrcTrackIndex=0; SrcTrackIndex<SrcNumTracks; ++SrcTrackIndex)
			{
				int32 NodeIndex = Rig->FindNode(SrcValidNodeNames[SrcTrackIndex]);
				// find rotation parent node
				int32 RotParentTrackIndex = FindValidTransformParentTrack(Rig, NodeIndex, false, SrcValidNodeNames);
				int32 TransParentTrackIndex = FindValidTransformParentTrack(Rig, NodeIndex, true, SrcValidNodeNames);

				// clear translation;
				RelativeToNewSpaceBases[NodeIndex].SetTranslation(FVector::ZeroVector);

				for(int32 Key=0; Key<NumKeys; ++Key)
				{
					// now convert to the new space and save to local spaces
					ConvertedSpaceAnimations[SrcTrackIndex][Key] = RelativeToNewSpaceBases[NodeIndex] * ComponentSpaceAnimations[SrcTrackIndex][Key];

					if(RotParentTrackIndex != INDEX_NONE)
					{
						FQuat LocalRotation = ConvertedSpaceAnimations[RotParentTrackIndex][Key].GetRotation().Inverse() * ConvertedSpaceAnimations[SrcTrackIndex][Key].GetRotation();
						ConvertedLocalSpaceAnimations[SrcTrackIndex][Key].SetRotation(LocalRotation);
					}
					else
					{
						ConvertedLocalSpaceAnimations[SrcTrackIndex][Key].SetRotation(ConvertedSpaceAnimations[SrcTrackIndex][Key].GetRotation());
					}

					if(TransParentTrackIndex != INDEX_NONE)
					{
						FTransform LocalTransform = ConvertedSpaceAnimations[SrcTrackIndex][Key].GetRelativeTransform(ConvertedSpaceAnimations[TransParentTrackIndex][Key]);
						ConvertedLocalSpaceAnimations[SrcTrackIndex][Key].SetTranslation(LocalTransform.GetLocation());
						ConvertedLocalSpaceAnimations[SrcTrackIndex][Key].SetScale3D(LocalTransform.GetScale3D());
					}
					else
					{
						ConvertedLocalSpaceAnimations[SrcTrackIndex][Key].SetTranslation(ConvertedSpaceAnimations[SrcTrackIndex][Key].GetTranslation());
						ConvertedLocalSpaceAnimations[SrcTrackIndex][Key].SetScale3D(ConvertedSpaceAnimations[SrcTrackIndex][Key].GetScale3D());
					}
				}

				auto& RawAnimation = NewRawAnimationData[SrcTrackIndex];
				RawAnimation.PosKeys.Empty(NumKeys);
				RawAnimation.PosKeys.AddUninitialized(NumKeys);
				RawAnimation.RotKeys.Empty(NumKeys);
				RawAnimation.RotKeys.AddUninitialized(NumKeys);
				RawAnimation.ScaleKeys.Empty(NumKeys);
				RawAnimation.ScaleKeys.AddUninitialized(NumKeys);

				for(int32 Key=0; Key<NumKeys; ++Key)
				{
					RawAnimation.PosKeys[Key] = ConvertedLocalSpaceAnimations[SrcTrackIndex][Key].GetLocation();
					RawAnimation.RotKeys[Key] = ConvertedLocalSpaceAnimations[SrcTrackIndex][Key].GetRotation();
					RawAnimation.ScaleKeys[Key] = ConvertedLocalSpaceAnimations[SrcTrackIndex][Key].GetScale3D();

					// normalize rotation
					RawAnimation.RotKeys[Key].Normalize();
				}
			}

			RiggingAnimationData.AnimationTracks = MoveTemp(NewRawAnimationData);
			RiggingAnimationData.TrackNames = MoveTemp(SrcValidNodeNames);

			// set new skeleton
			SetSkeleton(NewSkeleton);

			// convert back to animated data with new skeleton
			ConvertRiggingDataToAnimationData(RiggingAnimationData);
		}
		// @todo end rig testing
		// @IMPORTANT: now otherwise this will try to do bone to bone mapping
		else if(OldSkeleton)
		{
			// Validate animation tracks against the new skeleton, any tracks linked to bones that do not exist in the new hierarchy are removed
			Controller->RemoveBoneTracksMissingFromSkeleton(NewSkeleton);
			ensure(DataModel->GetNumBoneTracks() != 0);

			// make sure you do update reference pose before coming here
			
			// first calculate component space ref pose to get the relative transform between
			// two ref poses. It is very important update ref pose before getting here. 
			TArray<FTransform> NewSpaceBaseRefPose, OldSpaceBaseRefPose, RelativeToNewTransform;
			// get the spacebases transform
			FAnimationRuntime::FillUpComponentSpaceTransformsRefPose(NewSkeleton, NewSpaceBaseRefPose);
			FAnimationRuntime::FillUpComponentSpaceTransformsRefPose(OldSkeleton, OldSpaceBaseRefPose);

			const TArray<FTransform>& OldRefPose = OldSkeleton->GetReferenceSkeleton().GetRefBonePose();
			const TArray<FTransform>& NewRefPose = NewSkeleton->GetReferenceSkeleton().GetRefBonePose();

			// now we'd like to get the relative transform from old to new ref pose in component space
			// PK2*K2 = PK1*K1*theta where theta => P1*R1*theta = P2*R2 
			// where	P1 - parent transform in component space for original skeleton
			//			R1 - local space of the current bone for original skeleton
			//			P2 - parent transform in component space for new skeleton
			//			R2 - local space of the current bone for new skeleton
			// what we're looking for is theta, so that we can apply that to animated transform
			int32 NumBones = NewSpaceBaseRefPose.Num();
			// saves the theta data per bone
			RelativeToNewTransform.AddUninitialized(NumBones);
			TArray<float> OldToNewTranslationRatio;
			// saves the translation conversion data
			OldToNewTranslationRatio.AddUninitialized(NumBones);

			// calculate the relative transform to new skeleton
			// so that we can apply the delta in component space
			for(int32 BoneIndex=0; BoneIndex<NumBones; ++BoneIndex)
			{
				// first find bone name of the idnex
				FName BoneName = NewSkeleton->GetReferenceSkeleton().GetRefBoneInfo()[BoneIndex].Name;
				// find it in old index
				int32 OldBoneIndex = OldSkeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);

				// get old bone index
				if(OldBoneIndex != INDEX_NONE)
				{
					// theta (RelativeToNewTransform) = (P1*R1)^(-1) * P2*R2 where theta => P1*R1*theta = P2*R2
					RelativeToNewTransform[BoneIndex] = NewSpaceBaseRefPose[BoneIndex].GetRelativeTransform(OldSpaceBaseRefPose[OldBoneIndex]);

					// also savees the translation difference between old to new
					FVector OldTranslation = OldRefPose[OldBoneIndex].GetTranslation();
					FVector NewTranslation = NewRefPose[BoneIndex].GetTranslation();

					float OldTranslationSize = OldTranslation.Size();
					float NewTranslationSize = NewTranslation.Size();
					OldToNewTranslationRatio[BoneIndex] = (FMath::IsNearlyZero(OldTranslationSize))? 1.f/*do not touch new translation size*/ : NewTranslationSize/OldTranslationSize;
				}
				else
				{
					RelativeToNewTransform[BoneIndex].SetIdentity();
				}
			}

			// 2d array of animated time [boneindex][time key]
			TArray< TArray<FTransform> > AnimatedSpaceBases, ConvertedLocalSpaces, ConvertedSpaceBases;
			AnimatedSpaceBases.AddZeroed(NumBones);
			ConvertedLocalSpaces.AddZeroed(NumBones);
			ConvertedSpaceBases.AddZeroed(NumBones);

			const int32 NumKeys = DataModel->GetNumberOfKeys();
			const float Interval = DataModel->GetFrameRate().AsInterval();

			// allocate arrays
			for(int32 BoneIndex=0; BoneIndex<NumBones; ++BoneIndex)
			{
				AnimatedSpaceBases[BoneIndex].AddUninitialized(NumKeys);
				ConvertedLocalSpaces[BoneIndex].AddUninitialized(NumKeys);
				ConvertedSpaceBases[BoneIndex].AddUninitialized(NumKeys);
			}

			// now calculating old animated space bases
			// this one calculates aniamted space per bones and per key
			for(int32 BoneIndex=0; BoneIndex<NumBones; ++BoneIndex)
			{
				FName BoneName = NewSkeleton->GetReferenceSkeleton().GetBoneName(BoneIndex);
				const int32 OldBoneIndex = OldSkeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);
				const int32 TrackIndex = DataModel->GetBoneTrackIndexByName(BoneName);
				const int32 ParentBoneIndex = NewSkeleton->GetReferenceSkeleton().GetParentIndex(BoneIndex);

				if(TrackIndex != INDEX_NONE)
				{
					// fill up keys - calculate PK1 * K1
					for(int32 Key=0; Key<NumKeys; ++Key)
					{
						FTransform AnimatedLocalKey;
						UE::Anim::GetBoneTransformFromModel(DataModel, AnimatedLocalKey, TrackIndex, Interval * Key, Interpolation);

						// note that we apply scale in the animated space
						// at this point, you should have scaled version of animated skeleton
						AnimatedLocalKey.ScaleTranslation(OldToNewTranslationRatio[BoneIndex]);

						if(ParentBoneIndex != INDEX_NONE)
						{
							AnimatedSpaceBases[BoneIndex][Key] = AnimatedLocalKey * AnimatedSpaceBases[ParentBoneIndex][Key];
						}
						else
						{
							AnimatedSpaceBases[BoneIndex][Key] = AnimatedLocalKey;
						}
					}
				}
				else
				{
					// get local spaces from refpose and use that to fill it up
					FTransform LocalTransform = (OldBoneIndex != INDEX_NONE)? OldSkeleton->GetReferenceSkeleton().GetRefBonePose()[OldBoneIndex] : FTransform::Identity;

					for(int32 Key=0; Key<NumKeys; ++Key)
					{
						if(ParentBoneIndex != INDEX_NONE)
						{
							AnimatedSpaceBases[BoneIndex][Key] = LocalTransform * AnimatedSpaceBases[ParentBoneIndex][Key];
						}
						else
						{
							AnimatedSpaceBases[BoneIndex][Key] = LocalTransform;
						}
					}
				}
			}

			TArray<FVector> PosKeys;
			TArray<FQuat> RotKeys;
			TArray<FVector> ScaleKeys;

			// now apply the theta back to the animated space bases
			for(int32 BoneIndex=0; BoneIndex<NumBones; ++BoneIndex)
			{
				const FName BoneName = NewSkeleton->GetReferenceSkeleton().GetBoneName(BoneIndex);
				const int32 TrackIndex = DataModel->GetBoneTrackIndexByName(BoneName);
				int32 ParentBoneIndex = NewSkeleton->GetReferenceSkeleton().GetParentIndex(BoneIndex);

				for(int32 Key=0; Key<NumKeys; ++Key)
				{
					// thus PK2 & K2 =  PK1 * K1 * theta where theta = (P1*R1)^(-1) * P2*R2
					// where PK2	: parent transform in component space of animated key for new skeleton
					//		 K2		: local transform of animated key for new skeleton
					//		 PK1	: parent transform in component space of animated key for old skeleton
					//		 K1		: local transform of animated key for old skeleton
					FTransform SpaceBase;
					// we don't just apply it because translation is sensitive
					// we don't like to apply relative transform to tranlsation directly
					// rotation and scale we can, but translation we'd like to use scaled translation instead of transformed location
					// as their relative translation can be different
					SpaceBase.SetRotation(AnimatedSpaceBases[BoneIndex][Key].GetRotation() * RelativeToNewTransform[BoneIndex].GetRotation());
					SpaceBase.SetScale3D(AnimatedSpaceBases[BoneIndex][Key].GetScale3D() * RelativeToNewTransform[BoneIndex].GetScale3D());
					// use animated scaled translation directly
					SpaceBase.SetTranslation(AnimatedSpaceBases[BoneIndex][Key].GetTranslation());
					ConvertedSpaceBases[BoneIndex][Key] = SpaceBase;
					// now calculate local space for animation
					if(ParentBoneIndex != INDEX_NONE)
					{
						// K2 = PK2^(-1) * PK1 * K1 * (P1*R1)^(-1) * P2*R2
						ConvertedLocalSpaces[BoneIndex][Key] = SpaceBase.GetRelativeTransform(ConvertedSpaceBases[ParentBoneIndex][Key]);
					}
					else
					{
						ConvertedLocalSpaces[BoneIndex][Key] = SpaceBase;
					}
				}

				// now save back to animation data
				if(TrackIndex != INDEX_NONE)
				{
					PosKeys.SetNumUninitialized(NumKeys);
					RotKeys.SetNumUninitialized(NumKeys);
					ScaleKeys.SetNumUninitialized(NumKeys);

					for(int32 Key=0; Key<NumKeys; ++Key)
					{
						PosKeys[Key] = ConvertedLocalSpaces[BoneIndex][Key].GetLocation();
						RotKeys[Key] = ConvertedLocalSpaces[BoneIndex][Key].GetRotation();
						ScaleKeys[Key] = ConvertedLocalSpaces[BoneIndex][Key].GetScale3D();
					}

					Controller->SetBoneTrackKeys(BoneName, PosKeys, RotKeys, ScaleKeys);
				}
			}
		}
		else
		{
			Controller->RemoveBoneTracksMissingFromSkeleton(NewSkeleton);
		}

		// I have to set this here in order for compression
		// that has to happen outside of this after Skeleton changes
		SetSkeleton(NewSkeleton);
	}
	else
	{
		Controller->RemoveBoneTracksMissingFromSkeleton(NewSkeleton);
	}

	Super::RemapTracksToNewSkeleton(NewSkeleton, bConvertSpaces);
}

void UAnimSequence::PostProcessSequence(bool bForceNewRawDatGuid)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// Apply compression
	MarkRawDataAsModified(bForceNewRawDatGuid);
	OnRawDataChanged();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	// mark package as dirty
	MarkPackageDirty();
}

void UAnimSequence::RemoveNaNTracks()
{
	ValidateModel();

	TArray<FName> TracksToRemove;

	const TArray<FBoneAnimationTrack>& BoneAnimationTracks = DataModel->GetBoneAnimationTracks();		
	for (const FBoneAnimationTrack& AnimationTrack : BoneAnimationTracks)
	{
		const FRawAnimSequenceTrack& RawTrack = AnimationTrack.InternalTrackData;

		bool bContainsNaN = false;
		for ( auto Key : RawTrack.PosKeys )
		{
			bContainsNaN |= Key.ContainsNaN();
		}

		if (!bContainsNaN)
		{
			for(auto Key : RawTrack.RotKeys)
			{
				bContainsNaN |= Key.ContainsNaN();
			}
		}

		if (!bContainsNaN)
		{
			for(auto Key : RawTrack.ScaleKeys)
			{
				bContainsNaN |= Key.ContainsNaN();
			}
		}

		if (bContainsNaN)
		{
			UE_LOG(LogAnimation, Warning, TEXT("Animation raw data contains NaNs - Removing the following track [%s Track (%s)]"), (GetOuter() ? *GetOuter()->GetFullName() : *GetFullName()), *AnimationTrack.Name.ToString());

			TracksToRemove.Add(AnimationTrack.Name);
		}
	}

	UAnimDataController::FScopedBracket ScopedBracket(Controller, LOCTEXT("RemoveNaNTracks_Bracket", "Removing track(s) containing NaN key data"));	
	for (const FName& TrackName : TracksToRemove)
	{
		Controller->RemoveBoneTrack(TrackName);
	}	
}

void UAnimSequence::RemoveAllTracks()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	RawAnimationData.Empty();
	AnimationTrackNames.Empty();
	TrackToSkeletonMapTable.Empty();
	SourceRawAnimationData_DEPRECATED.Empty();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	
	ValidateModel();
	UAnimDataController::FScopedBracket ScopedBracket(Controller, LOCTEXT("RemoveAllTracks_Bracket", "Removing all Bone Animation and Transform Curve Tracks"));
	Controller->RemoveAllBoneTracks();

	// clear all transform tracks
	// not deleting curve names from skeleton 
	// since we don't know if that name is used by other assets
	Controller->RemoveAllCurvesOfType(ERawCurveTrackTypes::RCT_Transform);	
}

void UAnimSequence::RemoveTrack(int32 TrackIndex)
{
	ValidateModel();
	
	if (DataModel->IsValidBoneTrackIndex(TrackIndex))
	{
		Controller->RemoveBoneTrack(DataModel->GetBoneTrackByIndex(TrackIndex).Name);
	}
}

int32 FindFirstChildTrack(const USkeleton* MySkeleton, const FReferenceSkeleton& RefSkeleton, const TArray<FName>& AnimationTrackNames, FName BoneName)
{
	const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
	if(BoneIndex == INDEX_NONE)
	{
		// get out, nothing to do
		return INDEX_NONE;
	}

	// find children
	TArray<int32> Childs;
	if(MySkeleton->GetChildBones(BoneIndex, Childs) > 0)
	{
		// first look for direct children
		for(auto ChildIndex : Childs)
		{
			FName ChildBoneName = RefSkeleton.GetBoneName(ChildIndex);
			int32 ChildTrackIndex = AnimationTrackNames.Find(ChildBoneName);
			if(ChildTrackIndex != INDEX_NONE)
			{
				// found the new track
				return ChildTrackIndex;
			}
		}

		int32 BestGrandChildIndex = INDEX_NONE;
		// if you didn't find yet, now you have to go through all children
		for(auto ChildIndex : Childs)
		{
			FName ChildBoneName = RefSkeleton.GetBoneName(ChildIndex);
			// now I have to go through all childrewn and find who is earliest since I don't know which one might be the closest one
			int32 GrandChildIndex = FindFirstChildTrack(MySkeleton, RefSkeleton, AnimationTrackNames, ChildBoneName);
			if (GrandChildIndex != INDEX_NONE)
			{
				if (BestGrandChildIndex == INDEX_NONE)
				{
					BestGrandChildIndex = GrandChildIndex;
				}
				else if (BestGrandChildIndex > GrandChildIndex)
				{
					// best should be earlier track index
					BestGrandChildIndex = GrandChildIndex;
				}
			}
		}

		return BestGrandChildIndex;
	}
	else
	{
		// there is no child, just add at the end
		return AnimationTrackNames.Num();
	}
}

int32 UAnimSequence::InsertTrack(const FName& BoneName)
{
	ValidateModel();

	// first verify if it doesn't exists, if it does, return
	const int32 ExistingTrackIndex = DataModel->GetBoneTrackIndexByName(BoneName);
	if (ExistingTrackIndex != INDEX_NONE)
	{
		return ExistingTrackIndex;
	}

	USkeleton * MySkeleton = GetSkeleton();
	// should not call this if skeleton was empty
	if (ensure(MySkeleton) == false)
	{
		return INDEX_NONE;
	}

	const FReferenceSkeleton& RefSkeleton = MySkeleton->GetReferenceSkeleton();

	TArray<FName> TrackNames;
	DataModel->GetBoneTrackNames(TrackNames);

	const int32 NewTrackIndex = FindFirstChildTrack(MySkeleton, RefSkeleton, TrackNames, BoneName);
	ensure(NewTrackIndex <= DataModel->GetNumBoneTracks());
	const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
	if (NewTrackIndex != INDEX_NONE)
	{
		const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();

		FRawAnimSequenceTrack RawTrack;
		RawTrack.PosKeys.Add(RefPose[BoneIndex].GetTranslation());
		RawTrack.RotKeys.Add(RefPose[BoneIndex].GetRotation());
		RawTrack.ScaleKeys.Add(RefPose[BoneIndex].GetScale3D());

		// now insert to the track
		Controller->InsertBoneTrack(BoneName, NewTrackIndex);
	}

	return NewTrackIndex;
}

void UAnimSequence::ResizeSequence(float NewLength, int32 NewNumFrames, bool bInsert, int32 StartFrame/*inclusive */, int32 EndFrame/*inclusive*/)
{
 	ValidateModel();

	const FFrameRate& FrameRate = DataModel->GetFrameRate();
	const float T0 = FrameRate.AsSeconds(StartFrame);
	const float T1 = FrameRate.AsSeconds(EndFrame);

	FFrameRate NewFrameRate = FFrameRate((NewLength / NewNumFrames), 1);

	GetController()->Resize(NewLength, T0, T1);

	ensure(DataModel->GetNumberOfKeys() == NewNumFrames);
}

bool UAnimSequence::GetAllAnimationSequencesReferred(TArray<UAnimationAsset*>& AnimationAssets, bool bRecursive /*= true*/)
{
	Super::GetAllAnimationSequencesReferred(AnimationAssets, bRecursive);
	if (RefPoseSeq  && RefPoseSeq != this && !AnimationAssets.Contains(RefPoseSeq))
	{
		RefPoseSeq->HandleAnimReferenceCollection(AnimationAssets, bRecursive);
	}
	return AnimationAssets.Num() > 0;
}

void UAnimSequence::ReplaceReferredAnimations(const TMap<UAnimationAsset*, UAnimationAsset*>& ReplacementMap)
{
	Super::ReplaceReferredAnimations(ReplacementMap);

	if (RefPoseSeq)
	{
		UAnimSequence* const* ReplacementAsset = (UAnimSequence*const*)ReplacementMap.Find(RefPoseSeq);
		if (ReplacementAsset)
		{
			RefPoseSeq = *ReplacementAsset;
		}
	}
}

bool UAnimSequence::AddLoopingInterpolation()
{
	return UE::Anim::AnimationData::AddLoopingInterpolation(this);
}

int32 FindParentNodeIndex(URig* Rig, USkeleton* Skeleton, FName ParentNodeName)
{
	const int32& ParentNodeIndex = Rig->FindNode(ParentNodeName);
	const FName& ParentBoneName = Skeleton->GetRigBoneMapping(ParentNodeName);
	
	return Skeleton->GetReferenceSkeleton().FindBoneIndex(ParentBoneName);
}

int32 UAnimSequence::GetSpaceBasedAnimationData(TArray< TArray<FTransform> >& AnimationDataInComponentSpace, FAnimSequenceTrackContainer * RiggingAnimationData) const
{
	USkeleton* MySkeleton = GetSkeleton();

	check(MySkeleton);
	const FReferenceSkeleton& RefSkeleton = MySkeleton->GetReferenceSkeleton();
	int32 NumBones = RefSkeleton.GetNum();

	AnimationDataInComponentSpace.Empty(NumBones);
	AnimationDataInComponentSpace.AddZeroed(NumBones);

	// 2d array of animated time [boneindex][time key]
	const int32 NumKeys = DataModel->GetNumberOfKeys();
	const float Interval = DataModel->GetFrameRate().AsInterval();

	// allocate arrays
	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		AnimationDataInComponentSpace[BoneIndex].AddUninitialized(NumKeys);
	}

	if (RiggingAnimationData)
	{
		const URig* Rig = MySkeleton->GetRig();

		check(Rig);

		// to fix the issue where parent of rig doesn't correspond to parent of this skeleton
		// we do this in multiple iteration if needed. 
		// this flag will be used to evaluate all of them until done
		TArray<bool> BoneEvaluated;
		BoneEvaluated.AddZeroed(NumBones);

		bool bCompleted = false;
		do
		{
			for(int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
			{
				if ( !BoneEvaluated[BoneIndex] )
				{
					const FName& BoneName = RefSkeleton.GetBoneName(BoneIndex);
					const FName& NodeName = MySkeleton->GetRigNodeNameFromBoneName(BoneName);
					const FTransformBase* TransformBase = Rig->GetTransformBaseByNodeName(NodeName);
					const int32 NodeIndex = RiggingAnimationData->TrackNames.Find(NodeName);
					if(NodeIndex != INDEX_NONE)
					{
						check(TransformBase);

						// now calculate the component space
						const TArray<FRigTransformConstraint>	& RotTransformConstraints = TransformBase->Constraints[EControlConstraint::Type::Orientation].TransformConstraints;

						FQuat ComponentRotation;
						FTransform ComponentTranslation;
						FVector ComponentScale;

						// rotation first
						// this is easy since we just make sure it's evaluated or not
						{
							const FName& ParentNodeName = RotTransformConstraints[0].ParentSpace;
							const FName& ParentBoneName = MySkeleton->GetRigBoneMapping(ParentNodeName);
							const int32& ParentBoneIndex = RefSkeleton.FindBoneIndex(ParentBoneName);

							if(ParentBoneIndex != INDEX_NONE)
							{
								if (BoneEvaluated[ParentBoneIndex])
								{
									for(int32 Key = 0; Key < NumKeys; ++Key)
									{
										ComponentRotation = AnimationDataInComponentSpace[ParentBoneIndex][Key].GetRotation() * RiggingAnimationData->AnimationTracks[NodeIndex].RotKeys[Key];
										AnimationDataInComponentSpace[BoneIndex][Key].SetRotation(ComponentRotation);
									}

									BoneEvaluated[BoneIndex] = true;
								}
							}
							else
							{
								for(int32 Key = 0; Key < NumKeys; ++Key)
								{
									ComponentRotation = RiggingAnimationData->AnimationTracks[NodeIndex].RotKeys[Key];
									AnimationDataInComponentSpace[BoneIndex][Key].SetRotation(ComponentRotation);
								}

								BoneEvaluated[BoneIndex] = true;
							}
						}

						const TArray<FRigTransformConstraint>	& PosTransformConstraints = TransformBase->Constraints[EControlConstraint::Type::Translation].TransformConstraints;

						// now time to check translation
						// this is a bit more complicated
						// since we have to make sure if it's true to start with
						// did we succeed on getting rotation?
						if (BoneEvaluated[BoneIndex])
						{
							const FName& ParentNodeName = PosTransformConstraints[0].ParentSpace;
							const FName& ParentBoneName = MySkeleton->GetRigBoneMapping(ParentNodeName);
							const int32& ParentBoneIndex = RefSkeleton.FindBoneIndex(ParentBoneName);

							if(ParentBoneIndex != INDEX_NONE)
							{
								// this has to be check
								if (BoneEvaluated[ParentBoneIndex])
								{
									for(int32 Key = 0; Key < NumKeys; ++Key)
									{
										const FTransform& AnimCompSpace = AnimationDataInComponentSpace[ParentBoneIndex][Key];
										ComponentTranslation = FTransform(RiggingAnimationData->AnimationTracks[NodeIndex].PosKeys[Key]) * AnimCompSpace;
										AnimationDataInComponentSpace[BoneIndex][Key].SetTranslation(ComponentTranslation.GetTranslation());

										ComponentScale = AnimCompSpace.GetScale3D() * RiggingAnimationData->AnimationTracks[NodeIndex].ScaleKeys[Key];
										AnimationDataInComponentSpace[BoneIndex][Key].SetScale3D(ComponentScale);
									}
								}
								else
								{
									// if we failed to get parent clear the flag
									// because if translation has been calculated, BoneEvaluated[BoneIndex] might be true
									BoneEvaluated[BoneIndex] = false;
								}
							}
							else
							{
								for(int32 Key = 0; Key < NumKeys; ++Key)
								{
									ComponentTranslation = FTransform(RiggingAnimationData->AnimationTracks[NodeIndex].PosKeys[Key]);
									AnimationDataInComponentSpace[BoneIndex][Key].SetTranslation(ComponentTranslation.GetTranslation());
									
									ComponentScale = RiggingAnimationData->AnimationTracks[NodeIndex].ScaleKeys[Key];
									AnimationDataInComponentSpace[BoneIndex][Key].SetScale3D(ComponentScale);
								}
							}
						}
					}
					else
					{
						int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
						const FTransform& LocalSpace = RefSkeleton.GetRefBonePose()[BoneIndex];
						if(ParentIndex != INDEX_NONE)
						{
							// if parent is evaluated, do it
							if (BoneEvaluated[ParentIndex])
							{
								for(int32 Key = 0; Key < NumKeys; ++Key)
								{
									AnimationDataInComponentSpace[BoneIndex][Key] = LocalSpace * AnimationDataInComponentSpace[ParentIndex][Key];
								}

								BoneEvaluated[BoneIndex] = true;
							}
						}
						else
						{
							BoneEvaluated[BoneIndex] = true;

							for(int32 Key = 0; Key < NumKeys; ++Key)
							{
								AnimationDataInComponentSpace[BoneIndex][Key] = LocalSpace;
							}
						}
					}
				}
			}

			bCompleted = true;
			// see if we can get out, brute force for now
			for(int32 BoneIndex = 0; BoneIndex < NumBones && bCompleted; ++BoneIndex)
			{
				bCompleted &= !!BoneEvaluated[BoneIndex];
			}
		} while (bCompleted == false);
	}
	else
	{
		ValidateModel();

		// now calculating old animated space bases
		// this one calculates aniamted space per bones and per key
		for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
		{
			const FName BoneName = MySkeleton->GetReferenceSkeleton().GetBoneName(BoneIndex);
			const int32 TrackIndex = DataModel->GetBoneTrackIndexByName(BoneName);
			const int32 ParentBoneIndex = MySkeleton->GetReferenceSkeleton().GetParentIndex(BoneIndex);

			if (TrackIndex != INDEX_NONE)
			{
				// fill up keys - calculate PK1 * K1
				for (int32 Key = 0; Key < NumKeys; ++Key)
				{
					FTransform AnimatedLocalKey;
					UE::Anim::GetBoneTransformFromModel(DataModel, AnimatedLocalKey, TrackIndex, Interval * Key, Interpolation);

					if (ParentBoneIndex != INDEX_NONE)
					{
						AnimationDataInComponentSpace[BoneIndex][Key] = AnimatedLocalKey * AnimationDataInComponentSpace[ParentBoneIndex][Key];
					}
					else
					{
						AnimationDataInComponentSpace[BoneIndex][Key] = AnimatedLocalKey;
					}
				}
			}
			else
			{
				// get local spaces from refpose and use that to fill it up
				FTransform LocalTransform = MySkeleton->GetReferenceSkeleton().GetRefBonePose()[BoneIndex];

				for (int32 Key = 0; Key < NumKeys; ++Key)
				{
					if (ParentBoneIndex != INDEX_NONE)
					{
						AnimationDataInComponentSpace[BoneIndex][Key] = LocalTransform * AnimationDataInComponentSpace[ParentBoneIndex][Key];
					}
					else
					{
						AnimationDataInComponentSpace[BoneIndex][Key] = LocalTransform;
					}
				}
			}	
		}

	}

	return AnimationDataInComponentSpace.Num();
}

bool UAnimSequence::ConvertAnimationDataToRiggingData(FAnimSequenceTrackContainer& RiggingAnimationData)
{
	USkeleton* MySkeleton = GetSkeleton();
	if (MySkeleton && MySkeleton->GetRig())
	{
		const URig* Rig = MySkeleton->GetRig();
		TArray<FName> ValidNodeNames;
		int32 NumNodes = MySkeleton->GetMappedValidNodes(ValidNodeNames);
		TArray< TArray<FTransform> > AnimationDataInComponentSpace;
		int32 NumBones = GetSpaceBasedAnimationData(AnimationDataInComponentSpace, NULL);

		const int32 NumModelKeys = DataModel->GetNumberOfKeys();

		if (NumBones > 0)
		{
			RiggingAnimationData.Initialize(ValidNodeNames);

			// first we copy all space bases back to it
			for (int32 NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
			{
				struct FRawAnimSequenceTrack& Track = RiggingAnimationData.AnimationTracks[NodeIndex];
				const FName& NodeName = ValidNodeNames[NodeIndex];
				const FName& BoneName = MySkeleton->GetRigBoneMapping(NodeName);
				const int32& BoneIndex = MySkeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);

				if (ensure(BoneIndex != INDEX_NONE))
				{
					Track.PosKeys.Empty(NumModelKeys);
					Track.RotKeys.Empty(NumModelKeys);
					Track.ScaleKeys.Empty(NumModelKeys);
					Track.PosKeys.AddUninitialized(NumModelKeys);
					Track.RotKeys.AddUninitialized(NumModelKeys);
					Track.ScaleKeys.AddUninitialized(NumModelKeys);

					int32 RigConstraintIndex = Rig->FindTransformBaseByNodeName(NodeName);

					if (RigConstraintIndex != INDEX_NONE)
					{
						const auto* RigConstraint = Rig->GetTransformBase(RigConstraintIndex);

						// apply orientation - for now only one
						const TArray<FRigTransformConstraint>& RotationTransformConstraint = RigConstraint->Constraints[EControlConstraint::Type::Orientation].TransformConstraints;

						if (RotationTransformConstraint.Num() > 0)
						{
							const FName& ParentSpace = RotationTransformConstraint[0].ParentSpace;
							const FName& ParentBoneName = MySkeleton->GetRigBoneMapping(ParentSpace);
							const int32& ParentBoneIndex = MySkeleton->GetReferenceSkeleton().FindBoneIndex(ParentBoneName);
							if (ParentBoneIndex != INDEX_NONE)
							{
								// if no rig control, component space is used
								for (int32 KeyIndex = 0; KeyIndex < NumModelKeys; ++KeyIndex)
								{
									FTransform ParentTransform = AnimationDataInComponentSpace[ParentBoneIndex][KeyIndex];
									FTransform RelativeTransform = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetRelativeTransform(ParentTransform);
									Track.RotKeys[KeyIndex] = RelativeTransform.GetRotation();
								}
							}
							else
							{
								// if no rig control, component space is used
								for (int32 KeyIndex = 0; KeyIndex < NumModelKeys; ++KeyIndex)
								{
									Track.RotKeys[KeyIndex] = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetRotation();
								}
							}
						}
						else
						{
							// if no rig control, component space is used
							for (int32 KeyIndex = 0; KeyIndex < NumModelKeys; ++KeyIndex)
							{
								Track.RotKeys[KeyIndex] = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetRotation();
							}
						}

						// apply translation - for now only one
						const TArray<FRigTransformConstraint>& TranslationTransformConstraint = RigConstraint->Constraints[EControlConstraint::Type::Translation].TransformConstraints;

						if (TranslationTransformConstraint.Num() > 0)
						{
							const FName& ParentSpace = TranslationTransformConstraint[0].ParentSpace;
							const FName& ParentBoneName = MySkeleton->GetRigBoneMapping(ParentSpace);
							const int32& ParentBoneIndex = MySkeleton->GetReferenceSkeleton().FindBoneIndex(ParentBoneName);
							if (ParentBoneIndex != INDEX_NONE)
							{
								// if no rig control, component space is used
								for (int32 KeyIndex = 0; KeyIndex < NumModelKeys; ++KeyIndex)
								{
									FTransform ParentTransform = AnimationDataInComponentSpace[ParentBoneIndex][KeyIndex];
									FTransform RelativeTransform = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetRelativeTransform(ParentTransform);
									Track.PosKeys[KeyIndex] = RelativeTransform.GetTranslation();
									Track.ScaleKeys[KeyIndex] = RelativeTransform.GetScale3D();
								}
							}
							else
							{
								for (int32 KeyIndex = 0; KeyIndex < NumModelKeys; ++KeyIndex)
								{
									Track.PosKeys[KeyIndex] = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetTranslation();
									Track.ScaleKeys[KeyIndex] = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetScale3D();
								}
							}
						}
						else
						{
							for (int32 KeyIndex = 0; KeyIndex < NumModelKeys; ++KeyIndex)
							{
								Track.PosKeys[KeyIndex] = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetTranslation();
								Track.ScaleKeys[KeyIndex] = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetScale3D();
							}
						}
					}
					else
					{
						// if no rig control, component space is used
						for (int32 KeyIndex = 0; KeyIndex < NumModelKeys; ++KeyIndex)
						{
							Track.PosKeys[KeyIndex] = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetTranslation();
							Track.RotKeys[KeyIndex] = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetRotation();
							Track.ScaleKeys[KeyIndex] = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetScale3D();
						}
					}
				}
			}
		}

		return true;
	}

	return false;
}

bool UAnimSequence::ConvertRiggingDataToAnimationData(FAnimSequenceTrackContainer& RiggingAnimationData)
{
	if (RiggingAnimationData.GetNum() > 0)
	{
		ValidateModel();

		UAnimDataController::FScopedBracket ScopedBracket(Controller, LOCTEXT("ConvertRiggingDataToAnimationData_Bracket", "Converting Rigging Data to Animation Data"));

		TArray< TArray<FTransform> > AnimationDataInComponentSpace;
		int32 NumBones = GetSpaceBasedAnimationData(AnimationDataInComponentSpace, &RiggingAnimationData);

		USkeleton* MySkeleton = GetSkeleton();
		TArray<FName> ValidNodeNames;
		MySkeleton->GetMappedValidNodes(ValidNodeNames);
		// remove from ValidNodeNames if it doesn't belong to AnimationTrackNames
		for (int32 NameIndex=0; NameIndex<ValidNodeNames.Num(); ++NameIndex)
		{
			if (RiggingAnimationData.TrackNames.Contains(ValidNodeNames[NameIndex]) == false)
			{
				ValidNodeNames.RemoveAt(NameIndex);
				--NameIndex;
			}
		}

		int32 ValidNumNodes = ValidNodeNames.Num();

		// get local spaces
		// add all tracks?
		Controller->RemoveAllBoneTracks();

		// Reused track data arrays
		TArray<FVector> PosKeys;
		TArray<FQuat> RotKeys;
		TArray<FVector> ScaleKeys;

		const int32 NumModelKeys = DataModel->GetNumberOfKeys();
		const FReferenceSkeleton& RefSkeleton = MySkeleton->GetReferenceSkeleton();
		const URig* Rig = MySkeleton->GetRig();
		for (int32 NodeIndex = 0; NodeIndex < ValidNumNodes; ++NodeIndex)
		{
			FName BoneName = MySkeleton->GetRigBoneMapping(ValidNodeNames[NodeIndex]);
			int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);

			if (BoneIndex != INDEX_NONE)
			{
				// Add a new track by name
				Controller->AddBoneTrack(BoneName);

				PosKeys.SetNumUninitialized(NumModelKeys);
				RotKeys.SetNumUninitialized(NumModelKeys);
				ScaleKeys.SetNumUninitialized(NumModelKeys);

				const int32& ParentBoneIndex = RefSkeleton.GetParentIndex(BoneIndex);

				if(ParentBoneIndex != INDEX_NONE)
				{
					for(int32 KeyIndex = 0; KeyIndex < NumModelKeys; ++KeyIndex)
					{
						const FTransform LocalTransform = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetRelativeTransform(AnimationDataInComponentSpace[ParentBoneIndex][KeyIndex]);

						PosKeys[KeyIndex] = LocalTransform.GetTranslation();
						RotKeys[KeyIndex] = LocalTransform.GetRotation();
						ScaleKeys[KeyIndex] = LocalTransform.GetScale3D();
					}
				}
				else
				{
					for(int32 KeyIndex = 0; KeyIndex < NumModelKeys; ++KeyIndex)
					{
						const FTransform LocalTransform = AnimationDataInComponentSpace[BoneIndex][KeyIndex];

						PosKeys[KeyIndex] = LocalTransform.GetTranslation();
						RotKeys[KeyIndex] = LocalTransform.GetRotation();
						ScaleKeys[KeyIndex] = LocalTransform.GetScale3D();
					}
				}

				// Set the track's key data
				Controller->SetBoneTrackKeys(BoneName, PosKeys, RotKeys, ScaleKeys);
			}
		}

		return true;
	}

	return false;
}

void UAnimSequence::ClearBakedTransformData()
{
	UE_LOG(LogAnimation, Warning, TEXT("[%s] Detected previous edited data is invalidated. Clearing transform curve data and Source Data. This can happen if you do retarget another animation to this. If not, please report back to Epic. "), *GetName());
	//Clear Transform curve data
	Controller->RemoveAllCurvesOfType(ERawCurveTrackTypes::RCT_Transform);
}

bool UAnimSequence::DoesContainTransformCurves() const
{
	ValidateModel();
	return DataModel->GetNumberOfTransformCurves() > 0;
}

void UAnimSequence::BakeTrackCurvesToRawAnimationTracks(TArray<FRawAnimSequenceTrack>& InOutNewRawTracks, TArray<FName>& InOutNewTrackNames, TArray<FTrackToSkeletonMap>& InOutNewTrackToSkeletonMapTable)
{
	ValidateModel();

	if (DataModel->GetNumberOfTransformCurves() > 0)
	{
		USkeleton* CurSkeleton = GetSkeleton();
		check(CurSkeleton);

		const TArray<FBoneAnimationTrack>& BoneAnimationTracks = ResampledAnimationTrackData;
			
		InOutNewRawTracks.Reserve(BoneAnimationTracks.Num());
		InOutNewTrackToSkeletonMapTable.Reserve(BoneAnimationTracks.Num());
		InOutNewTrackNames.Reserve(BoneAnimationTracks.Num());

		for (FRawAnimSequenceTrack& Track : InOutNewRawTracks)
		{
		// since now I'm about to modify Scale Keys. I should add all of them here at least one key. 
		// if all turns out to be same, it will clear it up. 
			if (Track.ScaleKeys.Num() == 0)
			{
				// at least add one
				static FVector ScaleConstantKey(1.f);
				Track.ScaleKeys.Add(ScaleConstantKey);
			}
		}

		auto InsertNewBoneTrack = [&InOutNewRawTracks, &InOutNewTrackToSkeletonMapTable, &InOutNewTrackNames, CurSkeleton](const FName& BoneName) -> int32
		{
			const FReferenceSkeleton& RefSkeleton = CurSkeleton->GetReferenceSkeleton();
			const int32 NewTrackIndex = FindFirstChildTrack(CurSkeleton, RefSkeleton, InOutNewTrackNames, BoneName);
			const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
			if (NewTrackIndex != INDEX_NONE)
			{
				const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();

				FRawAnimSequenceTrack& RawTrack = InOutNewRawTracks.InsertDefaulted_GetRef(NewTrackIndex);
				RawTrack.PosKeys.Add(RefPose[BoneIndex].GetTranslation());
				RawTrack.RotKeys.Add(RefPose[BoneIndex].GetRotation());
				RawTrack.ScaleKeys.Add(RefPose[BoneIndex].GetScale3D());

				InOutNewTrackToSkeletonMapTable.Insert(BoneIndex, NewTrackIndex);
				InOutNewTrackNames.Insert(BoneName, NewTrackIndex);

				check(InOutNewRawTracks.Num() == InOutNewTrackToSkeletonMapTable.Num());
			}

			return NewTrackIndex;
		};


		// NumFrames can't be zero (filtered earlier)
		const float Interval = GetIntervalPerKey(NumberOfSampledKeys, GetPlayLength());

		const FSmartNameMapping* NameMapping = CurSkeleton->GetSmartNameContainer(USkeleton::AnimTrackCurveMappingName);
		const FAnimationCurveData& AnimationCurveData = DataModel->GetCurveData();
		for (const FTransformCurve& Curve : AnimationCurveData.TransformCurves)
		{
			// find curves first, and then see what is index of this curve
			FName BoneName;

			if (Curve.GetCurveTypeFlag(AACF_Disabled) == false &&
				ensureAlways(NameMapping->GetName(Curve.Name.UID, BoneName)))
			{
				int32 TrackIndex = InOutNewTrackNames.Find(BoneName);

				// the animation data doesn't have this track, so insert it
				if (TrackIndex == INDEX_NONE)
				{
					TrackIndex = InsertNewBoneTrack(BoneName);
					// if it still didn't find, something went horribly wrong
					if (ensure(TrackIndex != INDEX_NONE) == false)
					{
						UE_LOG(LogAnimation, Warning, TEXT("Animation Baking : Error adding %s track."), *BoneName.ToString());
						// I can't do anything about it
						continue;
					}
				}

				// now modify data
				FRawAnimSequenceTrack& RawTrack = InOutNewRawTracks[TrackIndex];

				// since now we're editing keys, 
				// if 1 (which meant constant), just expands to # of frames
				if (RawTrack.PosKeys.Num() == 1)
				{
					FVector OneKey = RawTrack.PosKeys[0];
					RawTrack.PosKeys.Init(OneKey, NumberOfSampledKeys);
				}
				else
				{
					ensure(RawTrack.PosKeys.Num() == NumberOfSampledKeys);
				}

				if (RawTrack.RotKeys.Num() == 1)
				{
					FQuat OneKey = RawTrack.RotKeys[0];
					RawTrack.RotKeys.Init(OneKey, NumberOfSampledKeys);
				}
				else
				{
					ensure(RawTrack.RotKeys.Num() == NumberOfSampledKeys);
				}

				// although we don't allow edit of scale
				// it is important to consider scale when apply transform
				// so make sure this also is included
				if (RawTrack.ScaleKeys.Num() == 1)
				{
					FVector OneKey = RawTrack.ScaleKeys[0];
					RawTrack.ScaleKeys.Init(OneKey, NumberOfSampledKeys);
				}
				else
				{
					ensure(RawTrack.ScaleKeys.Num() == NumberOfSampledKeys);
				}

				// now we have all data ready to apply
				for (int32 KeyIndex = 0; KeyIndex < NumberOfSampledKeys; ++KeyIndex)
				{
					// now evaluate
					FTransform AdditiveTransform = Curve.Evaluate(KeyIndex * Interval, 1.0);
						FTransform LocalTransform(RawTrack.RotKeys[KeyIndex], RawTrack.PosKeys[KeyIndex], RawTrack.ScaleKeys[KeyIndex]);
						RawTrack.RotKeys[KeyIndex] = LocalTransform.GetRotation() * AdditiveTransform.GetRotation();
						RawTrack.PosKeys[KeyIndex] = LocalTransform.TransformPosition(AdditiveTransform.GetTranslation());
						RawTrack.ScaleKeys[KeyIndex] = LocalTransform.GetScale3D() * AdditiveTransform.GetScale3D();
				}

				// Apply PostProcess behaviour in-place
				UE::Anim::Compression::SanitizeRawAnimSequenceTrack(RawTrack);
			}
		}
	}
}

void UAnimSequence::AddKeyToSequence(float Time, const FName& BoneName, const FTransform& AdditiveTransform)
{
	// find if this already exists, then just add curve data only
	FName CurveName = BoneName;
	USkeleton * CurrentSkeleton = GetSkeleton();
	check (CurrentSkeleton);

	FSmartName NewCurveName;
	CurrentSkeleton->AddSmartNameAndModify(USkeleton::AnimTrackCurveMappingName, CurveName, NewCurveName);


	ValidateModel();

	UAnimDataController::FScopedBracket ScopedBracket(Controller, LOCTEXT("AddKeyToSequence_Bracket", "Adding key to sequence"));
	FAnimationCurveIdentifier TransformCurveId(NewCurveName, ERawCurveTrackTypes::RCT_Transform);
	Controller->AddCurve(TransformCurveId, AACF_DriveTrack | AACF_Editable);

	Controller->RemoveBoneTracksMissingFromSkeleton(CurrentSkeleton);

	const FTransformCurve* TransformCurve = DataModel->FindTransformCurve(TransformCurveId);
	check(TransformCurve);

	Controller->SetTransformCurveKey(TransformCurveId, Time, AdditiveTransform);
}

void UAnimSequence::RefreshTrackMapFromAnimTrackNames()
{
	ValidateModel();
	const USkeleton * MySkeleton = GetSkeleton();
	Controller->RemoveBoneTracksMissingFromSkeleton(MySkeleton);
}

uint8* UAnimSequence::FindSyncMarkerPropertyData(int32 SyncMarkerIndex, FArrayProperty*& ArrayProperty)
{
	ArrayProperty = NULL;

	if (AuthoredSyncMarkers.IsValidIndex(SyncMarkerIndex))
	{
		return FindArrayProperty(TEXT("AuthoredSyncMarkers"), ArrayProperty, SyncMarkerIndex);
	}
	return NULL;
}

bool UAnimSequence::CreateAnimation(USkeletalMesh* Mesh)
{
	// create animation from Mesh's ref pose
	if (Mesh)
	{
		ValidateModel();
		const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();

		UAnimDataController::FScopedBracket ScopedBracket(Controller, LOCTEXT("CreateAnimationSkeletalMesh_Bracket", "Creating Animation Sequence based up Skeletal Mesh"));
		ResetAnimation();

		Controller->SetPlayLength(MINIMUM_ANIMATION_LENGTH);
		Controller->SetFrameRate(FFrameRate(1.f / MINIMUM_ANIMATION_LENGTH, 1));

		const int32 NumBones = RefSkeleton.GetRawBoneNum();

		const TArray<FTransform>& RefBonePose = RefSkeleton.GetRawRefBonePose();

		check(RefBonePose.Num() == NumBones);

		for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
		{
			const FName& BoneName = RefSkeleton.GetBoneName(BoneIndex);
			Controller->AddBoneTrack(BoneName);
			Controller->SetBoneTrackKeys(BoneName, { RefBonePose[BoneIndex].GetTranslation() }, { RefBonePose[BoneIndex].GetRotation() }, { RefBonePose[BoneIndex].GetScale3D() });
		}

		Controller->NotifyPopulated();
		

		return true;
	}

	return false;
}

bool UAnimSequence::CreateAnimation(USkeletalMeshComponent* MeshComponent)
{
	if(MeshComponent && MeshComponent->SkeletalMesh)
	{
		USkeletalMesh * Mesh = MeshComponent->SkeletalMesh;

		ValidateModel();
		const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();

		UAnimDataController::FScopedBracket ScopedBracket(Controller, LOCTEXT("CreateAnimationSkeletalMeshComponent_Bracket", "Creating Animation Sequence based up Skeletal Mesh Component"));
		ResetAnimation();

		Controller->SetPlayLength(MINIMUM_ANIMATION_LENGTH);
		Controller->SetFrameRate(FFrameRate(1.f / MINIMUM_ANIMATION_LENGTH, 1));

		const int32 NumBones = RefSkeleton.GetRawBoneNum();

		const TArray<FTransform> BoneSpaceTransforms = MeshComponent->GetBoneSpaceTransforms();

		check(BoneSpaceTransforms.Num() >= NumBones);

		for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
		{
			const FName& BoneName = RefSkeleton.GetBoneName(BoneIndex);
			Controller->AddBoneTrack(BoneName);
			Controller->SetBoneTrackKeys(BoneName, { BoneSpaceTransforms[BoneIndex].GetTranslation() }, { BoneSpaceTransforms[BoneIndex].GetRotation() }, { BoneSpaceTransforms[BoneIndex].GetScale3D() });
		}

		Controller->NotifyPopulated();
		return true;
	}

	return false;
}

bool UAnimSequence::CreateAnimation(UAnimSequence* Sequence)
{
	if(Sequence)
	{
		ResetAnimation();

		CopyDataModel(Sequence->GetDataModel());
		Controller->SetModel(DataModel);
		Notifies = Sequence->Notifies;
		AnimNotifyTracks = Sequence->AnimNotifyTracks;

		Controller->NotifyPopulated();

		return true;
	}
	
	return false;
}

#endif

void UAnimSequence::RefreshCacheData()
{
	SortSyncMarkers();
#if WITH_EDITOR
	for (int32 TrackIndex = 0; TrackIndex < AnimNotifyTracks.Num(); ++TrackIndex)
	{
		AnimNotifyTracks[TrackIndex].SyncMarkers.Empty();
	}
	for (FAnimSyncMarker& SyncMarker : AuthoredSyncMarkers)
	{
		const int32 TrackIndex = SyncMarker.TrackIndex;
		if (AnimNotifyTracks.IsValidIndex(TrackIndex))
		{
			AnimNotifyTracks[TrackIndex].SyncMarkers.Add(&SyncMarker);
		}
		else
		{
			// This should not happen, but if it does we must find somewhere else to add it
			ensureMsgf(0, TEXT("AnimNotifyTrack: Wrong indices found"));
			AnimNotifyTracks[0].SyncMarkers.Add(&SyncMarker);
			SyncMarker.TrackIndex = 0;
		}
	}
#endif
	Super::RefreshCacheData();
}

int32 UAnimSequence::GetNumberOfSampledKeys() const 
{ 
#if WITH_EDITOR
	return NumberOfSampledKeys;
#else
	return CompressedData.CompressedDataStructure->CompressedNumberOfKeys;
#endif
}

void UAnimSequence::EvaluateCurveData(FBlendedCurve& OutCurve, float CurrentTime, bool bForceUseRawData) const
{
	SCOPE_CYCLE_COUNTER(STAT_AnimSeq_EvalCurveData);

	if (OutCurve.NumValidCurveCount == 0)
	{
		return;
	}

	if (bUseRawDataOnly || bForceUseRawData || !IsCurveCompressedDataValid())
	{
		Super::EvaluateCurveData(OutCurve, CurrentTime, bForceUseRawData);
	}
	else
	{
		CSV_SCOPED_TIMING_STAT(Animation, EvaluateCurveData);
		CompressedData.CurveCompressionCodec->DecompressCurves(CompressedData, OutCurve, CurrentTime);
	}
}

float UAnimSequence::EvaluateCurveData(SmartName::UID_Type CurveUID, float CurrentTime, bool bForceUseRawData) const
{
	SCOPE_CYCLE_COUNTER(STAT_AnimSeq_EvalCurveData);

	if (bUseRawDataOnly || bForceUseRawData || !IsCurveCompressedDataValid())
	{
		return Super::EvaluateCurveData(CurveUID, CurrentTime, bForceUseRawData);
	}
	else
	{
		return CompressedData.CurveCompressionCodec->DecompressCurve(CompressedData, CurveUID, CurrentTime);
	}
}

bool UAnimSequence::HasCurveData(SmartName::UID_Type CurveUID, bool bForceUseRawData) const
{
	if (bUseRawDataOnly || bForceUseRawData || !IsCurveCompressedDataValid())
	{
		return Super::HasCurveData(CurveUID, bForceUseRawData);
	}

	for (const FSmartName& CurveName : CompressedData.CompressedCurveNames)
	{
		if (CurveName.UID == CurveUID)
		{
			return true;
		}
	}

	return false;
}

void UAnimSequence::RefreshSyncMarkerDataFromAuthored()
{
#if WITH_EDITOR
	MarkerDataUpdateCounter++;
#endif

	if (AuthoredSyncMarkers.Num() > 0)
	{
		UniqueMarkerNames.Reset();
		UniqueMarkerNames.Reserve(AuthoredSyncMarkers.Num());

		const FAnimSyncMarker* PreviousMarker = nullptr;
		for (const FAnimSyncMarker& Marker : AuthoredSyncMarkers)
		{
			UniqueMarkerNames.AddUnique(Marker.MarkerName);
			PreviousMarker = &Marker;
		}
	}
	else
	{
		UniqueMarkerNames.Empty();
	}

#if WITH_EDITOR
	check(IsInGameThread());

	// Update blend spaces that may be referencing us
	for(TObjectIterator<UBlendSpace> It; It; ++It)
	{
		if(!It->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad))
		{
			It->RuntimeValidateMarkerData();
		}
	}
#endif
}

bool IsMarkerValid(const FAnimSyncMarker* Marker, bool bLooping, const TArray<FName>& ValidMarkerNames)
{
	return (Marker == nullptr && !bLooping) || (Marker && ValidMarkerNames.Contains(Marker->MarkerName));
}

void UAnimSequence::AdvanceMarkerPhaseAsLeader(bool bLooping, float MoveDelta, const TArray<FName>& ValidMarkerNames, float& CurrentTime, FMarkerPair& PrevMarker, FMarkerPair& NextMarker, TArray<FPassedMarker>& MarkersPassed) const
{
	check(MoveDelta != 0.f);
	const bool bPlayingForwards = MoveDelta > 0.f;
	float CurrentMoveDelta = MoveDelta;

	// Hard to reproduce issue triggering this, ensure & clamp for now
	ensureMsgf(CurrentTime >= 0.f && CurrentTime <= GetPlayLength(), TEXT("Current time inside of AdvanceMarkerPhaseAsLeader is out of range %.3f of 0.0 to %.3f\n    Sequence: %s"), CurrentTime, GetPlayLength(), *GetFullName());

	CurrentTime = FMath::Clamp(CurrentTime, 0.f, GetPlayLength());

	if (bPlayingForwards)
	{
		while (true)
		{
			if (NextMarker.MarkerIndex == -1)
			{
				float PrevCurrentTime = CurrentTime;
				CurrentTime = FMath::Min(CurrentTime + CurrentMoveDelta, GetPlayLength());
				NextMarker.TimeToMarker = GetPlayLength() - CurrentTime;
				PrevMarker.TimeToMarker -= CurrentTime - PrevCurrentTime; //Add how far we moved to distance from previous marker
				break;
			}
			const FAnimSyncMarker& NextSyncMarker = AuthoredSyncMarkers[NextMarker.MarkerIndex];
			checkSlow(ValidMarkerNames.Contains(NextSyncMarker.MarkerName));

			if (CurrentMoveDelta > NextMarker.TimeToMarker)
			{
				CurrentTime = NextSyncMarker.Time;
				CurrentMoveDelta -= NextMarker.TimeToMarker;

				PrevMarker.MarkerIndex = NextMarker.MarkerIndex;
				PrevMarker.TimeToMarker = -CurrentMoveDelta;

				int32 PassedMarker = MarkersPassed.Add(FPassedMarker());
				MarkersPassed[PassedMarker].PassedMarkerName = NextSyncMarker.MarkerName;
				MarkersPassed[PassedMarker].DeltaTimeWhenPassed = CurrentMoveDelta;

				float MarkerTimeOffset = 0.f;
				do
				{
					++NextMarker.MarkerIndex;
					if (NextMarker.MarkerIndex >= AuthoredSyncMarkers.Num())
					{
						if (!bLooping)
						{
							NextMarker.MarkerIndex = -1;
							break;
						}
						NextMarker.MarkerIndex = 0;
						MarkerTimeOffset = GetPlayLength();
					}
				} while (!ValidMarkerNames.Contains(AuthoredSyncMarkers[NextMarker.MarkerIndex].MarkerName));
				if (NextMarker.MarkerIndex != -1)
				{
					NextMarker.TimeToMarker = MarkerTimeOffset + AuthoredSyncMarkers[NextMarker.MarkerIndex].Time - CurrentTime;
				}
			}
			else
			{
				CurrentTime = FMath::Fmod(CurrentTime + CurrentMoveDelta, GetPlayLength());
				if (CurrentTime < 0.f)
				{
					CurrentTime += GetPlayLength();
				}
				NextMarker.TimeToMarker -= CurrentMoveDelta;
				PrevMarker.TimeToMarker -= CurrentMoveDelta;
				break;
			}
		}
	}
	else
	{
		while (true)
		{
			if (PrevMarker.MarkerIndex == -1)
			{
				float PrevCurrentTime = CurrentTime;
				CurrentTime = FMath::Max(CurrentTime + CurrentMoveDelta, 0.f);
				PrevMarker.TimeToMarker = CurrentTime;
				NextMarker.TimeToMarker -= CurrentTime - PrevCurrentTime; //Add how far we moved to distance from previous marker
				break;
			}
			const FAnimSyncMarker& PrevSyncMarker = AuthoredSyncMarkers[PrevMarker.MarkerIndex];
			checkSlow(ValidMarkerNames.Contains(PrevSyncMarker.MarkerName));
			
			if (CurrentMoveDelta < PrevMarker.TimeToMarker)
			{
				CurrentTime = PrevSyncMarker.Time;
				CurrentMoveDelta -= PrevMarker.TimeToMarker;

				NextMarker.MarkerIndex = PrevMarker.MarkerIndex;
				NextMarker.TimeToMarker = -CurrentMoveDelta;

				int32 PassedMarker = MarkersPassed.Add(FPassedMarker());
				MarkersPassed[PassedMarker].PassedMarkerName = PrevSyncMarker.MarkerName;
				MarkersPassed[PassedMarker].DeltaTimeWhenPassed = CurrentMoveDelta;

				float MarkerTimeOffset = 0.f;
				do
				{
					--PrevMarker.MarkerIndex;
					if (PrevMarker.MarkerIndex < 0)
					{
						if (!bLooping)
						{
							PrevMarker.MarkerIndex = -1;
							break;
						}
						PrevMarker.MarkerIndex = AuthoredSyncMarkers.Num() - 1;
						MarkerTimeOffset -= GetPlayLength();
					}
				} while (!ValidMarkerNames.Contains(AuthoredSyncMarkers[PrevMarker.MarkerIndex].MarkerName));
				if (PrevMarker.MarkerIndex != -1)
				{
					PrevMarker.TimeToMarker = MarkerTimeOffset + AuthoredSyncMarkers[PrevMarker.MarkerIndex].Time - CurrentTime;
				}
			}
			else
			{
				CurrentTime = FMath::Fmod(CurrentTime + CurrentMoveDelta, GetPlayLength());
				if (CurrentTime < 0.f)
				{
					CurrentTime += GetPlayLength();
				}
				PrevMarker.TimeToMarker -= CurrentMoveDelta;
				NextMarker.TimeToMarker -= CurrentMoveDelta;
				break;
			}
		}
	}

	check(CurrentTime >= 0.f && CurrentTime <= GetPlayLength());
}

void AdvanceMarkerForwards(int32& Marker, FName MarkerToFind, bool bLooping, const TArray<FAnimSyncMarker>& AuthoredSyncMarkers)
{
	int32 MaxIterations = AuthoredSyncMarkers.Num();
	while ((AuthoredSyncMarkers[Marker].MarkerName != MarkerToFind) && (--MaxIterations >= 0))
	{
		++Marker;
		if (Marker == AuthoredSyncMarkers.Num() && !bLooping)
		{
			break;
		}
		Marker %= AuthoredSyncMarkers.Num();
	}

	if (!AuthoredSyncMarkers.IsValidIndex(Marker) || (AuthoredSyncMarkers[Marker].MarkerName != MarkerToFind))
	{
		Marker = MarkerIndexSpecialValues::AnimationBoundary;
	}
}

int32 MarkerCounterSpaceTransform(int32 MaxMarker, int32 Source)
{
	return MaxMarker - 1 - Source;
}

void AdvanceMarkerBackwards(int32& Marker, FName MarkerToFind, bool bLooping, const TArray<FAnimSyncMarker>& AuthoredSyncMarkers)
{
	int32 MaxIterations = AuthoredSyncMarkers.Num();
	const int32 MarkerMax = AuthoredSyncMarkers.Num();
	int32 Counter = MarkerCounterSpaceTransform(MarkerMax, Marker);
	while ((AuthoredSyncMarkers[Marker].MarkerName != MarkerToFind) && (--MaxIterations >= 0))
	{
		if ((Marker == 0) && !bLooping)
		{
			break;
		}
		Counter = (Counter + 1) % MarkerMax;
		Marker = MarkerCounterSpaceTransform(MarkerMax, Counter);
	}

	if (!AuthoredSyncMarkers.IsValidIndex(Marker) || (AuthoredSyncMarkers[Marker].MarkerName != MarkerToFind))
	{
		Marker = MarkerIndexSpecialValues::AnimationBoundary;
	}
}

bool MarkerMatchesPosition(const UAnimSequence* Sequence, int32 MarkerIndex, FName CorrectMarker)
{
	checkf(MarkerIndex != MarkerIndexSpecialValues::Unitialized, TEXT("Uninitialized marker supplied to MarkerMatchesPosition. Anim: %s Expecting marker %s (Added to help debug Jira OR-9675)"), *Sequence->GetName(), *CorrectMarker.ToString());
	return MarkerIndex == MarkerIndexSpecialValues::AnimationBoundary || CorrectMarker == Sequence->AuthoredSyncMarkers[MarkerIndex].MarkerName;
}

void UAnimSequence::ValidateCurrentPosition(const FMarkerSyncAnimPosition& Position, bool bPlayingForwards, bool bLooping, float&CurrentTime, FMarkerPair& PreviousMarker, FMarkerPair& NextMarker) const
{
	if (bPlayingForwards)
	{
		if (!MarkerMatchesPosition(this, PreviousMarker.MarkerIndex, Position.PreviousMarkerName))
		{
			AdvanceMarkerForwards(PreviousMarker.MarkerIndex, Position.PreviousMarkerName, bLooping, AuthoredSyncMarkers);
			NextMarker.MarkerIndex = (PreviousMarker.MarkerIndex + 1);
			if(NextMarker.MarkerIndex >= AuthoredSyncMarkers.Num())
			{
				NextMarker.MarkerIndex = bLooping ? NextMarker.MarkerIndex % AuthoredSyncMarkers.Num() : MarkerIndexSpecialValues::AnimationBoundary;
			}
		}

		if (!MarkerMatchesPosition(this, NextMarker.MarkerIndex, Position.NextMarkerName))
		{
			AdvanceMarkerForwards(NextMarker.MarkerIndex, Position.NextMarkerName, bLooping, AuthoredSyncMarkers);
		}
	}
	else
	{
		const int32 MarkerRange = AuthoredSyncMarkers.Num();
		if (!MarkerMatchesPosition(this, NextMarker.MarkerIndex, Position.NextMarkerName))
		{
			AdvanceMarkerBackwards(NextMarker.MarkerIndex, Position.NextMarkerName, bLooping, AuthoredSyncMarkers);
			if(NextMarker.MarkerIndex == MarkerIndexSpecialValues::AnimationBoundary || (NextMarker.MarkerIndex == 0 && bLooping))
			{
				PreviousMarker.MarkerIndex = AuthoredSyncMarkers.Num() - 1;
			}
			else
			{
				PreviousMarker.MarkerIndex = NextMarker.MarkerIndex - 1;
			}
		}
		if (!MarkerMatchesPosition(this, PreviousMarker.MarkerIndex, Position.PreviousMarkerName))
		{
			AdvanceMarkerBackwards(PreviousMarker.MarkerIndex, Position.PreviousMarkerName, bLooping, AuthoredSyncMarkers);
		}
	}

	checkSlow(MarkerMatchesPosition(this, PreviousMarker.MarkerIndex, Position.PreviousMarkerName));
	checkSlow(MarkerMatchesPosition(this, NextMarker.MarkerIndex, Position.NextMarkerName));

	// Only reset position if we found valid markers. Otherwise stay where we are to not pop.
	if ((PreviousMarker.MarkerIndex != MarkerIndexSpecialValues::AnimationBoundary) && (NextMarker.MarkerIndex != MarkerIndexSpecialValues::AnimationBoundary))
	{
		CurrentTime = GetCurrentTimeFromMarkers(PreviousMarker, NextMarker, Position.PositionBetweenMarkers);
	}
}

bool UAnimSequence::UseRawDataForPoseExtraction(const FBoneContainer& RequiredBones) const
{
	return bUseRawDataOnly || (GetSkeletonVirtualBoneGuid() != GetSkeleton()->GetVirtualBoneGuid()) || RequiredBones.GetDisableRetargeting() || RequiredBones.ShouldUseRawData() || RequiredBones.ShouldUseSourceData();
}

void UAnimSequence::GetCustomAttributes(FAnimationPoseData& OutAnimationPoseData, const FAnimExtractContext& ExtractionContext, bool bUseRawData) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_GetCustomAttributes);

	const FBoneContainer& RequiredBones = OutAnimationPoseData.GetPose().GetBoneContainer();
	FStackCustomAttributes& OutAttributes = OutAnimationPoseData.GetAttributes();

#if WITH_EDITOR
	if (bUseRawData)
	{
		for (const FCustomAttributePerBoneData& BoneAttributes : PerBoneCustomAttributeData)
		{
			const FCompactPoseBoneIndex PoseBoneIndex = RequiredBones.GetCompactPoseIndexFromSkeletonIndex(BoneAttributes.BoneTreeIndex);

			for (const FCustomAttribute& Attribute : BoneAttributes.Attributes)
			{
				FCustomAttributesRuntime::GetAttributeValue(OutAttributes, PoseBoneIndex, Attribute, ExtractionContext);
			}
		}
	}
	else
#endif // WITH_EDITOR
	{
		for (const FBakedCustomAttributePerBoneData& BakedBoneAttributes : BakedPerBoneCustomAttributeData)
		{
			const FCompactPoseBoneIndex PoseBoneIndex = RequiredBones.GetCompactPoseIndexFromSkeletonIndex(BakedBoneAttributes.BoneTreeIndex);
			for (const FBakedFloatCustomAttribute& Attribute : BakedBoneAttributes.FloatAttributes)
			{
				const ECustomAttributeBlendType BlendType = FCustomAttributesRuntime::GetAttributeBlendType(Attribute.AttributeName);
				const float Value = Attribute.FloatCurve.Eval(ExtractionContext.CurrentTime);
				OutAttributes.AddBoneAttribute<float>(PoseBoneIndex, Attribute.AttributeName, BlendType, Value);
			}

			for (const FBakedIntegerCustomAttribute& Attribute : BakedBoneAttributes.IntAttributes)
			{
				const ECustomAttributeBlendType BlendType = FCustomAttributesRuntime::GetAttributeBlendType(Attribute.AttributeName);
				const int32 Value = Attribute.IntCurve.Evaluate(ExtractionContext.CurrentTime);
				OutAttributes.AddBoneAttribute<int32>(PoseBoneIndex, Attribute.AttributeName, BlendType, Value);
			}

			for (const FBakedStringCustomAttribute& Attribute : BakedBoneAttributes.StringAttributes)
			{
				static const FString DefaultValue = TEXT("");
				const FString Value = Attribute.StringCurve.Eval(ExtractionContext.CurrentTime, DefaultValue);
				OutAttributes.AddBoneAttribute<FString>(PoseBoneIndex, Attribute.AttributeName, ECustomAttributeBlendType::Override, Value);
			}
		}
	}
}

#if WITH_EDITOR
void UAnimSequence::RemoveCustomAttribute(const FName& BoneName, const FName& AttributeName)
{
	FCustomAttributePerBoneData* DataPtr = PerBoneCustomAttributeData.FindByPredicate([BoneName, this](FCustomAttributePerBoneData& Attribute)
	{
		return Attribute.BoneTreeIndex == GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(BoneName);
	});

	if (DataPtr)
	{
		const int32 NumRemoved = DataPtr->Attributes.RemoveAll([AttributeName](FCustomAttribute& Attribute)
		{
			return Attribute.Name == AttributeName;
		});

		// In case there are no custom attributes left for this bone, remove the wrapping structure entry as well
		if (DataPtr->Attributes.Num() == 0)
		{
			ensure(PerBoneCustomAttributeData.RemoveAll([DataPtr](FCustomAttributePerBoneData& Attribute)
			{
				return Attribute.BoneTreeIndex == DataPtr->BoneTreeIndex;
			}) == 1);
		}

		if (NumRemoved)
		{
			// Update the Guid used to keep track of raw / baked versions
			CustomAttributesGuid = FGuid::NewGuid();
		}
	}
}

void UAnimSequence::RemoveAllCustomAttributesForBone(const FName& BoneName)
{
	const USkeleton* CurrentSkeleton = GetSkeleton();

	if (CurrentSkeleton)
	{
		const int32 BoneIndex = CurrentSkeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);

		if (BoneIndex != INDEX_NONE)
		{
			const int32 NumRemoved = PerBoneCustomAttributeData.RemoveAll([BoneIndex](const FCustomAttributePerBoneData PerBoneData)
			{
				return PerBoneData.BoneTreeIndex == BoneIndex;
			});

			if (NumRemoved)
			{
				// Update the Guid used to keep track of raw / baked versions
				CustomAttributesGuid = FGuid::NewGuid();
			}
		}
	}
}

void UAnimSequence::RemoveAllCustomAttributes()
{
	if (PerBoneCustomAttributeData.Num())
	{
		// Update the Guid used to keep track of raw / baked versions
		CustomAttributesGuid = FGuid::NewGuid();
	}

	PerBoneCustomAttributeData.Empty();
}

void UAnimSequence::GetCustomAttributesForBone(const FName& BoneName, TArray<FCustomAttribute>& OutAttributes) const
{
	const USkeleton* CurrentSkeleton = GetSkeleton();

	if (CurrentSkeleton)
	{
		const int32 BoneIndex = CurrentSkeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);

		if (BoneIndex != INDEX_NONE)
		{
			for (const FCustomAttributePerBoneData& PerBoneData : PerBoneCustomAttributeData)
			{
				if (PerBoneData.BoneTreeIndex == BoneIndex)
				{
					OutAttributes.Append(PerBoneData.Attributes);
				}
			}
		}
	}
}

// Helper functionality to populate a curve by sampling the custom attribute data
template<typename DataType, typename CurveType>
void ConvertAttributeToAdditive(const FCustomAttribute& AdditiveAttribute, const FCustomAttribute& RefAttribute, CurveType& InOutCurve, float SamplingTime, int32 NumberOfFrames, TFunctionRef<float(float Time)> GetReferenceTime)
{
	for (int32 Frame = 0; Frame < NumberOfFrames; ++Frame)
	{
		const float CurrentFrameTime = Frame * SamplingTime;

		DataType AdditiveValue;
		FCustomAttributesRuntime::GetAttributeValue(AdditiveAttribute, CurrentFrameTime, AdditiveValue);

		DataType RefValue;
		FCustomAttributesRuntime::GetAttributeValue(RefAttribute, GetReferenceTime(CurrentFrameTime), RefValue);

		const DataType Value = RefValue - AdditiveValue;
		InOutCurve.AddKey(CurrentFrameTime, Value);
	}
}

void UAnimSequence::SynchronousCustomAttributesCompression()
{
	// If we are additive, we'll need to sample the base pose (against we're additive) and subtract the attributes from the base ones
	const bool bShouldSampleBasePose = IsValidAdditive() && RefPoseType != ABPT_RefPose;
	
	BakedPerBoneCustomAttributeData.Empty(PerBoneCustomAttributeData.Num());

	auto ProcessCustomAttribute = [this](const FCustomAttribute& Attribute, FBakedCustomAttributePerBoneData& BakedBoneAttributes)
	{		
		switch (static_cast<EVariantTypes>(Attribute.VariantType))
		{
			case EVariantTypes::Float:
			{
				FBakedFloatCustomAttribute& BakedFloatAttribute = BakedBoneAttributes.FloatAttributes.AddDefaulted_GetRef();
				BakedFloatAttribute.AttributeName = Attribute.Name;

				FSimpleCurve& FloatCurve = BakedFloatAttribute.FloatCurve;

				TArray<FSimpleCurveKey> Keys;
				for (int32 KeyIndex = 0; KeyIndex < Attribute.Times.Num(); ++KeyIndex)
				{
					const FVariant& VariantValue = Attribute.Values[KeyIndex];
					FloatCurve.AddKey(Attribute.Times[KeyIndex], VariantValue.GetValue<float>());
				}

				FloatCurve.SetDefaultValue(FloatCurve.GetFirstKey().Value);
				FloatCurve.RemoveRedundantKeys(0.f);
				break;
			}

			case EVariantTypes::Int32:
			{
				FBakedIntegerCustomAttribute& BakedIntAttribute = BakedBoneAttributes.IntAttributes.AddDefaulted_GetRef();
				BakedIntAttribute.AttributeName = Attribute.Name;

				FIntegralCurve& IntCurve = BakedIntAttribute.IntCurve;
				for (int32 KeyIndex = 0; KeyIndex < Attribute.Times.Num(); ++KeyIndex)
				{
					const FVariant& VariantValue = Attribute.Values[KeyIndex];
					IntCurve.AddKey(Attribute.Times[KeyIndex], VariantValue.GetValue<int32>());
				}

				IntCurve.SetDefaultValue(IntCurve.GetKey(IntCurve.GetFirstKeyHandle()).Value);
				IntCurve.RemoveRedundantKeys();
				break;
			}

			case EVariantTypes::String:
			{
				FBakedStringCustomAttribute& BakedStringAttribute = BakedBoneAttributes.StringAttributes.AddDefaulted_GetRef();
				BakedStringAttribute.AttributeName = Attribute.Name;

				FStringCurve& StringCurve = BakedStringAttribute.StringCurve;
				for (int32 KeyIndex = 0; KeyIndex < Attribute.Times.Num(); ++KeyIndex)
				{
					const FVariant& VariantValue = Attribute.Values[KeyIndex];
					StringCurve.AddKey(Attribute.Times[KeyIndex], VariantValue.GetValue<FString>());
				}

				StringCurve.SetDefaultValue(StringCurve.GetKey(StringCurve.GetFirstKeyHandle()).Value);
				StringCurve.RemoveRedundantKeys();
				break;
			}

			default:
			{
				ensureMsgf(false, TEXT("Invalid data variant type for custom attribute, only int32, float and FString are currently supported"));
				break;
			}
		}
	};

	if (bShouldSampleBasePose)
	{
		// Behaviour for determining the time to sample the base pose attributes
		auto GetBasePoseTimeToSample = [this](float InTime) -> float
		{
			float BasePoseTime = 0.f;

			if (RefPoseType == ABPT_AnimScaled)
			{
				const float CurrentSequenceLength = GetPlayLength();
				const float Fraction = (CurrentSequenceLength > 0.f) ? FMath::Clamp<float>(InTime / CurrentSequenceLength, 0.f, 1.f) : 0.f;
				BasePoseTime = RefPoseSeq->GetPlayLength() * Fraction;
			}
			else if (RefPoseType == ABPT_AnimFrame)
			{
				const float Fraction = (RefPoseSeq->GetNumberOfSampledKeys() > 0) ? FMath::Clamp<float>((float)RefFrameIndex / (float)RefPoseSeq->GetNumberOfSampledKeys(), 0.f, 1.f) : 0.f;
				BasePoseTime = RefPoseSeq->GetPlayLength() * Fraction;

			}

			return BasePoseTime;
		};

		const FReferenceSkeleton& RefSkeleton = GetSkeleton()->GetReferenceSkeleton();

		// Helper struct to match sample timings with regular additive baking
		FByFramePoseEvalContext EvalContext(this);
		for (const FCustomAttributePerBoneData& BoneAttributes : PerBoneCustomAttributeData)
		{
			FBakedCustomAttributePerBoneData& BakedBoneAttributes = BakedPerBoneCustomAttributeData.AddDefaulted_GetRef();
			BakedBoneAttributes.BoneTreeIndex = BoneAttributes.BoneTreeIndex;

			TArray<FCustomAttribute> ReferenceSequenceAttributes;			
			RefPoseSeq->GetCustomAttributesForBone(RefSkeleton.GetBoneName(BoneAttributes.BoneTreeIndex), ReferenceSequenceAttributes);

			// Check whether or not the base sequence has any attributes
			if (!ReferenceSequenceAttributes.Num())
			{
				for (const FCustomAttribute& Attribute : BoneAttributes.Attributes)
				{
					ProcessCustomAttribute(Attribute, BakedBoneAttributes);
				}
			}
			else
			{
				for (const FCustomAttribute& Attribute : BoneAttributes.Attributes)
				{
					// Try and find equivalent in reference sequence
					const FCustomAttribute* RefAttribute = ReferenceSequenceAttributes.FindByPredicate([Attribute](const FCustomAttribute& Attr)
					{	
						return Attribute.Name == Attr.Name && Attribute.VariantType == Attr.VariantType;
					});

					if (RefAttribute)
					{
						switch (static_cast<EVariantTypes>(Attribute.VariantType))
						{
							case EVariantTypes::Float:
							{
								FBakedFloatCustomAttribute& BakedFloatAttribute = BakedBoneAttributes.FloatAttributes.AddDefaulted_GetRef();
								BakedFloatAttribute.AttributeName = Attribute.Name;

								FSimpleCurve& FloatCurve = BakedFloatAttribute.FloatCurve;
								ConvertAttributeToAdditive<float, FSimpleCurve>(Attribute, *RefAttribute, FloatCurve, EvalContext.IntervalTime, NumberOfSampledKeys, GetBasePoseTimeToSample);
								FloatCurve.RemoveRedundantKeys(0.f);

								break;
							}

							case EVariantTypes::Int32:
							{
								FBakedIntegerCustomAttribute& BakedIntAttribute = BakedBoneAttributes.IntAttributes.AddDefaulted_GetRef();
								BakedIntAttribute.AttributeName = Attribute.Name;

								FIntegralCurve& IntCurve = BakedIntAttribute.IntCurve;
								ConvertAttributeToAdditive<int32, FIntegralCurve>(Attribute, *RefAttribute, IntCurve, EvalContext.IntervalTime, NumberOfSampledKeys, GetBasePoseTimeToSample);
								IntCurve.RemoveRedundantKeys();
							
								break;
							}

							case EVariantTypes::String:
							{
								ProcessCustomAttribute(Attribute, BakedBoneAttributes);
								break;
							}
						}
					}
					else
					{
						ProcessCustomAttribute(Attribute, BakedBoneAttributes);
					}					
				}
			}
		}
	}
	else
	{
		for (const FCustomAttributePerBoneData& BoneAttributes : PerBoneCustomAttributeData)
		{
			FBakedCustomAttributePerBoneData& BakedBoneAttributes = BakedPerBoneCustomAttributeData.AddDefaulted_GetRef();
			BakedBoneAttributes.BoneTreeIndex = BoneAttributes.BoneTreeIndex;

			for (const FCustomAttribute& Attribute : BoneAttributes.Attributes)
			{
				ProcessCustomAttribute(Attribute, BakedBoneAttributes);
			}
		}
	}

	// Match baked/raw attributes guid
	BakedCustomAttributesGuid = CustomAttributesGuid;
}

FCustomAttributePerBoneData& UAnimSequence::FindOrAddCustomAttributeForBone(const FName& BoneName)
{
	FCustomAttributePerBoneData* DataPtr = PerBoneCustomAttributeData.FindByPredicate([BoneName, this](FCustomAttributePerBoneData& Attribute)
	{
		return Attribute.BoneTreeIndex == GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(BoneName);
	});

	return DataPtr ? *DataPtr : PerBoneCustomAttributeData.AddDefaulted_GetRef();
}
#endif // WITH_EDITOR

void UAnimSequence::AdvanceMarkerPhaseAsFollower(const FMarkerTickContext& Context, float DeltaRemaining, bool bLooping, float& CurrentTime, FMarkerPair& PreviousMarker, FMarkerPair& NextMarker) const
{
	const bool bPlayingForwards = DeltaRemaining > 0.f;

	ValidateCurrentPosition(Context.GetMarkerSyncStartPosition(), bPlayingForwards, bLooping, CurrentTime, PreviousMarker, NextMarker);
	if (bPlayingForwards)
	{
		int32 PassedMarkersIndex = 0;
		do
		{
			if (NextMarker.MarkerIndex == -1)
			{
				check(!bLooping || Context.GetMarkerSyncEndPosition().NextMarkerName == NAME_None); // shouldnt have an end of anim marker if looping
				CurrentTime = FMath::Min(CurrentTime + DeltaRemaining, GetPlayLength());
				break;
			}
			else if (PassedMarkersIndex < Context.MarkersPassedThisTick.Num())
			{
				PreviousMarker.MarkerIndex = NextMarker.MarkerIndex;
				checkSlow(NextMarker.MarkerIndex != -1);
				const FPassedMarker& PassedMarker = Context.MarkersPassedThisTick[PassedMarkersIndex];
				AdvanceMarkerForwards(NextMarker.MarkerIndex, PassedMarker.PassedMarkerName, bLooping, AuthoredSyncMarkers);
				if (NextMarker.MarkerIndex == -1)
				{
					DeltaRemaining = PassedMarker.DeltaTimeWhenPassed;
				}
				++PassedMarkersIndex;
			}
		} while (PassedMarkersIndex < Context.MarkersPassedThisTick.Num());

		const FMarkerSyncAnimPosition& End = Context.GetMarkerSyncEndPosition();
		
		if (End.NextMarkerName == NAME_None)
		{
			NextMarker.MarkerIndex = -1;
		}

		if (NextMarker.MarkerIndex != -1 && Context.MarkersPassedThisTick.Num() > 0)
		{
			AdvanceMarkerForwards(NextMarker.MarkerIndex, End.NextMarkerName, bLooping, AuthoredSyncMarkers);
		}

		//Validation
		if (NextMarker.MarkerIndex != -1)
		{
			check(AuthoredSyncMarkers[NextMarker.MarkerIndex].MarkerName == End.NextMarkerName);
		}

		// End Validation
		// Only reset position if we found valid markers. Otherwise stay where we are to not pop.
		if ((PreviousMarker.MarkerIndex != MarkerIndexSpecialValues::AnimationBoundary) && (NextMarker.MarkerIndex != MarkerIndexSpecialValues::AnimationBoundary))
		{
			CurrentTime = GetCurrentTimeFromMarkers(PreviousMarker, NextMarker, End.PositionBetweenMarkers);
		}
	}
	else
	{
		int32 PassedMarkersIndex = 0;
		do
		{
			if (PreviousMarker.MarkerIndex == -1)
			{
				check(!bLooping || Context.GetMarkerSyncEndPosition().PreviousMarkerName == NAME_None); // shouldn't have an end of anim marker if looping
				CurrentTime = FMath::Max(CurrentTime + DeltaRemaining, 0.f);
				break;
			}
			else if (PassedMarkersIndex < Context.MarkersPassedThisTick.Num())
			{
				NextMarker.MarkerIndex = PreviousMarker.MarkerIndex;
				checkSlow(PreviousMarker.MarkerIndex != -1);
				const FPassedMarker& PassedMarker = Context.MarkersPassedThisTick[PassedMarkersIndex];
				AdvanceMarkerBackwards(PreviousMarker.MarkerIndex, PassedMarker.PassedMarkerName, bLooping, AuthoredSyncMarkers);
				if (PreviousMarker.MarkerIndex == -1)
				{
					DeltaRemaining = PassedMarker.DeltaTimeWhenPassed;
				}
				++PassedMarkersIndex;
			}
		} while (PassedMarkersIndex < Context.MarkersPassedThisTick.Num());

		const FMarkerSyncAnimPosition& End = Context.GetMarkerSyncEndPosition();

		if (PreviousMarker.MarkerIndex != -1 && Context.MarkersPassedThisTick.Num() > 0)
		{
			AdvanceMarkerBackwards(PreviousMarker.MarkerIndex, End.PreviousMarkerName, bLooping, AuthoredSyncMarkers);
		}

		if (End.PreviousMarkerName == NAME_None)
		{
			PreviousMarker.MarkerIndex = -1;
		}

		//Validation
		if (PreviousMarker.MarkerIndex != -1)
		{
			check(AuthoredSyncMarkers[PreviousMarker.MarkerIndex].MarkerName == End.PreviousMarkerName);
		}

		// End Validation
		// Only reset position if we found valid markers. Otherwise stay where we are to not pop.
		if ((PreviousMarker.MarkerIndex != MarkerIndexSpecialValues::AnimationBoundary) && (NextMarker.MarkerIndex != MarkerIndexSpecialValues::AnimationBoundary))
		{
			CurrentTime = GetCurrentTimeFromMarkers(PreviousMarker, NextMarker, End.PositionBetweenMarkers);
		}
	}
}

void UAnimSequence::GetMarkerIndicesForTime(float CurrentTime, bool bLooping, const TArray<FName>& ValidMarkerNames, FMarkerPair& OutPrevMarker, FMarkerPair& OutNextMarker) const
{
	const int LoopModStart = bLooping ? -1 : 0;
	const int LoopModEnd = bLooping ? 2 : 1;

	OutPrevMarker.MarkerIndex = -1;
	OutPrevMarker.TimeToMarker = -CurrentTime;
	OutNextMarker.MarkerIndex = -1;
	OutNextMarker.TimeToMarker = GetPlayLength() - CurrentTime;

	for (int32 LoopMod = LoopModStart; LoopMod < LoopModEnd; ++LoopMod)
	{
		const float LoopModTime = LoopMod * GetPlayLength();
		for (int Idx = 0; Idx < AuthoredSyncMarkers.Num(); ++Idx)
		{
			const FAnimSyncMarker& Marker = AuthoredSyncMarkers[Idx];
			if (ValidMarkerNames.Contains(Marker.MarkerName))
			{
				const float MarkerTime = Marker.Time + LoopModTime;
				if (MarkerTime < CurrentTime)
				{
					OutPrevMarker.MarkerIndex = Idx;
					OutPrevMarker.TimeToMarker = MarkerTime - CurrentTime;
				}
				else if (MarkerTime >= CurrentTime)
				{
					OutNextMarker.MarkerIndex = Idx;
					OutNextMarker.TimeToMarker = MarkerTime - CurrentTime;
					break; // Done
				}
			}
		}
		if (OutNextMarker.MarkerIndex != -1)
		{
			break; // Done
		}
	}
}

FMarkerSyncAnimPosition UAnimSequence::GetMarkerSyncPositionfromMarkerIndicies(int32 PrevMarker, int32 NextMarker, float CurrentTime) const
{
	FMarkerSyncAnimPosition SyncPosition;
	float PrevTime, NextTime;
	
	if (PrevMarker != -1 && ensureAlwaysMsgf(AuthoredSyncMarkers.IsValidIndex(PrevMarker),
		TEXT("%s - MarkerCount: %d, PrevMarker : %d, NextMarker: %d, CurrentTime : %0.2f"), *GetFullName(), AuthoredSyncMarkers.Num(), PrevMarker, NextMarker, CurrentTime))
	{
		PrevTime = AuthoredSyncMarkers[PrevMarker].Time;
		SyncPosition.PreviousMarkerName = AuthoredSyncMarkers[PrevMarker].MarkerName;
	}
	else
	{
		PrevTime = 0.f;
	}

	if (NextMarker != -1 && ensureAlwaysMsgf(AuthoredSyncMarkers.IsValidIndex(NextMarker),
		TEXT("%s - MarkerCount: %d, PrevMarker : %d, NextMarker: %d, CurrentTime : %0.2f"), *GetFullName(), AuthoredSyncMarkers.Num(), PrevMarker, NextMarker, CurrentTime))
	{
		NextTime = AuthoredSyncMarkers[NextMarker].Time;
		SyncPosition.NextMarkerName = AuthoredSyncMarkers[NextMarker].MarkerName;
	}
	else
	{
		NextTime = GetPlayLength();
	}

	// Account for looping
	if(PrevTime > NextTime)
	{
		PrevTime = (PrevTime > CurrentTime) ? PrevTime - GetPlayLength() : PrevTime;
		NextTime = (NextTime < CurrentTime) ? NextTime + GetPlayLength() : NextTime;
	}
	else if (PrevTime > CurrentTime)
	{
		CurrentTime += GetPlayLength();
	}

	if (PrevTime == NextTime)
	{
		PrevTime -= GetPlayLength();
	}

	check(NextTime > PrevTime);

	SyncPosition.PositionBetweenMarkers = (CurrentTime - PrevTime) / (NextTime - PrevTime);
	return SyncPosition;
}

float UAnimSequence::GetCurrentTimeFromMarkers(FMarkerPair& PrevMarker, FMarkerPair& NextMarker, float PositionBetweenMarkers) const
{
	float PrevTime = (PrevMarker.MarkerIndex != -1) ? AuthoredSyncMarkers[PrevMarker.MarkerIndex].Time : 0.f;
	float NextTime = (NextMarker.MarkerIndex != -1) ? AuthoredSyncMarkers[NextMarker.MarkerIndex].Time : GetPlayLength();

	if (PrevTime >= NextTime)
	{
		PrevTime -= GetPlayLength(); //Account for looping
	}
	float CurrentTime = PrevTime + PositionBetweenMarkers * (NextTime - PrevTime);

	PrevMarker.TimeToMarker = PrevTime - CurrentTime;
	NextMarker.TimeToMarker = NextTime - CurrentTime;

	if (CurrentTime < 0.f)
	{
		CurrentTime += GetPlayLength();
	}
	CurrentTime = FMath::Clamp<float>(CurrentTime, 0, GetPlayLength());

	return CurrentTime;
}

void UAnimSequence::GetMarkerIndicesForPosition(const FMarkerSyncAnimPosition& SyncPosition, bool bLooping, FMarkerPair& OutPrevMarker, FMarkerPair& OutNextMarker, float& OutCurrentTime) const
{
	// If we're not looping, assume we're playing a transition and we need to stay where we are.
	if (!bLooping)
	{
		OutPrevMarker.MarkerIndex = INDEX_NONE;
		OutNextMarker.MarkerIndex = INDEX_NONE;

		for (int32 Idx = 0; Idx<AuthoredSyncMarkers.Num(); Idx++)
		{
			const FAnimSyncMarker& SyncMarker = AuthoredSyncMarkers[Idx];
			const float MarkerTime = SyncMarker.Time;

			if (OutCurrentTime > MarkerTime && SyncMarker.MarkerName == SyncPosition.PreviousMarkerName)
			{
				OutPrevMarker.MarkerIndex = Idx;
				OutPrevMarker.TimeToMarker = MarkerTime - OutCurrentTime;
			}
			else if (OutCurrentTime < MarkerTime && SyncMarker.MarkerName == SyncPosition.NextMarkerName)
			{
				OutNextMarker.MarkerIndex = Idx;
				OutNextMarker.TimeToMarker = MarkerTime - OutCurrentTime;
				break;
			}
		}

		ensureMsgf(OutCurrentTime >= 0.f && OutCurrentTime <= GetPlayLength(), TEXT("Current time inside of GetMarkerIndicesForPosition is out of range %.3f of 0.0 to %.3f\n    Sequence: %s"), OutCurrentTime, GetPlayLength(), *GetFullName());
		return;
	}

	if (SyncPosition.PreviousMarkerName == NAME_None)
	{
		OutPrevMarker.MarkerIndex = -1;
		check(SyncPosition.NextMarkerName != NAME_None);

		for (int32 Idx = 0; Idx < AuthoredSyncMarkers.Num(); ++Idx)
		{
			const FAnimSyncMarker& Marker = AuthoredSyncMarkers[Idx];
			if (Marker.MarkerName == SyncPosition.NextMarkerName)
			{
				OutNextMarker.MarkerIndex = Idx;
				OutCurrentTime = GetCurrentTimeFromMarkers(OutPrevMarker, OutNextMarker, SyncPosition.PositionBetweenMarkers);
				return;
			}
		}
		// Should have found a marker above!
		checkf(false, TEXT("Next Marker not found in GetMarkerIndicesForPosition. Anim: %s Expecting marker %s (Added to help debug Jira OR-9675)"), *GetName(), *SyncPosition.NextMarkerName.ToString());
	}

	if (SyncPosition.NextMarkerName == NAME_None)
	{
		OutNextMarker.MarkerIndex = -1;
		check(SyncPosition.PreviousMarkerName != NAME_None);

		for (int32 Idx = AuthoredSyncMarkers.Num() - 1; Idx >= 0; --Idx)
		{
			const FAnimSyncMarker& Marker = AuthoredSyncMarkers[Idx];
			if (Marker.MarkerName == SyncPosition.PreviousMarkerName)
			{
				OutPrevMarker.MarkerIndex = Idx;
				OutCurrentTime = GetCurrentTimeFromMarkers(OutPrevMarker, OutNextMarker, SyncPosition.PositionBetweenMarkers);
				return;
			}
		}
		// Should have found a marker above!
		checkf(false, TEXT("Previous Marker not found in GetMarkerIndicesForPosition. Anim: %s Expecting marker %s (Added to help debug Jira OR-9675)"), *GetName(), *SyncPosition.PreviousMarkerName.ToString());
	}

	float DiffToCurrentTime = FLT_MAX;
	const float CurrentInputTime  = OutCurrentTime;

	for (int32 PrevMarkerIdx = 0; PrevMarkerIdx < AuthoredSyncMarkers.Num(); ++PrevMarkerIdx)
	{
		const FAnimSyncMarker& PrevMarker = AuthoredSyncMarkers[PrevMarkerIdx];
		if (PrevMarker.MarkerName == SyncPosition.PreviousMarkerName)
		{
			const int32 EndMarkerSearchStart = PrevMarkerIdx + 1;

			const int32 EndCount = bLooping ? AuthoredSyncMarkers.Num() + EndMarkerSearchStart : AuthoredSyncMarkers.Num();
			for (int32 NextMarkerCount = EndMarkerSearchStart; NextMarkerCount < EndCount; ++NextMarkerCount)
			{
				const int32 NextMarkerIdx = NextMarkerCount % AuthoredSyncMarkers.Num();

				if (AuthoredSyncMarkers[NextMarkerIdx].MarkerName == SyncPosition.NextMarkerName)
				{
					float NextMarkerTime = AuthoredSyncMarkers[NextMarkerIdx].Time;
					if (NextMarkerTime < PrevMarker.Time)
					{
						NextMarkerTime += GetPlayLength();
					}
					float ThisCurrentTime = PrevMarker.Time + SyncPosition.PositionBetweenMarkers * (NextMarkerTime - PrevMarker.Time);
					if (ThisCurrentTime > GetPlayLength())
					{
						ThisCurrentTime -= GetPlayLength();
					}
					float ThisDiff = FMath::Abs(ThisCurrentTime - CurrentInputTime);
					if (ThisDiff < DiffToCurrentTime)
					{
						DiffToCurrentTime = ThisDiff;
						OutPrevMarker.MarkerIndex = PrevMarkerIdx;
						OutNextMarker.MarkerIndex = NextMarkerIdx;
						OutCurrentTime = GetCurrentTimeFromMarkers(OutPrevMarker, OutNextMarker, SyncPosition.PositionBetweenMarkers);
					}

					// this marker test is done, move onto next one
					break;
				}
			}

			// If we get here and we haven't found a match and we are not looping then there 
			// is no point running the rest of the loop set up something as relevant as we can and carry on
			if (OutPrevMarker.MarkerIndex == MarkerIndexSpecialValues::Unitialized)
			{
				//Find nearest previous marker that is earlier than our current time
				DiffToCurrentTime = OutCurrentTime - PrevMarker.Time;
				int32 PrevMarkerToUse = PrevMarkerIdx + 1;
				while (DiffToCurrentTime > 0.f && PrevMarkerToUse < AuthoredSyncMarkers.Num())
				{
					DiffToCurrentTime = OutCurrentTime - AuthoredSyncMarkers[PrevMarkerToUse].Time;
					++PrevMarkerToUse;
				}
				OutPrevMarker.MarkerIndex = PrevMarkerToUse - 1;	// We always go one past the marker we actually want to use
				
				OutNextMarker.MarkerIndex = -1;						// This goes to minus one as the very fact we are here means
																	// that there is no next marker to use
				OutCurrentTime = GetCurrentTimeFromMarkers(OutPrevMarker, OutNextMarker, SyncPosition.PositionBetweenMarkers);
				break; // no need to keep searching, we are done
			}
		}
	}
	// Should have found a markers above!
	checkf(OutPrevMarker.MarkerIndex != MarkerIndexSpecialValues::Unitialized, TEXT("Prev Marker not found in GetMarkerIndicesForPosition. Anim: %s Expecting marker %s (Added to help debug Jira OR-9675)"), *GetName(), *SyncPosition.PreviousMarkerName.ToString());
	checkf(OutNextMarker.MarkerIndex != MarkerIndexSpecialValues::Unitialized, TEXT("Next Marker not found in GetMarkerIndicesForPosition. Anim: %s Expecting marker %s (Added to help debug Jira OR-9675)"), *GetName(), *SyncPosition.NextMarkerName.ToString());
}

float UAnimSequence::GetFirstMatchingPosFromMarkerSyncPos(const FMarkerSyncAnimPosition& InMarkerSyncGroupPosition) const
{
	if ((InMarkerSyncGroupPosition.PreviousMarkerName == NAME_None) || (InMarkerSyncGroupPosition.NextMarkerName == NAME_None))
	{
		return 0.f;
	}

	for (int32 PrevMarkerIdx = 0; PrevMarkerIdx < AuthoredSyncMarkers.Num()-1; PrevMarkerIdx++)
	{
		const FAnimSyncMarker& PrevMarker = AuthoredSyncMarkers[PrevMarkerIdx];
		const FAnimSyncMarker& NextMarker = AuthoredSyncMarkers[PrevMarkerIdx+1];
		if ((PrevMarker.MarkerName == InMarkerSyncGroupPosition.PreviousMarkerName) && (NextMarker.MarkerName == InMarkerSyncGroupPosition.NextMarkerName))
		{
			return FMath::Lerp(PrevMarker.Time, NextMarker.Time, InMarkerSyncGroupPosition.PositionBetweenMarkers);
		}
	}

	return 0.f;
}

float UAnimSequence::GetNextMatchingPosFromMarkerSyncPos(const FMarkerSyncAnimPosition& InMarkerSyncGroupPosition, const float& StartingPosition) const
{
	if ((InMarkerSyncGroupPosition.PreviousMarkerName == NAME_None) || (InMarkerSyncGroupPosition.NextMarkerName == NAME_None))
	{
		return StartingPosition;
	}

	for (int32 PrevMarkerIdx = 0; PrevMarkerIdx < AuthoredSyncMarkers.Num() - 1; PrevMarkerIdx++)
	{
		const FAnimSyncMarker& PrevMarker = AuthoredSyncMarkers[PrevMarkerIdx];
		const FAnimSyncMarker& NextMarker = AuthoredSyncMarkers[PrevMarkerIdx + 1];

		if (NextMarker.Time < StartingPosition)
		{
			continue;
		}

		if ((PrevMarker.MarkerName == InMarkerSyncGroupPosition.PreviousMarkerName) && (NextMarker.MarkerName == InMarkerSyncGroupPosition.NextMarkerName))
		{
			const float FoundTime = FMath::Lerp(PrevMarker.Time, NextMarker.Time, InMarkerSyncGroupPosition.PositionBetweenMarkers);
			if (FoundTime < StartingPosition)
			{
				continue;
			}
			return FoundTime;
		}
	}

	return StartingPosition;
}

float UAnimSequence::GetPrevMatchingPosFromMarkerSyncPos(const FMarkerSyncAnimPosition& InMarkerSyncGroupPosition, const float& StartingPosition) const
{
	if ((InMarkerSyncGroupPosition.PreviousMarkerName == NAME_None) || (InMarkerSyncGroupPosition.NextMarkerName == NAME_None) || (AuthoredSyncMarkers.Num() < 2))
	{
		return StartingPosition;
	}

	for (int32 PrevMarkerIdx = AuthoredSyncMarkers.Num() - 2; PrevMarkerIdx >= 0; PrevMarkerIdx--)
	{
		const FAnimSyncMarker& PrevMarker = AuthoredSyncMarkers[PrevMarkerIdx];
		const FAnimSyncMarker& NextMarker = AuthoredSyncMarkers[PrevMarkerIdx + 1];

		if (PrevMarker.Time > StartingPosition)
		{
			continue;
		}

		if ((PrevMarker.MarkerName == InMarkerSyncGroupPosition.PreviousMarkerName) && (NextMarker.MarkerName == InMarkerSyncGroupPosition.NextMarkerName))
		{
			const float FoundTime = FMath::Lerp(PrevMarker.Time, NextMarker.Time, InMarkerSyncGroupPosition.PositionBetweenMarkers);
			if (FoundTime > StartingPosition)
			{
				continue;
			}
			return FoundTime;
		}
	}

	return StartingPosition;
}

void UAnimSequence::EnableRootMotionSettingFromMontage(bool bInEnableRootMotion, const ERootMotionRootLock::Type InRootMotionRootLock)
{
	if (!bRootMotionSettingsCopiedFromMontage)
	{
		bEnableRootMotion = bInEnableRootMotion;
		RootMotionRootLock = InRootMotionRootLock;
		bRootMotionSettingsCopiedFromMontage = true;
	}
}

#if WITH_EDITOR
void UAnimSequence::OnRawDataChanged()
{
	RecompressAnimationData();
	//MDW - Once we have async anim ddc requests we should do this too
	//RequestDependentAnimRecompression();
}
#endif

bool UAnimSequence::IsCompressedDataValid() const
{
	// For bone compressed data, we don't check if we have a codec. It is valid to have no compressed data
	// if we have no raw data. This can happen with sequences that only has curves.
#if WITH_EDITOR
	ValidateModel();
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (DataModel->GetNumBoneTracks() == 0 && RawAnimationData.Num() == 0)
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		return true;
	}
#endif

	return CompressedData.CompressedDataStructure != nullptr || CompressedData.CompressedTrackToSkeletonMapTable.Num() == 0;
}

bool UAnimSequence::IsCurveCompressedDataValid() const
{
#if WITH_EDITOR
	ValidateModel();
	// For curve compressed data, we don't check if we have a codec. It is valid to have no compressed data
	// if we have no raw data. This can happen with sequences that only has bones.

	if (CompressedData.CompressedCurveByteStream.Num() == 0 && DataModel->GetNumberOfFloatCurves() != 0)
	{
		// No compressed data but we have raw data
		if (!IsValidAdditive())
		{
			return false;
		}

		// Additive sequences can have raw curves that all end up being 0.0 (e.g. they 100% match the base sequence curves)
		// in which case there will be no compressed curve data.
	}
#endif

	return true;
}

void UAnimSequence::ClearCompressedBoneData()
{
	CompressedData.ClearCompressedBoneData();
}

void UAnimSequence::ClearCompressedCurveData()
{
	CompressedData.ClearCompressedCurveData();
}

#if WITH_EDITOR
void UAnimSequence::EnsureValidRawDataGuid()
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (!RawDataGuid.IsValid())
	{
		RawDataGuid = GenerateGuidFromRawData();
	}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void UAnimSequence::PopulateModel()
{
	check(!HasAnyFlags(EObjectFlags::RF_ClassDefaultObject));
	
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const int32 NumKeys = NumberOfKeys;
	const float PlayLength = SequenceLength;
	// Reset target framerate to current sampling frame rate
	TargetFrameRate = SamplingFrameRate;
	const FFrameRate FrameRate = SamplingFrameRate;
	const FRawCurveTracks CurveData = RawCurveData;
	const TArray<FRawAnimSequenceTrack> SequenceTracks = SourceRawAnimationData_DEPRECATED.Num() && SourceRawAnimationData_DEPRECATED.Num() == RawAnimationData.Num() ?
		SourceRawAnimationData_DEPRECATED :
		RawAnimationData;
	const TArray<FName> TempAnimationTrackNames = AnimationTrackNames;
	
	const bool bCurveOnlySequence = RawAnimationData.Num() == 0;
	const bool bValidNumberOfKeys = NumberOfKeys >= 1 || bCurveOnlySequence;
	const float ExpectedPlayLength = (float)SamplingFrameRate.AsSeconds(FMath::Max(NumberOfKeys - 1, 1));
	const bool bValidSequenceLength = FMath::IsNearlyEqual(ExpectedPlayLength, SequenceLength, KINDA_SMALL_NUMBER) || bCurveOnlySequence;
	const bool bValidNumberOfTrackArrayEntries = AnimationTrackNames.Num() == RawAnimationData.Num() && RawAnimationData.Num() == TrackToSkeletonMapTable.Num();
	const bool bValidAnimationData = SourceRawAnimationData_DEPRECATED.Num() == 0 || SourceRawAnimationData_DEPRECATED.Num() == RawAnimationData.Num();
	const bool bValidFrameRate = SamplingFrameRate.IsValid() && (SamplingFrameRate.AsInterval() >= 0.f);
	const bool bValidCurveData = RawCurveData.FloatCurves.Num() >= 1;

	if (!((bValidNumberOfTrackArrayEntries || bValidCurveData) && bValidAnimationData && bValidFrameRate && bValidNumberOfKeys && bValidSequenceLength))
	{
		UE_LOG(LogAnimation, Warning, TEXT("Possible invalid animation data during model generation %s - \n\tNumber of Keys: %i\n\tPlay length: %f\n\tNumber of Tracks: %i\n\tFrameRate: %s\n\tNumber of float Curves: %i"), *GetName(), NumKeys, PlayLength, RawAnimationData.Num(), *SamplingFrameRate.ToPrettyText().ToString(), RawCurveData.FloatCurves.Num());
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	if (RefPoseSeq && RefPoseSeq->HasAnyFlags(EObjectFlags::RF_NeedPostLoad))
	{
		RefPoseSeq->ConditionalPostLoad();
	}

	Controller->OpenBracket(LOCTEXT("UAnimSequence::PopulateModel_Bracket", "Generating Animation Model Data from Animation Sequence"));

	Controller->SetPlayLength(bCurveOnlySequence ? PlayLength : ExpectedPlayLength);
	Controller->SetFrameRate(FrameRate);

	USkeleton* TargetSkeleton = GetSkeleton();
	UE::Anim::CopyCurveDataToModel(CurveData, TargetSkeleton, Controller);
	
	const int32 NumTracks = SequenceTracks.Num();
	for (int32 TrackIndex = 0; TrackIndex < NumTracks; ++TrackIndex)
	{
		Controller->AddBoneTrack(TempAnimationTrackNames[TrackIndex]);

		auto GenerateUniformKeys = [NumKeys](auto& Keys, auto IdentityKey)
		{
			// Convert track keys to be uniform			
			if (Keys.Num() == 0)
			{
				// set all to identity
				for (int32 Index = 0; Index < NumKeys; ++Index)
				{
					Keys.Add(IdentityKey);
				}
			}
			else if (Keys.Num() == 1)
			{
				// set all to single key
				const auto KeyZero = Keys[0];
				for (int32 Index = 0; Index < (NumKeys - 1); ++Index)
				{
					Keys.Add(KeyZero);
				}
			}

			ensure(Keys.Num() == NumKeys);
		};

		TArray<FVector> PosKeys = SequenceTracks[TrackIndex].PosKeys;
		GenerateUniformKeys(PosKeys, FVector::ZeroVector);

		TArray<FQuat> RotKeys = SequenceTracks[TrackIndex].RotKeys;
		GenerateUniformKeys(RotKeys, FQuat::Identity);

		TArray<FVector> ScaleKeys = SequenceTracks[TrackIndex].ScaleKeys;
		GenerateUniformKeys(ScaleKeys, FVector::OneVector);

		Controller->SetBoneTrackKeys(TempAnimationTrackNames[TrackIndex], PosKeys, RotKeys, ScaleKeys);
	}

	DeleteDeprecatedRawAnimationData();
	
	Controller->CloseBracket();
}

void UAnimSequence::OnModelModified(const EAnimDataModelNotifyType& NotifyType, UAnimDataModel* Model, const FAnimDataModelNotifPayload& Payload)
{
	UAnimSequenceBase::OnModelModified(NotifyType, Model, Payload);	

	static const bool GenerateNewGUID = true;
	static const bool RegenerateGUID = false;

	auto UpdateRawDataGuid = [this](bool bForceNewRawDataGuid)
	{
		RawDataGuid = bForceNewRawDataGuid ? FGuid::NewGuid() : GenerateGuidFromRawData();
		bUseRawDataOnly = true;

		FlagDependentAnimationsAsRawDataOnly();
		UpdateDependentStreamingAnimations();
	};

	auto HandleTrackDataChanged = [this, UpdateRawDataGuid]()
	{
		if (NotifyCollector.IsNotWithinBracket())
		{
			ResampleAnimationTrackData();
			UpdateRawDataGuid(RegenerateGUID);
			RecompressAnimationData();
		}
	};

	auto HandleLengthChanged = [this](float NewLength, float OldLength, float T0, float T1)
	{
		if (bPopulatingDataModel)
		{
			return;
		}

		if (NewLength > OldLength)
		{
			const float InsertTime = T0;
			const float Duration = T1 - T0;

			for (FAnimSyncMarker& Marker : AuthoredSyncMarkers)
			{
				float CurrentTime = Marker.Time;

				// when insert, we only care about start time
				// if it's later than start time
				if (CurrentTime >= InsertTime)
				{
					CurrentTime += Duration;
				}

				Marker.Time = FMath::Clamp(CurrentTime, 0.f, NewLength);
			}
		}
		else if (NewLength < OldLength)
		{
			const float StartRemoveTime = T0;
			const float EndRemoveTime = T1;

			// Total time value for frames that were removed
			const float Duration = T1 - T0;

			for (FAnimSyncMarker& Marker : AuthoredSyncMarkers)
			{
				float CurrentTime = Marker.Time;

				if (CurrentTime >= StartRemoveTime && CurrentTime <= EndRemoveTime)
				{
					CurrentTime = StartRemoveTime;
				}
				else if (CurrentTime > EndRemoveTime)
				{
					CurrentTime -= Duration;
				}

				Marker.Time = FMath::Clamp(CurrentTime, 0.f, NewLength);
			}

		}
	};

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	switch (NotifyType)
	{
		case EAnimDataModelNotifyType::SequenceLengthChanged:		
		{
			const FSequenceLengthChangedPayload& TypedPayload = Payload.GetPayload<FSequenceLengthChangedPayload>();

			const float OldSequenceLength = TypedPayload.PreviousLength;
			const float CurrentSequenceLength = Model->GetPlayLength();
			
			HandleLengthChanged(CurrentSequenceLength, OldSequenceLength, TypedPayload.T0, TypedPayload.T1);
						
			if (NotifyCollector.IsNotWithinBracket())
			{
				HandleTrackDataChanged();
			}

			break;
		}

		case EAnimDataModelNotifyType::FrameRateChanged:
		{
			const FFrameRateChangedPayload& TypedPayload = Payload.GetPayload<FFrameRateChangedPayload>();
			
			if (NotifyCollector.IsNotWithinBracket())
			{
				HandleTrackDataChanged();
			}

			break;
		}

		case EAnimDataModelNotifyType::Populated:
		{
			TargetFrameRate = Model->GetFrameRate();

			if (NotifyCollector.IsNotWithinBracket())
			{
				HandleTrackDataChanged();
			}	
			break;
		}
		
		case EAnimDataModelNotifyType::BracketClosed:
		{
			if (NotifyCollector.IsNotWithinBracket())
			{
				const auto LengthChangingNotifies = { EAnimDataModelNotifyType::SequenceLengthChanged, EAnimDataModelNotifyType::FrameRateChanged, EAnimDataModelNotifyType::Reset };
				const auto ResamplingNotifies = { EAnimDataModelNotifyType::TrackAdded, EAnimDataModelNotifyType::TrackChanged, EAnimDataModelNotifyType::TrackRemoved,  EAnimDataModelNotifyType::Populated };
				const auto RecompressNotifies = { EAnimDataModelNotifyType::CurveAdded, EAnimDataModelNotifyType::CurveChanged, EAnimDataModelNotifyType::CurveRemoved, EAnimDataModelNotifyType::CurveFlagsChanged, EAnimDataModelNotifyType::CurveScaled };
				
				if (NotifyCollector.Contains(LengthChangingNotifies) || NotifyCollector.Contains(ResamplingNotifies))
				{
					ResampleAnimationTrackData();
				}

				if (NotifyCollector.Contains(LengthChangingNotifies) || NotifyCollector.Contains(ResamplingNotifies) || NotifyCollector.Contains(RecompressNotifies))
				{
					const bool bWasModelReset = NotifyCollector.Contains(EAnimDataModelNotifyType::Reset);
					UpdateRawDataGuid(bWasModelReset ? GenerateNewGUID : RegenerateGUID);
					ClearCompressedCurveData();
					RecompressAnimationData();
				}
			}

			break;
		}

		case EAnimDataModelNotifyType::BracketOpened:
		{
			break;
		}

		case EAnimDataModelNotifyType::Reset:
		{
			if (NotifyCollector.IsNotWithinBracket())
			{
				ResampleAnimationTrackData();
				UpdateRawDataGuid(GenerateNewGUID);
				RecompressAnimationData();
			}

			break;
		}
		
		case EAnimDataModelNotifyType::TrackAdded:
		case EAnimDataModelNotifyType::TrackChanged:
		case EAnimDataModelNotifyType::TrackRemoved:		
		{
			HandleTrackDataChanged();				
			break;
		}

		case EAnimDataModelNotifyType::CurveAdded:
		case EAnimDataModelNotifyType::CurveChanged:
		case EAnimDataModelNotifyType::CurveRemoved:
		case EAnimDataModelNotifyType::CurveFlagsChanged:
		case EAnimDataModelNotifyType::CurveScaled:
		{
			if (NotifyCollector.IsNotWithinBracket())
			{
				UpdateRawDataGuid(RegenerateGUID);
				ClearCompressedCurveData();
				RecompressAnimationData();
			}

			break;
		}

		case EAnimDataModelNotifyType::CurveColorChanged:
		{
			break;
		}

		case EAnimDataModelNotifyType::CurveRenamed:
		{
			const FCurveRenamedPayload& TypedPayload = Payload.GetPayload<FCurveRenamedPayload>();
			UpdateCompressedCurveName(TypedPayload.Identifier.InternalName.UID, TypedPayload.NewIdentifier.InternalName);
			break;
		}
		default:
		{
			checkf(false, TEXT("Missing case statement for animation model notify type"));
			break;
		}
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	
	MarkPackageDirty();
}


void UAnimSequence::ResampleAnimationTrackData()
{
	ValidateModel();

	if (TargetFrameRate == DataModel->GetFrameRate())
	{
		// Can directly copy
		ResampledAnimationTrackData = DataModel->GetBoneAnimationTracks();
		NumberOfSampledKeys = DataModel->GetNumberOfKeys();
		NumberOfSampledFrames = DataModel->GetNumberOfFrames();
	}
	else
	{
		// We actually need to resample
		ResampledAnimationTrackData.SetNum(DataModel->GetBoneAnimationTracks().Num());

		NumberOfSampledFrames = TargetFrameRate.AsFrameNumber(DataModel->GetPlayLength()).Value;
		NumberOfSampledKeys = NumberOfSampledFrames + 1;
				
		for (int32 TrackIndex = 0; TrackIndex < ResampledAnimationTrackData.Num(); ++TrackIndex)	
		{
			FBoneAnimationTrack& TrackData = ResampledAnimationTrackData[TrackIndex];

			TrackData.InternalTrackData.PosKeys.SetNumUninitialized(NumberOfSampledKeys);
			TrackData.InternalTrackData.RotKeys.SetNumUninitialized(NumberOfSampledKeys);
			TrackData.InternalTrackData.ScaleKeys.SetNumUninitialized(NumberOfSampledKeys);

			for (int32 FrameIndex = 0; FrameIndex < NumberOfSampledKeys; ++FrameIndex)
			{
				FTransform Transform;

				const double SampleTime = TargetFrameRate.AsSeconds(FrameIndex);

				UE::Anim::GetBoneTransformFromModel(DataModel, Transform, TrackIndex, SampleTime, EAnimInterpolationType::Linear);

				TrackData.InternalTrackData.PosKeys[FrameIndex] = Transform.GetLocation();
				TrackData.InternalTrackData.RotKeys[FrameIndex] = Transform.GetRotation();
				TrackData.InternalTrackData.ScaleKeys[FrameIndex] = Transform.GetScale3D();
			}
		}
	}
}

void UAnimSequence::RecompressAnimationData()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	ClearCompressedBoneData();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	bUseRawDataOnly = true;
	RequestAsyncAnimRecompression(false);
}
#endif // WITH_EDITOR

/*-----------------------------------------------------------------------------
	AnimNotify& subclasses
-----------------------------------------------------------------------------*/

#if !UE_BUILD_SHIPPING

void GatherAnimSequenceStats(FOutputDevice& Ar)
{
	int32 AnimationKeyFormatNum[AKF_MAX];
	int32 TranslationCompressionFormatNum[ACF_MAX];
	int32 RotationCompressionFormatNum[ACF_MAX];
	int32 ScaleCompressionFormatNum[ACF_MAX];
	FMemory::Memzero( AnimationKeyFormatNum, AKF_MAX * sizeof(int32) );
	FMemory::Memzero( TranslationCompressionFormatNum, ACF_MAX * sizeof(int32) );
	FMemory::Memzero( RotationCompressionFormatNum, ACF_MAX * sizeof(int32) );
	FMemory::Memzero( ScaleCompressionFormatNum, ACF_MAX * sizeof(int32) );

	Ar.Logf( TEXT(" %60s, Frames,NTT,NRT, NT1,NR1, TotTrnKys,TotRotKys,Codec,ResBytes"), TEXT("Sequence Name") );
	int32 GlobalNumTransTracks = 0;
	int32 GlobalNumRotTracks = 0;
	int32 GlobalNumScaleTracks = 0;
	int32 GlobalNumTransTracksWithOneKey = 0;
	int32 GlobalNumRotTracksWithOneKey = 0;
	int32 GlobalNumScaleTracksWithOneKey = 0;
	int32 GlobalApproxCompressedSize = 0;
	int32 GlobalApproxKeyDataSize = 0;
	int32 GlobalNumTransKeys = 0;
	int32 GlobalNumRotKeys = 0;
	int32 GlobalNumScaleKeys = 0;

	for( TObjectIterator<UAnimSequence> It; It; ++It )
	{
		UAnimSequence* Seq = *It;

		if (Seq->CompressedData.CompressedDataStructure == nullptr || !Seq->CompressedData.BoneCompressionCodec->IsA<UAnimCompress>())
		{
			continue;	// Custom codec we know nothing about, skip it
		}

		int32 NumTransTracks = 0;
		int32 NumRotTracks = 0;
		int32 NumScaleTracks = 0;
		int32 TotalNumTransKeys = 0;
		int32 TotalNumRotKeys = 0;
		int32 TotalNumScaleKeys = 0;
		float TranslationKeySize = 0.0f;
		float RotationKeySize = 0.0f;
		float ScaleKeySize = 0.0f;
		int32 OverheadSize = 0;
		int32 NumTransTracksWithOneKey = 0;
		int32 NumRotTracksWithOneKey = 0;
		int32 NumScaleTracksWithOneKey = 0;

		const FUECompressedAnimData& AnimData = static_cast<FUECompressedAnimData&>(*Seq->CompressedData.CompressedDataStructure);

		AnimationFormat_GetStats(
			AnimData,
			NumTransTracks,
			NumRotTracks,
			NumScaleTracks,
			TotalNumTransKeys,
			TotalNumRotKeys,
			TotalNumScaleKeys,
			TranslationKeySize,
			RotationKeySize,
			ScaleKeySize, 
			OverheadSize,
			NumTransTracksWithOneKey,
			NumRotTracksWithOneKey,
			NumScaleTracksWithOneKey);

		GlobalNumTransTracks += NumTransTracks;
		GlobalNumRotTracks += NumRotTracks;
		GlobalNumScaleTracks += NumScaleTracks;
		GlobalNumTransTracksWithOneKey += NumTransTracksWithOneKey;
		GlobalNumRotTracksWithOneKey += NumRotTracksWithOneKey;
		GlobalNumScaleTracksWithOneKey += NumScaleTracksWithOneKey;

		GlobalApproxCompressedSize += Seq->GetApproxCompressedSize();
		GlobalApproxKeyDataSize += (int32)((TotalNumTransKeys * TranslationKeySize) + (TotalNumRotKeys * RotationKeySize) + (TotalNumScaleKeys * ScaleKeySize));

		GlobalNumTransKeys += TotalNumTransKeys;
		GlobalNumRotKeys += TotalNumRotKeys;
		GlobalNumScaleKeys += TotalNumScaleKeys;

		Ar.Logf(TEXT(" %60s, %3i, %3i,%3i,%3i, %3i,%3i,%3i, %10i,%10i,%10i, %s, %d"),
			*Seq->GetName(),
			Seq->GetNumberOfSampledKeys(),
			NumTransTracks, NumRotTracks, NumScaleTracks,
			NumTransTracksWithOneKey, NumRotTracksWithOneKey, NumScaleTracksWithOneKey,
			TotalNumTransKeys, TotalNumRotKeys, TotalNumScaleKeys,
			*FAnimationUtils::GetAnimationKeyFormatString(AnimData.KeyEncodingFormat),
			(int32)Seq->GetResourceSizeBytes(EResourceSizeMode::EstimatedTotal) );
	}
	Ar.Logf( TEXT("======================================================================") );
	Ar.Logf( TEXT("Total Num Tracks: %i trans, %i rot, %i scale, %i trans1, %i rot1, %i scale1"), GlobalNumTransTracks, GlobalNumRotTracks, GlobalNumScaleTracks, GlobalNumTransTracksWithOneKey, GlobalNumRotTracksWithOneKey, GlobalNumScaleTracksWithOneKey  );
	Ar.Logf( TEXT("Total Num Keys: %i trans, %i rot, %i scale"), GlobalNumTransKeys, GlobalNumRotKeys, GlobalNumScaleKeys );

	Ar.Logf( TEXT("Approx Compressed Memory: %i bytes"), GlobalApproxCompressedSize);
	Ar.Logf( TEXT("Approx Key Data Memory: %i bytes"), GlobalApproxKeyDataSize);
}

#endif // !UE_BUILD_SHIPPING


#undef LOCTEXT_NAMESPACE 
