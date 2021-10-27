// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioGameplayVolumeListener.h"
#include "AudioGameplayVolumeProxyMutator.h"
#include "Misc/App.h"

void FInterpolatedInteriorSettings::Apply(const FInteriorSettings& NewSettings)
{
	if (InteriorSettings != NewSettings)
	{
		InteriorStartTime = FApp::GetCurrentTime();
		if (NewSettings.bIsWorldSettings)
		{
			// If we're going to default world settings, use our previous / current interpolation times.
			InteriorEndTime = InteriorStartTime + InteriorSettings.InteriorTime;
			ExteriorEndTime = InteriorStartTime + InteriorSettings.ExteriorTime;
			InteriorLPFEndTime = InteriorStartTime + InteriorSettings.InteriorLPFTime;
			ExteriorLPFEndTime = InteriorStartTime + InteriorSettings.ExteriorLPFTime;
		}
		else
		{
			InteriorEndTime = InteriorStartTime + NewSettings.InteriorTime;
			ExteriorEndTime = InteriorStartTime + NewSettings.ExteriorTime;
			InteriorLPFEndTime = InteriorStartTime + NewSettings.InteriorLPFTime;
			ExteriorLPFEndTime = InteriorStartTime + NewSettings.ExteriorLPFTime;
		}

		InteriorSettings = NewSettings;
	}
}

float FInterpolatedInteriorSettings::Interpolate(double CurrentTime, double EndTime) const
{
	if (CurrentTime < InteriorStartTime)
	{
		return 0.0f;
	}

	if (CurrentTime >= EndTime)
	{
		return 1.0f;
	}

	float InterpValue = (float)((CurrentTime - InteriorStartTime) / (EndTime - InteriorStartTime));
	return FMath::Clamp(InterpValue, 0.0f, 1.0f);
}

void FInterpolatedInteriorSettings::UpdateInteriorValues()
{
	// Store the interpolation value, not the actual value
	double CurrentTime = FApp::GetCurrentTime();
	InteriorVolumeInterp = Interpolate(CurrentTime, InteriorEndTime);
	ExteriorVolumeInterp = Interpolate(CurrentTime, ExteriorEndTime);
	InteriorLPFInterp = Interpolate(CurrentTime, InteriorLPFEndTime);
	ExteriorLPFInterp = Interpolate(CurrentTime, ExteriorLPFEndTime);
}

void FAudioGameplayVolumeListener::Update(const FAudioProxyMutatorSearchResult& Result, const FVector& InPosition, uint32 InWorldID)
{
	check(IsInAudioThread());

	// If we have a different worldID, this is a new listener; we'll need to exit our previous proxies and enter our current ones
	if (WorldID != InWorldID)
	{
		bNewListener = true;
	}

	Position = InPosition;
	WorldID = InWorldID;

	Swap(PreviousProxies, CurrentProxies);
	CurrentProxies = Result.VolumeSet;

	TSet<uint32> EnteredProxies = CurrentProxies.Difference(PreviousProxies);
	TSet<uint32> ExitedProxies = PreviousProxies.Difference(CurrentProxies);

	if (bNewListener || EnteredProxies.Num() || ExitedProxies.Num())
	{
		// Reapply interior from mutators
		InteriorSettings.Apply(Result.InteriorSettings);
	}

	// Update interpolation
	InteriorSettings.UpdateInteriorValues();

	bNewListener = false;
}
