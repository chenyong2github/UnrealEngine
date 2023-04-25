// Copyright Epic Games, Inc. All Rights Reserved.
// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "AnimBoneCompressionCodec_ACL.h"

#if WITH_EDITORONLY_DATA
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
// @third party code - Epic Games Begin
	bAllowFrameRemoval = false;
	FrameRemovalThresholdType = ACLFrameRemovalThresholdType::ProportionOfFrames;
	RemovalProportion = 0.5f;
// @third party code - Epic Games End
#endif	// WITH_EDITORONLY_DATA
}

#if WITH_EDITORONLY_DATA
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

// @third party code - Epic Games Begin
void UAnimBoneCompressionCodec_ACL::PopulateDDCKey(const UE::Anim::Compression::FAnimDDCKeyArgs& KeyArgs, FArchive& Ar)
{
	Super::PopulateDDCKey(KeyArgs, Ar);

	acl::compression_settings Settings;
	GetCompressionSettings(Settings, KeyArgs.TargetPlatform);
// @third party code - Epic Games End

	uint32 ForceRebuildVersion = 1;
	uint32 SettingsHash = Settings.get_hash();

	Ar	<< ForceRebuildVersion << SettingsHash;

	for (USkeletalMesh* SkelMesh : OptimizationTargets)
	{
		FSkeletalMeshModel* MeshModel = SkelMesh != nullptr ? SkelMesh->GetImportedModel() : nullptr;
		if (MeshModel != nullptr)
		{
			Ar << MeshModel->SkeletalMeshModelGUID;
		}
	}
}
#endif // WITH_EDITORONLY_DATA

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

