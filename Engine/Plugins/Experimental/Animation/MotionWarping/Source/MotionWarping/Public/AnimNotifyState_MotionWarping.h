// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "RootMotionModifier.h"
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

	//@TODO: Prevent notify callbacks and add comments explaining why we don't use those here.

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Config")
	URootMotionModifierConfig* RootMotionModifierConfig;

	UAnimNotifyState_MotionWarping(const FObjectInitializer& ObjectInitializer);

	/** Called from the MotionWarpingComp when this notify becomes relevant. See: UMotionWarpingComponent::Update */
	FRootMotionModifierHandle OnBecomeRelevant(UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const;

	/** Creates a root motion modifier from the config class defined in the notify */
	UFUNCTION(BlueprintNativeEvent, Category = "Motion Warping")
	FRootMotionModifierHandle AddRootMotionModifier(UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const;

	UFUNCTION()
	void OnRootMotionModifierActivate(UMotionWarpingComponent* MotionWarpingComp, const FRootMotionModifierHandle& Handle);

	UFUNCTION()
	void OnRootMotionModifierUpdate(UMotionWarpingComponent* MotionWarpingComp, const FRootMotionModifierHandle& Handle);

	UFUNCTION()
	void OnRootMotionModifierDeactivate(UMotionWarpingComponent* MotionWarpingComp, const FRootMotionModifierHandle& Handle);

	UFUNCTION(BlueprintImplementableEvent, Category = "Motion Warping")
	void OnWarpBegin(UMotionWarpingComponent* MotionWarpingComp, const FRootMotionModifierHandle& Handle) const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Motion Warping")
	void OnWarpUpdate(UMotionWarpingComponent* MotionWarpingComp, const FRootMotionModifierHandle& Handle) const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Motion Warping")
	void OnWarpEnd(UMotionWarpingComponent* MotionWarpingComp, const FRootMotionModifierHandle& Handle) const;
};