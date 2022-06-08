// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RCVirtualProperty.h"
#include "RCController.generated.h"

class URCBehaviour;
class URCBehaviourNode;

/**
 * Remote Control Controller. Container for Behaviours and Actions
 */
UCLASS(BlueprintType)
class REMOTECONTROLLOGIC_API URCController : public URCVirtualPropertyInContainer
{
	GENERATED_BODY()

public:
	/** Create and add behaviour to behaviour set */
	virtual URCBehaviour* AddBehaviour(TSubclassOf<URCBehaviourNode> InBehaviourNodeClass);

	/** Create new behaviour */
	virtual URCBehaviour* CreateBehaviour(TSubclassOf<URCBehaviourNode> InBehaviourNodeClass);

	/** Remove the behaviour by behaviour UObject pointer */
	virtual int32 RemoveBehaviour(URCBehaviour* InBehaviour);

	/** Remove the behaviour by behaviour id */
	virtual int32 RemoveBehaviour(const FGuid InBehaviourId);

	/** Removes all behaviours. */
	virtual void EmptyBehaviours();

	/** Execute all behaviours for this controller. */
	virtual void ExecuteBehaviours();

	/** Handles modifications to controller value; evaluates all behaviours */
	virtual void OnModifyPropertyValue() override
	{
		ExecuteBehaviours();
	}

public:
	/** Set of the behaviours */
	UPROPERTY()
	TSet<TObjectPtr<URCBehaviour>> Behaviours;
};
