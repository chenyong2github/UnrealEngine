// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaveOscillatorCameraShakePattern.h"

float FWaveOscillator::Initialize(float& OutInitialOffset) const
{
	OutInitialOffset = (InitialOffsetType == EInitialWaveOscillatorOffsetType::Random)
		? FMath::FRand() * (2.f * PI)
		: 0.f;
	return Amplitude * FMath::Sin(OutInitialOffset);
}

float FWaveOscillator::Update(float DeltaTime, float AmplitudeMultiplier, float FrequencyMultiplier, float& InOutCurrentOffset) const
{
	const float TotalAmplitude = Amplitude * AmplitudeMultiplier;
	if (TotalAmplitude != 0.f)
	{
		InOutCurrentOffset += DeltaTime * Frequency * FrequencyMultiplier * (2.f * PI);
		return TotalAmplitude * FMath::Sin(InOutCurrentOffset);
	}
	return 0.f;
}

UWaveOscillatorCameraShakePattern::UWaveOscillatorCameraShakePattern(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	// Default to only location shaking.
	RotationAmplitudeMultiplier = 0.f;
	FOV.Amplitude = 0.f;
}

void UWaveOscillatorCameraShakePattern::StartShakePatternImpl(const FCameraShakeStartParams& Params)
{
	if (!Params.bIsRestarting)
	{
		X.Initialize(LocationOffset.X);
		Y.Initialize(LocationOffset.Y);
		Z.Initialize(LocationOffset.Z);

		Pitch.Initialize(RotationOffset.X);
		Yaw.Initialize(RotationOffset.Y);
		Roll.Initialize(RotationOffset.Z);

		FOV.Initialize(FOVOffset);
	}
}

void UWaveOscillatorCameraShakePattern::UpdateShakePatternImpl(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult)
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

