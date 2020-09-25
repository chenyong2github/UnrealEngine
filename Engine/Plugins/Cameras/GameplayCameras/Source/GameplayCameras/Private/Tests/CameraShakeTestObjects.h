// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Camera/CameraShakeBase.h"
#include "CameraShakeTestObjects.generated.h"

UCLASS()
class UConstantCameraShakePattern : public UCameraShakePattern
{
public:

	GENERATED_BODY()

	UConstantCameraShakePattern(const FObjectInitializer& ObjectInitializer);

private:
	virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override;
	virtual void UpdateShakePatternImpl(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult) override;
};

UCLASS()
class UConstantCameraShake : public UCameraShakeBase
{
public:

	GENERATED_BODY()

	UConstantCameraShake(const FObjectInitializer& ObjectInitializer);

	UPROPERTY()
	FVector LocationOffset;

	UPROPERTY()
	FRotator RotationOffset;

	UPROPERTY()
	float Duration;
};
