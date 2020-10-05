// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioDeviceManager.h"
#include "CoreMinimal.h"
#include "DSP/LFO.h"
#include "IAudioModulation.h"
#include "SoundModulationValue.h"
#include "Templates/SharedPointer.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "SoundModulationGenerator.generated.h"


namespace AudioModulation
{
	class AUDIOMODULATION_API IGenerator
	{
	public:
		IGenerator(Audio::FDeviceId InDeviceId = INDEX_NONE)
			: AudioDeviceId(InDeviceId)
		{
		}
		virtual ~IGenerator() = default;

		virtual float GetValue() const { return 1.0f; }
		virtual bool IsBypassed() const { return true; };
		virtual void Update(double InElapsed) { };


#if !UE_BUILD_SHIPPING
		virtual void GetDebugValues(TArray<FString>& OutDebugValues) const { }

		virtual void GetDebugCategories(TArray<FString>& OutDebugCategories) const { }

		// Required for instance look-up in factory registration
		virtual const FString& GetDebugName() const { static const FString Invalid; return Invalid; }
#endif // !UE_BUILD_SHIPPING

		void AudioRenderThreadCommand(TUniqueFunction<void()>&& InCommand);
		void PumpCommands();

	protected:
		Audio::FDeviceId AudioDeviceId = INDEX_NONE;

	private:
		TQueue<TUniqueFunction<void()>> CommandQueue;
	};

	using FGeneratorPtr = TSharedPtr<IGenerator, ESPMode::ThreadSafe>;
} // namespace AudioModulation

/**
 * Base class for modulators that algorithmically generate values that can effect
 * various endpoints (ex. Control Buses & Parameter Destinations)
 */
UCLASS(hideCategories = Object, abstract)
class AUDIOMODULATION_API USoundModulationGenerator : public USoundModulatorBase
{
	GENERATED_BODY()

public:
	virtual AudioModulation::FGeneratorPtr CreateInstance(Audio::FDeviceId AudioDeviceId) const
	{
		return nullptr;
	}

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent) override;
#endif // WITH_EDITOR

	virtual void BeginDestroy() override;
};
