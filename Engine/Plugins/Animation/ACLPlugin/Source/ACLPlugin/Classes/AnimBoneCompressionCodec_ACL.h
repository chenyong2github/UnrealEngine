// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "AnimBoneCompressionCodec_ACLBase.h"
#include "PerPlatformProperties.h"
#include "AnimBoneCompressionCodec_ACL.generated.h"

// @third party code - Epic Games Begin
UENUM()
enum class ACLFrameRemovalThresholdType: uint8
{
	ProportionOfFrames UMETA(DisplayName = "Proportion"),
	DistanceError UMETA(DisplayName = "Distance")
};

class ITargetPlatform;
// @third party code - Epic Games End

/** The default codec implementation for ACL support with the minimal set of exposed features for ease of use. */
UCLASS(MinimalAPI, config = Engine, meta = (DisplayName = "Anim Compress ACL"))
class UAnimBoneCompressionCodec_ACL : public UAnimBoneCompressionCodec_ACLBase
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, Category = "ACL Options", Instanced, meta = (EditInline))
	TObjectPtr<UAnimBoneCompressionCodec> SafetyFallbackCodec;

#if WITH_EDITORONLY_DATA
	/** The error threshold after which we fallback on a safer encoding. */
	UPROPERTY(EditAnywhere, Category = "ACL Options", meta = (ClampMin = "0"))
	float SafetyFallbackThreshold;

	/** The skeletal meshes used to estimate the skinning deformation during compression. */
	UPROPERTY(EditAnywhere, Category = "ACL Options")
	TArray<TObjectPtr<class USkeletalMesh>> OptimizationTargets;

// @third party code - Epic Games Begin
	/** Enable removal of the least important frames in the animation */
	UPROPERTY(EditAnywhere, Category = "Frame Removal")
	bool bAllowFrameRemoval;

	UPROPERTY(EditAnywhere, Category = "Frame Removal", meta = (EditCondition = "bAllowFrameRemoval", EditConditionHides))
	ACLFrameRemovalThresholdType FrameRemovalThresholdType;

	/** Proportion of frames to remove (0.0 - 1.0)*/
	UPROPERTY(EditAnywhere, Category = "Frame Removal", meta = (EditCondition = "bAllowFrameRemoval && FrameRemovalThresholdType == ACLFrameRemovalThresholdType::ProportionOfFrames", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	FPerPlatformFloat RemovalProportion;

	/** Remove frames until worst distance error reaches this number of cm*/
	UPROPERTY(EditAnywhere, Category = "Frame Removal", meta = (EditCondition = "bAllowFrameRemoval && FrameRemovalThresholdType == ACLFrameRemovalThresholdType::DistanceError", EditConditionHides, ClampMin = "0", ClampMax = "100"))
	FPerPlatformFloat RemovalDistanceError;
// @third party code - Epic Games End

	//////////////////////////////////////////////////////////////////////////
	// UObject implementation
	virtual void PostInitProperties() override;
	virtual void GetPreloadDependencies(TArray<UObject*>& OutDeps) override;

	// UAnimBoneCompressionCodec implementation
	virtual bool IsCodecValid() const override;
// @third party code - Epic Games Begin
	virtual void PopulateDDCKey(const UE::Anim::Compression::FAnimDDCKeyArgs& KeyArgs, FArchive& Ar) override;
// @third party code - Epic Games End

	// UAnimBoneCompressionCodec_ACLBase implementation
// @third party code - Epic Games Begin
	virtual void GetCompressionSettings(acl::compression_settings& OutSettings, const ITargetPlatform* TargetPlatform) const override;
// @third party code - Epic Games End
	virtual TArray<class USkeletalMesh*> GetOptimizationTargets() const override { return OptimizationTargets; }
	virtual ACLSafetyFallbackResult ExecuteSafetyFallback(acl::iallocator& Allocator, const acl::compression_settings& Settings, const acl::track_array_qvvf& RawClip, const acl::track_array_qvvf& BaseClip, const acl::compressed_tracks& CompressedClipData, const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult);
#endif

	// UAnimBoneCompressionCodec implementation
	virtual UAnimBoneCompressionCodec* GetCodec(const FString& DDCHandle);
	virtual void DecompressPose(FAnimSequenceDecompressionContext& DecompContext, const BoneTrackArray& RotationPairs, const BoneTrackArray& TranslationPairs, const BoneTrackArray& ScalePairs, TArrayView<FTransform>& OutAtoms) const override;
	virtual void DecompressBone(FAnimSequenceDecompressionContext& DecompContext, int32 TrackIndex, FTransform& OutAtom) const override;
};
