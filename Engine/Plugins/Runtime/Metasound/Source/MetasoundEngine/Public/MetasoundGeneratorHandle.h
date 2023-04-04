// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Templates/SharedPointer.h"
#include "MetasoundParameterPack.h"

#include "MetasoundGeneratorHandle.generated.h"

class UAudioComponent;
class UMetaSoundSource;
class UMetasoundParameterPack;
namespace Metasound
{
	class FMetasoundGenerator;
}

UCLASS(BlueprintType,Category="MetaSound")
class METASOUNDENGINE_API UMetasoundGeneratorHandle : public UObject
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category="MetaSound")
	static UMetasoundGeneratorHandle* CreateMetaSoundGeneratorHandle(UAudioComponent* OnComponent);

	void BeginDestroy() override;

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


	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AudioComponent;
	uint64 AudioComponentId;
	UPROPERTY(Transient)
	TObjectPtr<UMetaSoundSource> CachedMetasoundSource;
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
};