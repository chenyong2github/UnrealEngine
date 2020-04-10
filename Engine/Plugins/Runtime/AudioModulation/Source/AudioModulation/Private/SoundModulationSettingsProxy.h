// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IAudioModulation.h"
#include "SoundModulationPatchProxy.h"
#include "SoundModulationProxy.h"


// Forward Declarations
class USoundModulationSettings;

namespace AudioModulation
{
	// Forward Declarations
	class FAudioModulationSystem;

	class FModulationSettingsProxy : public TModulatorProxyBase<uint32>
	{
	public:
		FModulationSettingsProxy();
		FModulationSettingsProxy(const USoundModulationSettings& Settings, FAudioModulationSystem& InModSystem);

		FModulationPatchProxy Volume;
		FModulationPatchProxy Pitch;
		FModulationPatchProxy Lowpass;
		FModulationPatchProxy Highpass;

		TMap<FName, FModulationPatchProxy> Controls;
	};
} // namespace AudioModulation