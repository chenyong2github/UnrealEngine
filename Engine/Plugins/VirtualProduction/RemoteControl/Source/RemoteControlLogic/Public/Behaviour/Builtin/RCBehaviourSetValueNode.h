// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Behaviour/RCBehaviourNode.h"
#include "RCBehaviourSetValueNode.generated.h"

/**
 * Simple pass behaviour which is returns execute true all the time.
 *
 * That is needed for simple the execute the actions without any logic
 */
UCLASS()
class REMOTECONTROLLOGIC_API URCBehaviourSetValueNode : public URCBehaviourNode
{
	GENERATED_BODY()

public:

	URCBehaviourSetValueNode();

	//~ Begin URCBehaviourNode interface
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Remote Control Behaviour")
	bool Execute(URCBehaviour* InBehaviour) const override;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Remote Control Behaviour")
	bool IsSupported(URCBehaviour* InBehaviour) const override;
	//~ End URCBehaviourNode interface
};
