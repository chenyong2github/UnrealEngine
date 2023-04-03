// Copyright Epic Games, Inc. All Rights Reserved.
// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "AnimBoneCompressionCodec_ACL.h"

#if WITH_EDITORONLY_DATA
#include "AnimBoneCompressionCodec_ACLSafe.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshModel.h"
// @third party code - Epic Games Begin
#include "Interfaces/ITargetPlatform.h"
#include "PlatformInfo.h"
// @third party code - Epic Games End

#include "ACLImpl.h"

THIRD_PARTY_INCLUDES_START
#include <acl/compression/track_error.h>
#include <acl/decompression/decompress.h>
THIRD_PARTY_INCLUDES_END

#endif	// WITH_EDITORONLY_DATA

#include "ACLDecompressionImpl.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimBoneCompressionCodec_ACL)

UAnimBoneCompressionCodec_ACL::UAnimBoneCompressionCodec_ACL(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	SafetyFallbackThreshold = 1.0f;			// 1cm, should be very rarely exceeded
// @third party code - Epic Games Begin
	bAllowFrameRemoval = false;
	FrameRemovalThresholdType = ACLFrameRemovalThresholdType::ProportionOfFrames;
	RemovalProportion = 0.5f;
// @third party code - Epic Games End
#endif	// WITH_EDITORONLY_DATA
}

#if WITH_EDITORONLY_DATA
void UAnimBoneCompressionCodec_ACL::PostInitProperties()
{
	Super::PostInitProperties();

	if (!IsTemplate())
	{
		// Ensure we are never null
		SafetyFallbackCodec = NewObject<UAnimBoneCompressionCodec_ACLSafe>(this, NAME_None, RF_Public);
	}
}

void UAnimBoneCompressionCodec_ACL::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	Super::GetPreloadDependencies(OutDeps);

	if (SafetyFallbackCodec != nullptr)
	{
		OutDeps.Add(SafetyFallbackCodec);
	}
}

bool UAnimBoneCompressionCodec_ACL::IsCodecValid() const
{
	if (!Super::IsCodecValid())
	{
		return false;
	}

	return SafetyFallbackCodec != nullptr ? SafetyFallbackCodec->IsCodecValid() : true;
}

// @third party code - Epic Games Begin
void UAnimBoneCompressionCodec_ACL::GetCompressionSettings(acl::compression_settings& OutSettings, const ITargetPlatform* TargetPlatform) const
// @third party code - Epic Games End
{
	OutSettings = acl::get_default_compression_settings();

	OutSettings.level = GetCompressionLevel(CompressionLevel);
// @third party code - Epic Games Begin
	if (bAllowFrameRemoval)
	{
		// set the compression settings for tracking error per frame
		OutSettings.enable_frame_stripping = true;
		OutSettings.frame_stripping_use_proportion = (FrameRemovalThresholdType == ACLFrameRemovalThresholdType::ProportionOfFrames);
		if (TargetPlatform != nullptr)
		{
			FName TargetPlatformName = TargetPlatform->GetTargetPlatformInfo().Name;
			OutSettings.frame_stripping_proportion = RemovalProportion.GetValueForPlatform(TargetPlatformName);
			OutSettings.frame_stripping_error_distance = RemovalDistanceError.GetValueForPlatform(TargetPlatformName);
		}
		else
		{
			OutSettings.frame_stripping_proportion = RemovalProportion.GetValue();
			OutSettings.frame_stripping_error_distance = RemovalDistanceError.GetValue();
		}

		OutSettings.metadata.include_contributing_error = true;
	}
// @third party code - Epic Games End
}

ACLSafetyFallbackResult UAnimBoneCompressionCodec_ACL::ExecuteSafetyFallback(acl::iallocator& Allocator, const acl::compression_settings& Settings, const acl::track_array_qvvf& RawClip, const acl::track_array_qvvf& BaseClip, const acl::compressed_tracks& CompressedClipData, const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult)
{
	if (SafetyFallbackCodec != nullptr && SafetyFallbackThreshold > 0.0f)
	{
		checkSlow(CompressedClipData.is_valid(true).empty());

		acl::decompression_context<UE4DefaultDBDecompressionSettings> Context;
		Context.initialize(CompressedClipData);

		const acl::track_error TrackError = acl::calculate_compression_error(Allocator, RawClip, Context, *Settings.error_metric, BaseClip);
		if (TrackError.error >= SafetyFallbackThreshold)
		{
			UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation compressed size: %u bytes [%s]"), CompressedClipData.get_size(), *CompressibleAnimData.FullName);
			UE_LOG(LogAnimationCompression, Warning, TEXT("ACL Animation error is too high, a safe fallback will be used instead: %.4f cm at %.4f on track %i [%s]"), TrackError.error, TrackError.sample_time, TrackError.index, *CompressibleAnimData.FullName);

			// Just use the safety fallback
			return SafetyFallbackCodec->Compress(CompressibleAnimData, OutResult) ? ACLSafetyFallbackResult::Success : ACLSafetyFallbackResult::Failure;
		}
	}

	return ACLSafetyFallbackResult::Ignored;
}

// @third party code - Epic Games Begin
void UAnimBoneCompressionCodec_ACL::PopulateDDCKey(const UE::Anim::Compression::FAnimDDCKeyArgs& KeyArgs, FArchive& Ar)
{
	Super::PopulateDDCKey(KeyArgs, Ar);

	acl::compression_settings Settings;
	GetCompressionSettings(Settings, KeyArgs.TargetPlatform);
// @third party code - Epic Games End

	uint32 ForceRebuildVersion = 1;
	uint32 SettingsHash = Settings.get_hash();

	Ar	<< SafetyFallbackThreshold << ForceRebuildVersion << SettingsHash;

	for (USkeletalMesh* SkelMesh : OptimizationTargets)
	{
		FSkeletalMeshModel* MeshModel = SkelMesh != nullptr ? SkelMesh->GetImportedModel() : nullptr;
		if (MeshModel != nullptr)
		{
			Ar << MeshModel->SkeletalMeshModelGUID;
		}
	}

	if (SafetyFallbackCodec != nullptr)
	{
// @third party code - Epic Games Begin
		SafetyFallbackCodec->PopulateDDCKey(KeyArgs, Ar);
// @third party code - Epic Games End
	}
}
#endif // WITH_EDITORONLY_DATA

UAnimBoneCompressionCodec* UAnimBoneCompressionCodec_ACL::GetCodec(const FString& DDCHandle)
{
	const FString ThisHandle = GetCodecDDCHandle();
	UAnimBoneCompressionCodec* CodecMatch = ThisHandle == DDCHandle ? this : nullptr;

	if (CodecMatch == nullptr && SafetyFallbackCodec != nullptr)
	{
		CodecMatch = SafetyFallbackCodec->GetCodec(DDCHandle);
	}

	return CodecMatch;
}

void UAnimBoneCompressionCodec_ACL::DecompressPose(FAnimSequenceDecompressionContext& DecompContext, const BoneTrackArray& RotationPairs, const BoneTrackArray& TranslationPairs, const BoneTrackArray& ScalePairs, TArrayView<FTransform>& OutAtoms) const
{
	const FACLCompressedAnimData& AnimData = static_cast<const FACLCompressedAnimData&>(DecompContext.CompressedAnimData);
	const acl::compressed_tracks* CompressedClipData = AnimData.GetCompressedTracks();
	check(CompressedClipData != nullptr && CompressedClipData->is_valid(false).empty());

	acl::decompression_context<UE4DefaultDecompressionSettings> ACLContext;
	ACLContext.initialize(*CompressedClipData);

	::DecompressPose(DecompContext, ACLContext, RotationPairs, TranslationPairs, ScalePairs, OutAtoms);
}

void UAnimBoneCompressionCodec_ACL::DecompressBone(FAnimSequenceDecompressionContext& DecompContext, int32 TrackIndex, FTransform& OutAtom) const
{
	const FACLCompressedAnimData& AnimData = static_cast<const FACLCompressedAnimData&>(DecompContext.CompressedAnimData);
	const acl::compressed_tracks* CompressedClipData = AnimData.GetCompressedTracks();
	check(CompressedClipData != nullptr && CompressedClipData->is_valid(false).empty());

	acl::decompression_context<UE4DefaultDecompressionSettings> ACLContext;
	ACLContext.initialize(*CompressedClipData);

	::DecompressBone(DecompContext, ACLContext, TrackIndex, OutAtom);
}

