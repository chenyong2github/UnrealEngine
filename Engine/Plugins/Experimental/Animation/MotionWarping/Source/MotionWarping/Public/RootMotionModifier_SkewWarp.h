// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RootMotionModifier.h"
#include "RootMotionModifier_SkewWarp.generated.h"

//DEPRECATED will be removed shortly
UCLASS()
class MOTIONWARPING_API URootMotionModifierConfig_SkewWarp : public UObject
{
	GENERATED_BODY()

public:

	URootMotionModifierConfig_SkewWarp(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}

	UE_DEPRECATED(5.0, "Please use UMotionModifier_SkewWarp::AddRootMotionModifierSkewWarp instead")
	UFUNCTION(BlueprintCallable, Category = "Motion Warping", meta = (DeprecatedFunction))
	static FRootMotionModifierHandle AddRootMotionModifierSkewWarp(UMotionWarpingComponent* InMotionWarpingComp, const UAnimSequenceBase* InAnimation, float InStartTime, float InEndTime, 
		FName InSyncPointName, EWarpPointAnimProvider InWarpPointAnimProvider, FTransform InWarpPointAnimTransform, FName InWarpPointAnimBoneName,
		bool bInWarpTranslation, bool bInIgnoreZAxis, bool bInWarpRotation, EMotionWarpRotationType InRotationType, float InWarpRotationTimeMultiplier = 1.f);
};

//////////////////////////////////////////////

UCLASS()
class MOTIONWARPING_API URootMotionModifier_SkewWarp : public URootMotionModifier_Warp
{
	GENERATED_BODY()

public:

	URootMotionModifier_SkewWarp(const FObjectInitializer& ObjectInitializer);

	virtual FTransform ProcessRootMotion(const FTransform& InRootMotion, float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Motion Warping")
	static URootMotionModifier_SkewWarp* AddRootMotionModifierSkewWarp(UMotionWarpingComponent* InMotionWarpingComp, const UAnimSequenceBase* InAnimation, float InStartTime, float InEndTime,
		FName InWarpTargetName, EWarpPointAnimProvider InWarpPointAnimProvider, FTransform InWarpPointAnimTransform, FName InWarpPointAnimBoneName,
		bool bInWarpTranslation, bool bInIgnoreZAxis, bool bInWarpRotation, EMotionWarpRotationType InRotationType, float InWarpRotationTimeMultiplier = 1.f);
};