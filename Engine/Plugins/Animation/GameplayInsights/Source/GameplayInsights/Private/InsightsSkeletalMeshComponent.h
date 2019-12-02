// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "InsightsSkeletalMeshComponent.generated.h"

class FAnimationProvider;
struct FSkeletalMeshPoseMessage;
struct FSkeletalMeshInfo;

UCLASS()
class UInsightsSkeletalMeshComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	// Set this component up from a provider & message
	void SetPoseFromProvider(const FAnimationProvider& InProvider, const FSkeletalMeshPoseMessage& InMessage, const FSkeletalMeshInfo& SkeletalMeshInfo);

	// USkeletalMeshComponent interface
	virtual void InitAnim(bool bForceReInit) override;
};
