// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioDeviceManager.h"
#include "SoundModulationGenerator.h"
#include "SoundModulationProxy.h"


namespace AudioModulation
{
	// Forward Declarations
	class FAudioModulationSystem;
	class FModulatorGeneratorProxy;

	struct FModulationGeneratorSettings;

	// Modulator Ids
	using FGeneratorId = uint32;
	extern const FGeneratorId InvalidGeneratorId;

	using FGeneratorProxyMap = TMap<FGeneratorId, FModulatorGeneratorProxy>;
	using FGeneratorHandle = TProxyHandle<FGeneratorId, FModulatorGeneratorProxy, FModulationGeneratorSettings>;

	struct FModulationGeneratorSettings : public TModulatorBase<FGeneratorId>
	{
		FGeneratorPtr Generator;

		FModulationGeneratorSettings() = default;

		FModulationGeneratorSettings(const USoundModulationGenerator& InGenerator, Audio::FDeviceId InDeviceId)
			: TModulatorBase<FGeneratorId>(InGenerator.GetName(), InGenerator.GetUniqueID())
			, Generator(InGenerator.CreateInstance(InDeviceId))
		{
		}

		FModulationGeneratorSettings(const FModulationGeneratorSettings& InSettings)
			: TModulatorBase<FGeneratorId>(InSettings.GetName(), InSettings.GetId())
			, Generator(InSettings.Generator.IsValid() ? InSettings.Generator->Clone() : nullptr)
		{
		}

		FModulationGeneratorSettings(FModulationGeneratorSettings&& InSettings)
			: TModulatorBase<FGeneratorId>(InSettings.GetName(), InSettings.GetId())
			, Generator(MoveTemp(InSettings.Generator))
		{
		}
	};

	class FModulatorGeneratorProxy : public TModulatorProxyRefType<FGeneratorId, FModulatorGeneratorProxy, FModulationGeneratorSettings>
	{
		FGeneratorPtr Generator;

	public:
		FModulatorGeneratorProxy() = default;
		FModulatorGeneratorProxy(FModulationGeneratorSettings&& InSettings, FAudioModulationSystem& InModSystem);
		
		FModulatorGeneratorProxy& operator =(FModulationGeneratorSettings&& InSettings);

		float GetValue() const
		{
			return Generator->GetValue();
		}

		bool IsBypassed() const
		{
			return Generator->IsBypassed();
		}

		void Update(double InElapsed)
		{
			Generator->Update(InElapsed);
		}

		void PumpCommands()
		{
			Generator->PumpCommands();
		}

#if !UE_BUILD_SHIPPING
		TArray<FString> GetDebugCategories() const
		{
			TArray<FString> DebugCategories;
			DebugCategories.Add(TEXT("Name"));
			DebugCategories.Add(TEXT("Ref Count"));

			TArray<FString> GeneratorCategories;
			Generator->GetDebugCategories(GeneratorCategories);
			DebugCategories.Append(GeneratorCategories);

			return DebugCategories;
		}

		TArray<FString> GetDebugValues() const
		{
			TArray<FString> DebugValues;
			DebugValues.Add(GetName());
			DebugValues.Add(FString::FormatAsNumber(GetRefCount()));

			Generator->GetDebugValues(DebugValues);

			return DebugValues;
		}

		const FString& GetDebugName() const
		{
			return Generator->GetDebugName();
		}
#endif // !UE_BUILD_SHIPPING
	};
} // namespace AudioModulation