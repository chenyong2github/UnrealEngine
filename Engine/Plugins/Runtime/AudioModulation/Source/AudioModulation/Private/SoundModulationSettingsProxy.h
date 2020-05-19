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

	class FSoundModulationSettings : public TModulatorBase<uint32>
	{
	public:
		FSoundModulationSettings() = default;
		FSoundModulationSettings(const USoundModulationSettings& InSettings);

		FModulationPatchSettings Volume;
		FModulationPatchSettings Pitch;
		FModulationPatchSettings Lowpass;
		FModulationPatchSettings Highpass;
	};

	class FModulationSettingsProxy : public TModulatorBase<uint32>
	{
	public:
		FModulationSettingsProxy();
		FModulationSettingsProxy(const FSoundModulationSettings& InSettings, FAudioModulationSystem& OutModSystem);

		FModulationPatchProxy Volume;
		FModulationPatchProxy Pitch;
		FModulationPatchProxy Lowpass;
		FModulationPatchProxy Highpass;
	};
} // namespace AudioModulation