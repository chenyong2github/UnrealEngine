// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntitySubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassSignalSubsystem.generated.h"

namespace UE::MassSignal 
{
	DECLARE_MULTICAST_DELEGATE_TwoParams(FSignalDelegate, FName /*SignalName*/, TConstArrayView<FLWEntity> /*Entities*/);
} // UE::MassSignal

/**
* A subsystem for handling Signals in Mass
*/
UCLASS()
class MASSSIGNALS_API UMassSignalSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
	
public:

	/** 
	 * Retrieve the delegate dispatcher from the signal name
	 * @param SignalName is the name of the signal to get the delegate dispatcher from
	 */
	UE::MassSignal::FSignalDelegate& GetSignalDelegateByName(FName SignalName)
	{
		return NamedSignals.FindOrAdd(SignalName);
	}

	/**
	 * Inform a single entity of a signal being raised
	 * @param SignalName is the name of the signal raised
	 * @param Entity entity that should be informed that signal 'SignalName' was raised
	 */
	void SignalEntity(FName SignalName, const FLWEntity Entity);

	/**
	 * Inform multiple entities of a signal being raised
	 * @param SignalName is the name of the signal raised
	 * @param Entities list of entities that should be informed that signal 'SignalName' was raised
	 */
	void SignalEntities(FName SignalName, TConstArrayView<FLWEntity> Entities);

	/**
	 * Inform a single entity of a signal being raised in a certain amount of seconds
	 * @param SignalName is the name of the signal raised
	 * @param Entity entity that should be informed that signal 'SignalName' was raised
	 * @param DelayInSeconds is the amount of time before signaling the entity
	 */
	void DelaySignalEntity(FName SignalName, const FLWEntity Entity, const float DelayInSeconds);

 	/**
	 * Inform multiple entities of a signal being raised in a certain amount of seconds
	 * @param SignalName is the name of the signal raised
	 * @param Entities being informed of the raised signal
	 * @param DelayInSeconds is the amount of time before signaling the entities
	 */
	void DelaySignalEntities(FName SignalName, TConstArrayView<FLWEntity> Entities, const float DelayInSeconds);

	/**
	 * Inform single entity of a signal being raised asynchronously using the LWComponent Command Buffer
	 * @param Context is the Entity System execution context to push the async command
	 * @param SignalName is the name of the signal raised
	 * @param Entity entity that should be informed that signal 'SignalName' was raised
	 */
	void SignalEntityDeferred(FLWComponentSystemExecutionContext& Context, FName SignalName, const FLWEntity Entity);

	/**
	 * Inform multiple entities of a signal being raised asynchronously using the LWComponent Command Buffer
	 * @param Context is the Entity System execution context to push the async command
	 * @param SignalName is the name of the signal raised
	 * @param Entities list of entities that should be informed that signal 'SignalName' was raised
	 */
	void SignalEntitiesDeferred(FLWComponentSystemExecutionContext& Context, FName SignalName, TConstArrayView<FLWEntity> Entities);

 	/**
	 * Inform single entity of a signal being raised asynchronously using the LWComponent Command Buffer
	 * @param Context is the Entity System execution context to push the async command
	 * @param SignalName is the name of the signal raised
	 * @param Entity entity that should be informed that signal 'SignalName' was raised
	 * @param DelayInSeconds is the amount of time before signaling the entities
	 */
	void DelaySignalEntityDeferred(FLWComponentSystemExecutionContext& Context, FName SignalName, const FLWEntity Entity, const float DelayInSeconds);

 	/**
	 * Inform multiple entities of a signal being raised asynchronously using the LWComponent Command Buffer
	 * @param Context is the Entity System execution context to push the async command
	 * @param SignalName is the name of the signal raised
	 * @param Entities being informed of that signal was raised
	 * @param DelayInSeconds is the amount of time before signaling the entities
	 */
	void DelaySignalEntitiesDeferred(FLWComponentSystemExecutionContext& Context, FName SignalName, TConstArrayView<FLWEntity> Entities, const float DelayInSeconds);

protected:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	TMap<FName, UE::MassSignal::FSignalDelegate> NamedSignals;

	struct FDelayedSignal
	{
		FName SignalName;
		TArray<FLWEntity> Entities;
		float DelayInSeconds;
	};

	TArray<FDelayedSignal> DelayedSignals;
};
