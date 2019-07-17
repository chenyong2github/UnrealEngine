// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/ObjectMacros.h"
#include "LiveLinkRetargetAsset.h"
#include "WindowsMixedRealityHandTrackingLiveLinkRemapAsset.generated.h"

UENUM()
enum class EQuatSwizzleAxis : uint8
{
	X		UMETA(DisplayName = "X"),
	Y		UMETA(DisplayName = "Y"),
	Z		UMETA(DisplayName = "Z"),
	W		UMETA(DisplayName = "W"),
	MinusX	UMETA(DisplayName = "-X"),
	MinusY	UMETA(DisplayName = "-Y"),
	MinusZ	UMETA(DisplayName = "-Z"),
	MinusW	UMETA(DisplayName = "-W")
};

int Sign(const EQuatSwizzleAxis& QuatSwizzleAxis);

/**
  * WindowsMixedReality HandTracking LiveLink remapping asset
  */
UCLASS(Blueprintable)
class UWindowsMixedRealityHandTrackingLiveLinkRemapAsset :
	public ULiveLinkRetargetAsset
{
	GENERATED_UCLASS_BODY()

public:
	virtual void BuildPoseFromAnimationData(float DeltaTime, const FLiveLinkSkeletonStaticData* InSkeletonData, const FLiveLinkAnimationFrameData* InFrameData, FCompactPose& OutPose) override;

	/** If true, remap the full human hand skeleton including metacarpals */
	UPROPERTY(EditAnywhere, Category = "LiveLink")
	bool bHasMetacarpals = true;

	/** Only apply the orientation to each bone */
	UPROPERTY(EditAnywhere, Category = "LiveLink")
	bool bRetargetRotationOnly = false;

	/** Reorient the skeleton (swizzle the quaternion) to adjust for base skeleton and incoming skeleton differences */
	UPROPERTY(EditAnywhere, Category = "LiveLink")
	EQuatSwizzleAxis SwizzleX = EQuatSwizzleAxis::X;
	UPROPERTY(EditAnywhere, Category = "LiveLink")
	EQuatSwizzleAxis SwizzleY = EQuatSwizzleAxis::Y;
	UPROPERTY(EditAnywhere, Category = "LiveLink")
	EQuatSwizzleAxis SwizzleZ = EQuatSwizzleAxis::Z;
	UPROPERTY(EditAnywhere, Category = "LiveLink")
	EQuatSwizzleAxis SwizzleW = EQuatSwizzleAxis::W;

	UPROPERTY(EditAnywhere, Category="LiveLink")
	TMap<FName, FName> HandTrackingBoneNameMap;

#if WITH_EDITORONLY_DATA
	virtual void PostInitProperties() override;
#endif

private:
	FName GetRemappedBoneName(FName BoneName) const;

	FTransform GetRetargetedTransform(const FLiveLinkAnimationFrameData* InFrameData, int TransformIndex) const;
};
