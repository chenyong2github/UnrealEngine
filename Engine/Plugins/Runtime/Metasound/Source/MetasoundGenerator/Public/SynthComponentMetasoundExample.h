// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Components/SynthComponent.h"

#include "MetasoundOperatorInterface.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundAudioFormats.h"
#include "MetasoundGenerator.h"

#include "SynthComponentMetasoundExample.generated.h"


/** USynthComponentMetasoundExample is an class to show some of the interface 
 * for MetasoundGraphCore. It's not very useful except as a code example.
 */
UCLASS(ClassGroup = Synth, meta = (BlueprintSpawnableComponent))
class METASOUNDGENERATOR_API USynthComponentMetasoundExample : public USynthComponent
{
    GENERATED_BODY()

public:
	using FOperatorUniquePtr = TUniquePtr<Metasound::IOperator>;
	using FAudioBufferReadRef = Metasound::FAudioBufferReadRef;
	using FOperatorSettings = Metasound::FOperatorSettings;

    USynthComponentMetasoundExample(const FObjectInitializer& ObjInitializer);

    virtual ~USynthComponentMetasoundExample();

	/** Parameters which can be set on the input of the metasound graph during
	 * runtime. 
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Metasound)
	TMap<FString, float> FloatParameters;

	/** Set a float parameter on a metasound. 
	 *
	 * @param InName - Name of parameter to set.
	 * @param InValue - Value of parameter.
	 */
	UFUNCTION(BlueprintCallable, Category=Metasound)
	void SetFloatParameter(const FString& InName, float InValue);

	/** Sets the graph operator to use to generate audio. The given operator will be used
	 * the next time CreateSoundGenerator(...) is called.
	 *
	 * @param InGraphOperator - Operator which executes entire MetasoundGraph.
	 * @param InOperatorSettings - Settings associated with the graph operator.
	 * @param InOutputAudioName - Name of the FAudioBufferReadRef data ref in the graph output.
	 */
	void SetGraphOperator(FOperatorUniquePtr InGraphOperator, const FOperatorSettings& InOperatorSettings, const FString& InOutputAudioName);

	/** Attempt to push a new graph operator to the existing metasound generator.
	 *
	 * @param InGraphOperator - Operator which executes entire MetasoundGraph.
	 * @param InOperatorSettings - Settings associated with the graph operator.
	 * @param InOutputAudioName - Name of the FAudioBufferReadRef data ref in the graph output.
	 *
	 * @return True on success, false on failure.
	 */
	bool PushGraphOperator(FOperatorUniquePtr InGraphOperator, const FOperatorSettings& InOperatorSettings, const FString& InOutputAudioName);

    virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent & PropertyChangedEvent) override;
#endif

private:

	TSharedPtr<Metasound::FMetasoundGenerator, ESPMode::ThreadSafe> Generator;
};
