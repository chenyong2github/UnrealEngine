// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioDeviceManager.h"
#include "Containers/Queue.h"
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
		virtual ~IGenerator() = default;

		/** Allows child generator class to override default copy/update behavior when receiving an updated generator call
		  * from the audio thread. Useful for ignoring updates while a generator is running or deferring the transition
		  * to the new generator state to the modulation processing thread.  This enables interpolating between existing
		  * and new generator state, properties, avoiding discontinuities, etc.
		  * @param InGenerator - The constructed version of the generator being sent from the Audio Thread
		  * @return - True if this generator instance handled update using data from the instance provided.
		  * False if the modulation system should destroy this instance and replace it with the provided version.
		  */
		virtual bool UpdateGenerator(const IGenerator& InGenerator) = 0;

		/** Returns current value of the generator. */
		virtual float GetValue() const = 0;

		/** Returns whether or not the generator is bypassed. */
		virtual bool IsBypassed() const = 0;

		/** Pumps commands from Audio Thread to the generator's modulation processing thread.*/
		virtual void PumpCommands() = 0;

		/** Updates the generators value at the audio block rate on the modulation processing thread. */
		virtual void Update(double InElapsed) = 0;

#if !UE_BUILD_SHIPPING
		virtual void GetDebugValues(TArray<FString>& OutDebugValues) const = 0;

		virtual void GetDebugCategories(TArray<FString>& OutDebugCategories) const = 0;

		// Required for instance look-up in factory registration
		virtual const FString& GetDebugName() const = 0;
#endif // !UE_BUILD_SHIPPING
	};

	class AUDIOMODULATION_API FGeneratorBase : public IGenerator
	{
	public:
		FGeneratorBase() = default;
		FGeneratorBase(Audio::FDeviceId InDeviceId)
			: AudioDeviceId(InDeviceId)
		{
		}

		virtual ~FGeneratorBase() = default;

		virtual bool UpdateGenerator(const IGenerator& InGenerator) override { return false; }
		virtual void PumpCommands() override;


	protected:
		void AudioRenderThreadCommand(TUniqueFunction<void()>&& InCommand);

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
