// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationGeneratorProxy.h"

#include "AudioModulation.h"
#include "AudioModulationSystem.h"


namespace AudioModulation
{
	const FGeneratorId InvalidGeneratorId = INDEX_NONE;

	FModulatorGeneratorProxy::FModulatorGeneratorProxy(const FModulationGeneratorSettings& InSettings, FAudioModulationSystem& InModSystem)
		: TModulatorProxyRefType(InSettings.GetName(), InSettings.GetId(), InModSystem)
		, Generator(InSettings.Generator)
	{
	}

	FModulatorGeneratorProxy& FModulatorGeneratorProxy::operator =(const FModulationGeneratorSettings& InSettings)
	{
		Generator = InSettings.Generator;
		return *this;
	}

} // namespace AudioModulation
