// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_MotionWarping.generated.h"

class UMotionWarpingComponent;
class UAnimSequenceBase;
class URootMotionModifierConfig;

/** AnimNotifyState used to define a motion warping window in the animation */
UCLASS(meta = (DisplayName = "Motion Warping"))
class MOTIONWARPING_API UAnimNotifyState_MotionWarping : public UAnimNotifyState
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Config")
	URootMotionModifierConfig* RootMotionModifierConfig;

	UAnimNotifyState_MotionWarping(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintNativeEvent, Category = "Motion Warping")
	void AddRootMotionModifier(UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Motion Warping")
	void OnWarpBegin(UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Motion Warping")
	void OnWarpPreUpdate(UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Motion Warping")
	void OnWarpEnd(UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const;
};