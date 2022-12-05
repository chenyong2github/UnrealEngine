// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeTypes.h"
#include "StateTreeEvents.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeNodeBlueprintBase.generated.h"

struct FStateTreeLinker;
struct FStateTreeExecutionContext;

UENUM()
enum class EStateTreeBlueprintPropertyCategory : uint8
{
	NotSet,
	Input,	
	Parameter,
	Output,
	ContextObject,
};


/** Struct use to copy external data to the Blueprint item instance, resolved during StateTree linking. */
struct STATETREEMODULE_API FStateTreeBlueprintExternalDataHandle
{
	const FProperty* Property = nullptr;
	FStateTreeExternalDataHandle Handle;
};


UCLASS(Abstract)
class STATETREEMODULE_API UStateTreeNodeBlueprintBase : public UObject
{
	GENERATED_BODY()

public:
	/** Sends event to the StateTree */
	UFUNCTION(BlueprintCallable, Category = "StateTree", meta = (HideSelfPin = "true", DisplayName = "StateTree Send Event"))
	void SendEvent(const FStateTreeEvent& Event);
	
protected:
	virtual UWorld* GetWorld() const override;
	AActor* GetOwnerActor(const FStateTreeExecutionContext& Context) const;

	/** These methods are const as they set mutable variables and need to be called from a const method. */
	void SetCachedEventQueueFromContext(const FStateTreeExecutionContext& Context) const;
	void ClearCachedEventQueue() const;
	
private:
	/** Cached instance data while the node is active. */
	mutable FStateTreeEventQueue* CachedEventQueue = nullptr;

	/** Cached owner while the node is active. */
	UPROPERTY()
	mutable TObjectPtr<UObject> CachedOwner = nullptr; 
};
