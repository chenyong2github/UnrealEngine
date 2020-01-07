// Copyright Epic Games, Inc. All Rights Reserved.


#include "Sound/SoundNodeAttenuation.h"
#include "EngineDefines.h"
#include "ActiveSound.h"
#include "AudioDevice.h"

/*-----------------------------------------------------------------------------
	USoundNodeAttenuation implementation.
-----------------------------------------------------------------------------*/

USoundNodeAttenuation::USoundNodeAttenuation(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

float USoundNodeAttenuation::GetMaxDistance() const 
{ 
	float MaxDistance = WORLD_MAX;
	const FSoundAttenuationSettings* Settings = GetAttenuationSettingsToApply();
	if (Settings)
	{
		MaxDistance = Settings->GetMaxDimension();
	}

	for (USoundNode* ChildNode : ChildNodes)
	{
		if (ChildNode)
		{
			ChildNode->ConditionalPostLoad();
			MaxDistance = FMath::Max(ChildNode->GetMaxDistance(), MaxDistance);
		}
	}
	return MaxDistance;
}

const FSoundAttenuationSettings* USoundNodeAttenuation::GetAttenuationSettingsToApply() const
{
	const FSoundAttenuationSettings* Settings = nullptr;

	if (bOverrideAttenuation)
	{
		Settings = &AttenuationOverrides;
	}
	else if (AttenuationSettings)
	{
		Settings = &AttenuationSettings->Attenuation;
	}

	return Settings;
}

void USoundNodeAttenuation::ParseNodes(FAudioDevice* AudioDevice, const UPTRINT NodeWaveInstanceHash, FActiveSound& ActiveSound, const FSoundParseParameters& ParseParams, TArray<FWaveInstance*>& WaveInstances)
{
	FSoundParseParameters UpdatedParseParams = ParseParams;

	const FSoundAttenuationSettings* Settings = (ActiveSound.bAllowSpatialization ? GetAttenuationSettingsToApply() : nullptr);
	if (Settings)
	{
		// Update this node's attenuation settings overrides
		check(AudioDevice);
		const int32 ClosestListenerIndex = AudioDevice->FindClosestListenerIndex(UpdatedParseParams.Transform);
		ActiveSound.ParseAttenuation(UpdatedParseParams, ClosestListenerIndex, *Settings);
	}
	else
	{
		UpdatedParseParams.bUseSpatialization = false;
	}

	Super::ParseNodes(AudioDevice, NodeWaveInstanceHash, ActiveSound, UpdatedParseParams, WaveInstances);
}