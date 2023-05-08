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

/** Uses the open source Animation Compression Library with default settings suitable for general purpose animations. */
UCLASS(MinimalAPI, config = Engine, meta = (DisplayName = "Anim Compress ACL"))
class UAnimBoneCompressionCodec_ACL : public UAnimBoneCompressionCodec_ACLBase
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITORONLY_DATA
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
	// UAnimBoneCompressionCodec implementation
// @third party code - Epic Games Begin
	virtual void PopulateDDCKey(const UE::Anim::Compression::FAnimDDCKeyArgs& KeyArgs, FArchive& Ar) override;
// @third party code - Epic Games End

	// UAnimBoneCompressionCodec_ACLBase implementation
// @third party code - Epic Games Begin
	virtual void GetCompressionSettings(acl::compression_settings& OutSettings, const ITargetPlatform* TargetPlatform) const override;
// @third party code - Epic Games End
	virtual TArray<class USkeletalMesh*> GetOptimizationTargets() const override { return OptimizationTargets; }
#endif

	// UAnimBoneCompressionCodec implementation
	virtual void DecompressPose(FAnimSequenceDecompressionContext& DecompContext, const BoneTrackArray& RotationPairs, const BoneTrackArray& TranslationPairs, const BoneTrackArray& ScalePairs, TArrayView<FTransform>& OutAtoms) const override;
	virtual void DecompressBone(FAnimSequenceDecompressionContext& DecompContext, int32 TrackIndex, FTransform& OutAtom) const override;
};
