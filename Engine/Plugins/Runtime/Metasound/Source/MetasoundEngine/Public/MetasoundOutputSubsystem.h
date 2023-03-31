// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundOutput.h"
#include "MetasoundOutputWatcher.h"
#include "Containers/Queue.h"
#include "Containers/Map.h"
#include "Subsystems/WorldSubsystem.h"

#include "MetasoundOutputSubsystem.generated.h"

namespace Metasound
{
	namespace Frontend
	{
		class FAnalyzerAddress;
	}

	class FMetasoundGenerator;
}

class UAudioComponent;
class UMetaSoundSource;

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnMetasoundOutputValueChanged, const FMetaSoundOutput&, Output);

/**
 * Provides access to a playing Metasound generator's outputs
 */
UCLASS()
class METASOUNDENGINE_API UMetaSoundOutputSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual ~UMetaSoundOutputSubsystem() override = default;
	
	/**
	 * Watch an output on a Metasound playing on a given audio component.
	 *
	 * @param AudioComponent - The audio component
	 * @param OutputName - The user-specified name of the output in the Metasound
	 * @param OnOutputValueChanged - The event to fire when the output's value changes
	 * @param AnalyzerName - (optional) The name of the analyzer to use on the output, defaults to a passthrough
	 * @param AnalyzerOutputName - (optional) The name of the output on the analyzer to watch, defaults to the passthrough output
	 * @returns true if the watch setup succeeded, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category="MetaSoundOutput", meta=(AdvancedDisplay = "3"))
	bool WatchOutput(
		UAudioComponent* AudioComponent,
		FName OutputName,
		const FOnMetasoundOutputValueChanged& OnOutputValueChanged,
		FName AnalyzerName = NAME_None,
		FName AnalyzerOutputName = NAME_None);
	
	/** Begin UTickableWorldSubsystem */
	virtual bool IsTickable() const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	/** End UTickableWorldSubsystem */

	/**
	 * Map a type name to a passthrough analyzer name to use as a default for UMetasoundOutputSubsystem::WatchOutput()
	 *
	 * @param TypeName - The type name returned from GetMetasoundDataTypeName()
	 * @param AnalyzerName - The name of the analyzer to use
	 */
	static void RegisterPassthroughAnalyzerForType(FName TypeName, FName AnalyzerName);

private:
	static TMap<FName, FName> PassthroughAnalyzers;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnOutputValueChangedMulticast, const FMetaSoundOutput&, Output);
	
	struct FGeneratorInfo
	{
		TWeakObjectPtr<UAudioComponent> AudioComponent;
		TWeakObjectPtr<UMetaSoundSource> Source;
		TWeakPtr<Metasound::FMetasoundGenerator, ESPMode::ThreadSafe> Generator;
		FDelegateHandle OnCreatedHandle;
		FDelegateHandle OnDestroyedHandle;
		TQueue<Metasound::Frontend::FAnalyzerAddress> AnalyzersToCreate;
		TMap<FName, TMap<FName, FOnOutputValueChangedMulticast>> OutputChangedDelegates;
		TArray<Metasound::Private::FMetasoundOutputWatcher> OutputWatchers;
		void HandleOutputChanged(FName OutputName, const FMetaSoundOutput& Output);
		bool IsValid() const;
	};

	FGeneratorInfo* FindOrAddGeneratorInfo(UAudioComponent* AudioComponent);
	void OnGeneratorCreated(uint64 InAudioComponentId, TSharedPtr<Metasound::FMetasoundGenerator> Generator);
	void OnGeneratorDestroyed(uint64 InAudioComponentId, TSharedPtr<Metasound::FMetasoundGenerator> Generator);
	void CreateAnalyzerAndWatcher(
		const TSharedPtr<Metasound::FMetasoundGenerator>& Generator,
		Metasound::Frontend::FAnalyzerAddress&& AnalyzerAddress);

	TArray<FGeneratorInfo> TrackedGenerators;
};
