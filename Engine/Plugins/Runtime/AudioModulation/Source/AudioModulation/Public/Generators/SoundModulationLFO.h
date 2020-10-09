// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DSP/LFO.h"
#include "IAudioModulation.h"
#include "SoundModulationGenerator.h"
#include "Templates/SharedPointer.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "SoundModulationLFO.generated.h"


UENUM(BlueprintType)
enum class FSoundModulationLFOShape : uint8
{
	Sine			  UMETA(DisplayName = "Sine"),
	UpSaw			  UMETA(DisplayName = "Saw (Up)"),
	DownSaw			  UMETA(DisplayName = "Saw (Down)"),
	Square			  UMETA(DisplayName = "Square"),
	Triangle		  UMETA(DisplayName = "Triangle"),
	Exponential		  UMETA(DisplayName = "Exponential"),
	RandomSampleHold  UMETA(DisplayName = "Random"),

	COUNT UMETA(Hidden)
};

USTRUCT(BlueprintType)
struct FSoundModulationLFOParams
{
	GENERATED_USTRUCT_BODY()

	/** Shape of oscillating waveform */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayPriority = 20, EditCondition = "!bBypass"))
	FSoundModulationLFOShape Shape;

	/** Amplitude of oscillator */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayPriority = 30, EditCondition = "!bBypass", UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Amplitude = 0.5f;

	/** Frequency of oscillator */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayPriority = 40, EditCondition = "!bBypass", UIMin = "0", UIMax = "20", ClampMin = "0", ClampMax = "20"))
	float Frequency = 1.0f;

	/** Offset of oscillator */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayPriority = 50, EditCondition = "!bBypass", UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Offset = 0.5;

	/** Whether or not to loop the oscillation more than once */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayPriority = 60, EditCondition = "!bBypass"))
	bool bLooping = 1;

	/** If true, bypasses LFO bus from being modulated by parameters, patches, or mixed (LFO remains active and computed). */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayPriority = 10))
	bool bBypass = 0;
};


namespace AudioModulation
{
	class FLFOGenerator : public IGenerator
	{
		public:
			FLFOGenerator(const FSoundModulationLFOParams& InParams)
				: Params(InParams)
			{
				LFO.SetGain(Params.Amplitude);
				LFO.SetFrequency(Params.Frequency);
				LFO.SetMode(Params.bLooping ? Audio::ELFOMode::Type::Sync : Audio::ELFOMode::OneShot);

				static_assert(static_cast<int32>(FSoundModulationLFOShape::COUNT) == static_cast<int32>(Audio::ELFO::Type::NumLFOTypes), "LFOShape/ELFO Type mismatch");
				LFO.SetType(static_cast<Audio::ELFO::Type>(Params.Shape));
				LFO.Start();
			}

			virtual ~FLFOGenerator() = default;

			virtual float GetValue() const override
			{
				return Value;
			}

			virtual bool IsBypassed() const override
			{
				return Params.bBypass;
			}

			virtual void Update(double InElapsed) override
			{
				if (InElapsed > 0.0f && LFO.GetFrequency() > 0.0f)
				{
					const float SampleRate = static_cast<float>(1.0 / InElapsed);
					LFO.SetSampleRate(SampleRate);
					LFO.Update();
					Value = LFO.Generate() + Params.Offset;
				}
			}

#if !UE_BUILD_SHIPPING
		static const FString DebugName;

		virtual void GetDebugCategories(TArray<FString>& OutDebugCategories) const override;
		virtual void GetDebugValues(TArray<FString>& OutDebugValues) const override;
		virtual const FString& GetDebugName() const override;
#endif // !UE_BUILD_SHIPPING

	protected:
		Audio::FLFO LFO;
		float Value = 1.0f;
		FSoundModulationLFOParams Params;
	};
} // namespace AudioModulation


UCLASS(BlueprintType, hidecategories = Object, editinlinenew, MinimalAPI)
class USoundModulationGeneratorLFO : public USoundModulationGenerator
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (ShowOnlyInnerProperties))
	FSoundModulationLFOParams Params;

#if !UE_BUILD_SHIPPING
	static const TArray<FString>& GetDebugCategories()
	{
		static const TArray<FString> Categories =
		{
			TEXT("Value"),
			TEXT("Gain"),
			TEXT("Frequency"),
			TEXT("Offset"),
			TEXT("Curve")
		};
		return Categories;
	}

	static const FString& GetDebugName();
#endif // !UE_BUILD_SHIPPING


	virtual AudioModulation::FGeneratorPtr CreateInstance(Audio::FDeviceId InDeviceId) const override
	{
		using namespace AudioModulation;
		auto NewGenerator = MakeShared<FLFOGenerator, ESPMode::ThreadSafe>(Params);
		return StaticCastSharedRef<IGenerator>(NewGenerator);
	}
};
