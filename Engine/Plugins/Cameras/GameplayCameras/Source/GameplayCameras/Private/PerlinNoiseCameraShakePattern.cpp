// Copyright Epic Games, Inc. All Rights Reserved.

#include "PerlinNoiseCameraShakePattern.h"

float FPerlinNoiseShaker::Update(float DeltaTime, float AmplitudeMultiplier, float FrequencyMultiplier, float& InOutCurrentOffset) const
{
	const float TotalAmplitude = Amplitude * AmplitudeMultiplier;
	if (TotalAmplitude != 0.f)
	{
		InOutCurrentOffset += DeltaTime * Frequency * FrequencyMultiplier;
		return TotalAmplitude * FMath::PerlinNoise1D(InOutCurrentOffset);
	}
	return 0.f;
}

UPerlinNoiseCameraShakePattern::UPerlinNoiseCameraShakePattern(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	// Default to only location shaking.
	RotationAmplitudeMultiplier = 0.f;
	FOV.Amplitude = 0.f;
}

void UPerlinNoiseCameraShakePattern::StartShakePatternImpl(const FCameraShakeStartParams& Params)
{
	if (!Params.bIsRestarting)
	{
		// All offsets are random. This is because the core perlin noise implementation
		// uses permutation tables, so if two shakers have the same initial offset and the same
		// frequency, they will have the same exact values.
		LocationOffset = FVector((float)FMath::RandHelper(255), (float)FMath::RandHelper(255), (float)FMath::RandHelper(255));
		RotationOffset = FVector((float)FMath::RandHelper(255), (float)FMath::RandHelper(255), (float)FMath::RandHelper(255));
		FOVOffset = (float)FMath::RandHelper(255);
	}
}

void UPerlinNoiseCameraShakePattern::UpdateShakePatternImpl(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult)
{
	const float DeltaTime = Params.DeltaTime;

	OutResult.Location.X = X.Update(DeltaTime, LocationAmplitudeMultiplier, LocationFrequencyMultiplier, LocationOffset.X);
	OutResult.Location.Y = Y.Update(DeltaTime, LocationAmplitudeMultiplier, LocationFrequencyMultiplier, LocationOffset.Y);
	OutResult.Location.Z = Z.Update(DeltaTime, LocationAmplitudeMultiplier, LocationFrequencyMultiplier, LocationOffset.Z);

	OutResult.Rotation.Pitch = Pitch.Update(DeltaTime, RotationAmplitudeMultiplier, RotationFrequencyMultiplier, RotationOffset.X);
	OutResult.Rotation.Yaw   = Yaw.Update(  DeltaTime, RotationAmplitudeMultiplier, RotationFrequencyMultiplier, RotationOffset.Y);
	OutResult.Rotation.Roll  = Roll.Update( DeltaTime, RotationAmplitudeMultiplier, RotationFrequencyMultiplier, RotationOffset.Z);

	OutResult.FOV = FOV.Update(DeltaTime, 1.f, 1.f, FOVOffset);
}

