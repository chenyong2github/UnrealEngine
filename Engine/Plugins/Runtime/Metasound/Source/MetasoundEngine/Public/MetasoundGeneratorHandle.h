// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundOutputWatcher.h"
#include "MetasoundParameterPack.h"
#include "Containers/SpscQueue.h"
#include "Templates/SharedPointer.h"

#include "MetasoundGeneratorHandle.generated.h"

class UAudioComponent;
class UMetaSoundSource;
class UMetasoundParameterPack;
namespace Metasound
{
	class FMetasoundGenerator;
}

DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnMetasoundOutputValueChanged, FName, OutputName, const FMetaSoundOutput&, Output);

UCLASS(BlueprintType,Category="MetaSound")
class METASOUNDENGINE_API UMetasoundGeneratorHandle : public UObject
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category="MetaSound")
	static UMetasoundGeneratorHandle* CreateMetaSoundGeneratorHandle(UAudioComponent* OnComponent);

	virtual void BeginDestroy() override;

	bool IsValid() const;

	/**
	 * Get the id for the UAudioComponent associated with this handle.
	 * NOTE: Be sure to check IsValid() before expecting a valid return from this method.
	 *
	 * @returns The audio component's id, or INDEX_NONE if the component is no longer valid.
	 */
	uint64 GetAudioComponentId() const;

	// UMetasoundGeneratorHandle shields its "clients" from "cross thread" issues
	// related to callbacks coming in the audio control or rendering threads that 
	// game thread clients (e.g. blueprints) want to know about. That is why these 
	// next delegate definitions do not need to be the "TS" variants. Assignments 
	// to members of this type, and the broadcasts there to will all happen on the
	// game thread. EVEN IF the instigator of those callbacks is on the audio
	// render thread. 
	DECLARE_MULTICAST_DELEGATE(FOnAttached);
	DECLARE_MULTICAST_DELEGATE(FOnDetached);
	DECLARE_DELEGATE(FOnSetGraph);

	/**
	 * Makes a copy of the supplied parameter pack and passes it to the MetaSoundGenerator
	 * for asynchronous processing. IT ALSO caches this copy so that if the AudioComponent
	 * is virtualized the parameter pack will be sent again when/if the AudioComponent is 
	 * "unvirtualized".
	 */
	UFUNCTION(BlueprintCallable, Category="MetaSoundParameterPack")
	bool ApplyParameterPack(UMetasoundParameterPack* Pack);

	TSharedPtr<Metasound::FMetasoundGenerator> GetGenerator();

	UMetasoundGeneratorHandle::FOnAttached OnGeneratorHandleAttached;
	UMetasoundGeneratorHandle::FOnDetached OnGeneratorHandleDetached;
	// Note: We don't allow direct assignment to the OnGeneratorsGraphChanged delegate
	// because we need to know that someone actually wants this message so we can
	// start actively listening for the corresponding audio render thread callback...
	FDelegateHandle AddGraphSetCallback(const UMetasoundGeneratorHandle::FOnSetGraph& Delegate);
	bool RemoveGraphSetCallback(const FDelegateHandle& Handle);

	/**
	 * Watch an output value.
	 *
	 * @param OutputName - The user-specified name of the output in the Metasound
	 * @param OnOutputValueChanged - The event to fire when the output's value changes
	 * @param AnalyzerName - (optional) The name of the analyzer to use on the output, defaults to a passthrough
	 * @param AnalyzerOutputName - (optional) The name of the output on the analyzer to watch, defaults to the passthrough output
	 * @returns true if the watch setup succeeded, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category="MetaSoundOutput", meta=(AdvancedDisplay = "2"))
	bool WatchOutput(
		FName OutputName,
		const FOnMetasoundOutputValueChanged& OnOutputValueChanged,
		FName AnalyzerName = NAME_None,
		FName AnalyzerOutputName = NAME_None);

	/**
	 * Map a type name to a passthrough analyzer name to use as a default for UMetasoundOutputSubsystem::WatchOutput()
	 *
	 * @param TypeName - The type name returned from GetMetasoundDataTypeName()
	 * @param AnalyzerName - The name of the analyzer to use
	 */
	static void RegisterPassthroughAnalyzerForType(FName TypeName, FName AnalyzerName);

	/**
	 * Update any watched outputs
	 */
	void UpdateWatchers();
	
private:
	void SetAudioComponent(UAudioComponent* InAudioComponent);
	void CacheMetasoundSource();
	void ClearCachedData();

	/**
	 * Attempts to pin the weak generator pointer. If the first attempt fails it checks to see
	 * if it can "recapture" a pointer to a generator for the current AudioComponent/MetaSoundSource
	 * combination. 
	 */
	TSharedPtr<Metasound::FMetasoundGenerator> PinGenerator();

	/**
	 * Functions for adding and removing our MetaSoundGenerator lifecycle delegates
	 */
	void AttachGeneratorDelegates();
	void AttachGraphChangedDelegate();
	void DetachGeneratorDelegates();

	/**
	 * Generator creation and destruction delegates we register with the UMetaSoundSource
	 */
	void OnSourceCreatedAGenerator(uint64 InAudioComponentId, TSharedPtr<Metasound::FMetasoundGenerator> InGenerator);
	void OnSourceDestroyedAGenerator(uint64 InAudioComponentId, TSharedPtr<Metasound::FMetasoundGenerator> InGenerator);
	
	TWeakObjectPtr<UAudioComponent> AudioComponent;
	TWeakObjectPtr<UMetaSoundSource> CachedMetasoundSource;
	TWeakPtr<Metasound::FMetasoundGenerator> CachedGeneratorPtr;
	FSharedMetasoundParameterStoragePtr CachedParameterPack;

	// Note: We don't allow direct assignment to the OnGeneratorsGraphChanged delegate
	// because we need to know that someone actually wants this message so we can
	// start actively listening for the corresponding audio render thread callback. 
	// So these next members are private and a "client" that wants to be notified of
	// the graph change have to call public functions declared above to add themselves.
	DECLARE_MULTICAST_DELEGATE(FOnSetGraphMulticast);
	FOnSetGraphMulticast OnGeneratorsGraphChanged;

	FDelegateHandle GeneratorCreatedDelegateHandle;
	FDelegateHandle GeneratorDestroyedDelegateHandle;
	FDelegateHandle GeneratorGraphChangedDelegateHandle;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnOutputValueChangedMulticast, FName, Name, const FMetaSoundOutput&, Output);
	static TMap<FName, FName> PassthroughAnalyzers;
	// This is a map of output names on the generator to a map of output names on an analyzer
	// to delegates listening to that analyzer output. We do this because analyzers can have more
	// than one output.
	TMap<FName, TMap<FName, FOnOutputValueChangedMulticast>> OutputListenerMap;
	TSpscQueue<Metasound::Frontend::FAnalyzerAddress> OutputAnalyzersToAdd;
	void CreateAnalyzerAndWatcher(const TSharedPtr<Metasound::FMetasoundGenerator> Generator, Metasound::Frontend::FAnalyzerAddress&& AnalyzerAddress);
	TArray<Metasound::Private::FMetasoundOutputWatcher> OutputWatchers;
};