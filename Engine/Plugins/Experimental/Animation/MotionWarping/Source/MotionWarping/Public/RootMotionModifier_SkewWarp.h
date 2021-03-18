// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RootMotionModifier.h"
#include "RootMotionModifier_SkewWarp.generated.h"

USTRUCT()
struct MOTIONWARPING_API FRootMotionModifier_SkewWarp : public FRootMotionModifier_Warp
{
	GENERATED_BODY()

public:

	FRootMotionModifier_SkewWarp(){}
	virtual ~FRootMotionModifier_SkewWarp() {}

	virtual UScriptStruct* GetScriptStruct() const { return FRootMotionModifier_SkewWarp::StaticStruct(); }
	virtual FTransform ProcessRootMotion(UMotionWarpingComponent& OwnerComp, const FTransform& InRootMotion, float DeltaSeconds) override;
};

UCLASS(meta = (DisplayName = "Skew Warp"))
class MOTIONWARPING_API URootMotionModifierConfig_SkewWarp : public URootMotionModifierConfig_Warp
{
	GENERATED_BODY()

public:

	URootMotionModifierConfig_SkewWarp(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer) {}

	virtual FRootMotionModifierHandle AddRootMotionModifierNew(UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const override
	{
		return URootMotionModifierConfig_SkewWarp::AddRootMotionModifierSkewWarp(MotionWarpingComp, Animation, StartTime, EndTime, 
			SyncPointName, WarpPointAnimProvider, WarpPointAnimTransform, WarpPointAnimBoneName,
			bWarpTranslation, bIgnoreZAxis, bWarpRotation, RotationType, WarpRotationTimeMultiplier);
	}

	UFUNCTION(BlueprintCallable, Category = "Motion Warping")
	static FRootMotionModifierHandle AddRootMotionModifierSkewWarp(UMotionWarpingComponent* InMotionWarpingComp, const UAnimSequenceBase* InAnimation, float InStartTime, float InEndTime, 
		FName InSyncPointName, EWarpPointAnimProvider InWarpPointAnimProvider, FTransform InWarpPointAnimTransform, FName InWarpPointAnimBoneName,
		bool bInWarpTranslation, bool bInIgnoreZAxis, bool bInWarpRotation, EMotionWarpRotationType InRotationType, float InWarpRotationTimeMultiplier = 1.f);
};