// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraShakeTestObjects.h"

UConstantCameraShake::UConstantCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer
				.SetDefaultSubobjectClass<UConstantCameraShakePattern>(TEXT("RootShakePattern")))
	, LocationOffset(FVector::ZeroVector)
	, RotationOffset(FRotator::ZeroRotator)
{
}

UConstantCameraShakePattern::UConstantCameraShakePattern(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UConstantCameraShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
{
	const UConstantCameraShake* Shake = GetTypedOuter<UConstantCameraShake>();
	OutInfo.Duration = Shake->Duration;
}

void UConstantCameraShakePattern::UpdateShakePatternImpl(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult)
{
	const UConstantCameraShake* Shake = GetTypedOuter<UConstantCameraShake>();
	OutResult.Location = Shake->LocationOffset;
	OutResult.Rotation = Shake->RotationOffset;
}
