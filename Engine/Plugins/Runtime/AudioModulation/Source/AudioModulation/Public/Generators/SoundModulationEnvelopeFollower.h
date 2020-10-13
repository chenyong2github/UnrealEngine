// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "SoundModulationGenerator.h"

#include "AudioDeviceManager.h"
#include "DSP/BufferVectorOperations.h"
#include "DSP/EnvelopeFollower.h"
#include "DSP/MultithreadedPatching.h"
#include "IAudioModulation.h"
#include "SoundModulationGenerator.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "SoundModulationEnvelopeFollower.generated.h"


// Forward Declarations
class UAudioBus;

USTRUCT(BlueprintType)
struct FEnvelopeFollowerGeneratorParams
{
	GENERATED_USTRUCT_BODY()

	/** If true, bypasses generator from being modulated by parameters, patches, or mixed (remains active and computed). */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite)
	bool bBypass = false;

	/** If true, inverts output */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (EditCondition = "!bBypass", DisplayAfter = "ReleaseTime"))
	bool bInvert = false;

	/** AudioBus to follow amplitude of and generate modulation control signal from. */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (EditCondition = "!bBypass"))
	UAudioBus* AudioBus = nullptr;

	/** Gain to apply to amplitude signal. */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (EditCondition = "!bBypass", ClampMin = 0.0f))
	float Gain = 1.0f;

	/** Attack time of envelope response (in sec) */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (EditCondition = "!bBypass", ClampMin = 0.0f))
	float AttackTime = 0.010f;

	/** Release time of envelope response (in sec) */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (EditCondition = "!bBypass", ClampMin = 0.0f))
	float ReleaseTime = 0.100f;
};

namespace AudioModulation
{
	class AUDIOMODULATION_API FEnvelopeFollowerGenerator : public IGenerator
	{
	public:
		FEnvelopeFollowerGenerator(const FEnvelopeFollowerGeneratorParams& InParams, Audio::FDeviceId InDeviceId);
		virtual ~FEnvelopeFollowerGenerator() = default;

		virtual float GetValue() const override;
		virtual bool IsBypassed() const override;
		virtual void Update(double InElapsed) override;

#if !UE_BUILD_SHIPPING
		static const FString DebugName;

		virtual void GetDebugCategories(TArray<FString>& OutDebugCategories) const override;
		virtual void GetDebugValues(TArray<FString>& OutDebugValues) const override;
		virtual const FString& GetDebugName() const override;
#endif // !UE_BUILD_SHIPPING

	protected:
		FEnvelopeFollowerGeneratorParams Params;

	private:
		Audio::FPatchOutputStrongPtr AudioBusPatch;
		Audio::FAlignedFloatBuffer TempBuffer;
		Audio::FEnvelopeFollower EnvelopeFollower;

		float CurrentValue = 0.0f;
	};
} // namespace AudioModulation

UCLASS(hidecategories = Object, BlueprintType, editinlinenew, meta = (DisplayName = "Envelope Follower Generator"))
class AUDIOMODULATION_API USoundModulationGeneratorEnvelopeFollower : public USoundModulationGenerator
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (ShowOnlyInnerProperties))
	FEnvelopeFollowerGeneratorParams Params;

#if !UE_BUILD_SHIPPING
	static const TArray<FString>& GetDebugCategories()
	{
		static const TArray<FString> Categories =
		{
			TEXT("Value"),
			TEXT("Gain"),
			TEXT("Attack"),
			TEXT("Release"),
		};
		return Categories;
	}

	static const FString& GetDebugName();
#endif // !UE_BUILD_SHIPPING

	virtual AudioModulation::FGeneratorPtr CreateInstance(Audio::FDeviceId InDeviceId) const override
	{
		using namespace AudioModulation;

		auto NewGenerator = MakeShared<FEnvelopeFollowerGenerator, ESPMode::ThreadSafe>(Params, InDeviceId);
		return StaticCastSharedRef<AudioModulation::IGenerator>(NewGenerator);
	}
};
