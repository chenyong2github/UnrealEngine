// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationGeneratorProxy.h"

#include "AudioModulation.h"
#include "AudioModulationSystem.h"


namespace AudioModulation
{
	const FGeneratorId InvalidGeneratorId = INDEX_NONE;

	FModulatorGeneratorProxy::FModulatorGeneratorProxy(FModulationGeneratorSettings&& InSettings, FAudioModulationSystem& InModSystem)
		: TModulatorProxyRefType(InSettings.GetName(), InSettings.GetId(), InModSystem)
		, Generator(MoveTemp(InSettings.Generator))
	{
	}

	FModulatorGeneratorProxy& FModulatorGeneratorProxy::operator =(FModulationGeneratorSettings&& InSettings)
	{
		if (ensure(InSettings.Generator.IsValid()))
		{
			Generator->UpdateGenerator(MoveTemp(InSettings.Generator));
		}
		else
		{
			Generator.Reset();
		}

		return *this;
	}

} // namespace AudioModulation
