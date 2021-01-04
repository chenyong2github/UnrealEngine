// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_MotionWarping.generated.h"

/** AnimNotifyState used to define a motion warping window in the animation */
UCLASS(meta = (DisplayName = "Motion Warping"))
class UAnimNotifyState_MotionWarping : public UAnimNotifyState
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Instanced, Category = "Config")
	class URootMotionModifierConfig* RootMotionModifierConfig;

	UAnimNotifyState_MotionWarping(const FObjectInitializer& ObjectInitializer);
};