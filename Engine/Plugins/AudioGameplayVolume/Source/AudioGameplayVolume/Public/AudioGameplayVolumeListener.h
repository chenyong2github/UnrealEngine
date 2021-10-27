// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sound/AudioVolume.h"

// Forward Declarations 
struct FAudioProxyMutatorSearchResult;

/**
 *  FInterpolatedInteriorSettings
 */
class FInterpolatedInteriorSettings
{
public:

	FInterpolatedInteriorSettings() = default;

	// Set a new interior settings target
	void Apply(const FInteriorSettings& NewSettings);

	// Interpolates between the source and end
	float Interpolate(double CurrentTime, double EndTime) const;

	// Updates the current state of the interior settings
	void UpdateInteriorValues();

	FORCEINLINE void SetInteriorStartTime(double InTime) { InteriorStartTime = InTime; }

	FORCEINLINE double GetInteriorStartTime() const { return InteriorStartTime; }

	FORCEINLINE float GetInteriorVolume() const { return InteriorSettings.InteriorVolume; }
	FORCEINLINE float GetInteriorVolumeInterp() const { return InteriorVolumeInterp; }

	FORCEINLINE float GetExteriorVolume() const { return InteriorSettings.ExteriorVolume; }
	FORCEINLINE float GetExteriorVolumeInterp() const { return ExteriorVolumeInterp; }

	FORCEINLINE float GetInteriorLPF() const { return InteriorSettings.InteriorLPF; }
	FORCEINLINE float GetInteriorLPFInterp() const { return InteriorLPFInterp; }

	FORCEINLINE float GetExteriorLPF() const { return InteriorSettings.ExteriorLPF; }
	FORCEINLINE float GetExteriorLPFInterp() const { return ExteriorLPFInterp; }

	FORCEINLINE bool IsWorldSettings() const { return InteriorSettings.bIsWorldSettings; }

protected:

	double InteriorStartTime = 0.0;

	double InteriorEndTime = 0.0;
	double ExteriorEndTime = 0.0;

	double InteriorLPFEndTime = 0.0;
	double ExteriorLPFEndTime = 0.0;

	float InteriorVolumeInterp = 0.f;
	float ExteriorVolumeInterp = 0.f;

	float InteriorLPFInterp = 0.f;
	float ExteriorLPFInterp = 0.f;

	FInteriorSettings InteriorSettings;
};

/**
 *  FAudioGameplayVolumeListener
 */
class FAudioGameplayVolumeListener
{
public:

	FAudioGameplayVolumeListener() = default;

	void Update(const FAudioProxyMutatorSearchResult& Result, const FVector& InPosition, uint32 InWorldID);

	const TSet<uint32>& GetCurrentProxies() const { return CurrentProxies; }

	bool GetAffectedByLegacySystem() const { return bAffectedByLegacySystem; }
	void SetAffectedByLegacySystem(bool bIsAffected) { bAffectedByLegacySystem = bIsAffected; }

	const FInterpolatedInteriorSettings& GetInteriorSettings() const { return InteriorSettings; }

protected:

	FInterpolatedInteriorSettings InteriorSettings;

	FVector Position = FVector::ZeroVector;
	uint32 WorldID = INDEX_NONE;
	bool bNewListener = true;
	bool bAffectedByLegacySystem = false;

	TSet<uint32> CurrentProxies;
	TSet<uint32> PreviousProxies;
};

