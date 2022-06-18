// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "RCBehaviour.generated.h"

class URCBehaviourNode;
class URCController;
class URemoteControlPreset;
class URCActionContainer;
class URCVirtualProperty;

/**
 * Base class for remote control behaviour.
 *
 * Behaviour is container for:
 * - Set of behaviour conditions
 * - And associated actions which should be executed if that is passed the behaviour
 *
 * Behaviour can be extended in Blueprints or CPP classes
 */
UCLASS(BlueprintType)
class REMOTECONTROLLOGIC_API URCBehaviour : public UObject
{
	GENERATED_BODY()

public:
	URCBehaviour();

	/** Initialize behaviour functionality */
	virtual void Initialize() {}

	/** Execute the behaviour */
	virtual void Execute();

	/** Get number of action associated with behaviour */
	int32 GetNumActions() const;

	/**
	 * Return blueprint class associated with behaviour if exists
	 */
	UClass* GetOverrideBehaviourBlueprintClass() const;

#if WITH_EDITORONLY_DATA
	/**
	 * Return blueprint instance associated with behaviour if exists
	 */
	UBlueprint* GetBlueprint() const;
#endif

	/**
	 * Set blueprint class for this behaviour
	 */
	void SetOverrideBehaviourBlueprintClass(UBlueprint* InBlueprint);

#if WITH_EDITOR
	/** Get Display Name for this Behaviour */
	const FText& GetDisplayName();

	/** Get Description for this Behaviour */
	const FText& GetBehaviorDescription();
#endif

private:
	/**
	 * It created the node if it called first time
	 * If BehaviourNodeClass changes it creates new instance
	 * Or just return cached one
	 */
	URCBehaviourNode* GetBehaviourNode();

public:
	/** Associated cpp behaviour */
	UPROPERTY()
	TSubclassOf<URCBehaviourNode> BehaviourNodeClass;

	/** Class path to associated blueprint class behaviour */
	UPROPERTY()
	FSoftClassPath OverrideBehaviourBlueprintClassPath;

	/** Behaviour Id */
	UPROPERTY()
	FGuid Id;

	/** Action container which is associated with current behaviour */
	UPROPERTY()
	TObjectPtr<URCActionContainer> ActionContainer;

	/** Reference to controller virtual property with this behaviour */
	UPROPERTY()
	TWeakObjectPtr<URCController> ControllerWeakPtr;

private:
	/** Cached behaviour node class */
	TSubclassOf<UObject> CachedBehaviourNodeClass;

	/** Cached behaviour node */
	UPROPERTY(Instanced)
	TObjectPtr<URCBehaviourNode> CachedBehaviourNode;

public:
	/** Whether this Behaviour is currently enabled. 
	* If disabled, it will be not evaluated when the associated Controller changes */
	UPROPERTY()
	bool bIsEnabled = true;
};
