// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "DSP/Dsp.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundDataReferenceTypes.h"
#include "MetasoundTime.h"
#include "MetasoundFrequency.h"
#include "Sound/SoundGenerator.h"

#include "MetasoundGenerator.generated.h"

namespace Metasound
{
	class METASOUNDGENERATOR_API FMetasoundGenerator : public ISoundGenerator
	{
	public:
		using FOperatorUniquePtr = TUniquePtr<Metasound::IOperator>;
		using FAudioReadRef = Metasound::FAudioBufferReadRef;

		FMetasoundGenerator(FOperatorUniquePtr InOperator, const FAudioReadRef& InOperatorReadBuffer);
		virtual ~FMetasoundGenerator();

		void SetFrequency(float InFrequency);
		void SetBopPeriod(float InPeriodInSeconds);

		//~ Begin FSoundGenerator
		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;
		//~ End FSoundGenerator

	private:
		int32 FillWithBuffer(const Audio::AlignedFloatBuffer& InBuffer, float* OutAudio, int32 MaxNumOutputSamples); 

		using FExecuteFunction = Metasound::IOperator::FExecuteFunction;

		Audio::AlignedFloatBuffer OverflowBuffer;
		FOperatorUniquePtr Operator;
		FAudioReadRef OperatorReadBuffer;

		FFrequencyWriteRef FrequencyRef;
		FFloatTimeWriteRef BopPeriodRef;

		int32 OperatorReadNum;
		FExecuteFunction ExecuteOperator;
	};
}

UCLASS(ClassGroup = Synth, meta = (BlueprintSpawnableComponent))
class METASOUNDGENERATOR_API USynthComponentMetasoundGenerator : public USynthComponent
{
    GENERATED_BODY()

public:

    USynthComponentMetasoundGenerator(const FObjectInitializer& ObjInitializer);

    virtual ~USynthComponentMetasoundGenerator();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Metasound, meta = (ClampMin="0.0001", ClampMax="10.0"))
	float BopPeriod;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Metasound, meta = (ClampMin="20", ClampMax="800"))
	float Frequency;

	UFUNCTION(BlueprintCallable, Category=Metasound)
	void SetBopPeriod(float InBopPeriod);

	UFUNCTION(BlueprintCallable, Category = Metasound)
	void SetFrequency(float InFrequency);

    virtual ISoundGeneratorPtr CreateSoundGenerator(int32 InSampleRate, int32 InNumChannels) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent & PropertyChangedEvent) override;
#endif

private:
	ISoundGeneratorPtr Generator;
};




