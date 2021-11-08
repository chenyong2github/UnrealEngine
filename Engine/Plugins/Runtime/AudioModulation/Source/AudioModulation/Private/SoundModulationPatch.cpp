// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationPatch.h"

#include "AudioDefines.h"
#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationStatics.h"
#include "AudioModulationSystem.h"
#include "SoundControlBus.h"
#include "SoundModulationTransform.h"
#include "SoundModulatorAssetProxy.h"


#define LOCTEXT_NAMESPACE "SoundModulationPatch"


USoundModulationPatch::USoundModulationPatch(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TUniquePtr<Audio::IProxyData> USoundModulationPatch::CreateNewProxyData(const Audio::FProxyDataInitParams& InitParams)
{
	using namespace AudioModulation;
	return MakeUnique<TSoundModulatorAssetProxy<USoundModulationPatch>>(*this);
}

#if WITH_EDITOR
void USoundModulationPatch::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	AudioModulation::IterateModulationImpl([this](AudioModulation::FAudioModulation& OutModulation)
	{
		OutModulation.UpdateModulator(*this);
	});

	Super::PostEditChangeProperty(InPropertyChangedEvent);
}

void USoundModulationPatch::PostEditChangeChainProperty(FPropertyChangedChainEvent& InPropertyChangedEvent)
{
	AudioModulation::IterateModulationImpl([this](AudioModulation::FAudioModulation& OutModulation)
	{
		OutModulation.UpdateModulator(*this);
	});

	Super::PostEditChangeChainProperty(InPropertyChangedEvent);
}
#endif // WITH_EDITOR

FSoundControlModulationInput::FSoundControlModulationInput()
	: bSampleAndHold(0)
{
}

const USoundControlBus* FSoundControlModulationInput::GetBus() const
{
	return Bus;
}

const USoundControlBus& FSoundControlModulationInput::GetBusChecked() const
{
	check(Bus);
	return *Bus;
}
#undef LOCTEXT_NAMESPACE // SoundModulationPatch
