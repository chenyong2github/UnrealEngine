// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Behaviour/RCBehaviourNode.h"
#include "RCBehaviourOnEmptyNode.generated.h"

/**
 * Check is the controlled property is empty
 */
UCLASS()
class REMOTECONTROLLOGIC_API URCBehaviourOnEmptyNode : public URCBehaviourNode
{
	GENERATED_BODY()

public:

	URCBehaviourOnEmptyNode();

	//~ Begin URCBehaviourNode interface
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Remote Control Behaviour")
	bool Execute(URCBehaviour* InBehaviour) const override;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Remote Control Behaviour")
	bool IsSupported(URCBehaviour* InBehaviour) const override;
	//~ End URCBehaviourNode interface
};
