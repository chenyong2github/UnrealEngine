// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Animation/AnimNodeContext.h"
#include "AnimNodeContextLibrary.generated.h"

// Exposes operations to be performed on anim node contexts
UCLASS()
class ANIMGRAPHRUNTIME_API UAnimNodeContextLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

#if WITH_EDITOR
	/** Prototype function for thread-safe anim node calls */
	UFUNCTION(BlueprintInternalUseOnly, meta=(BlueprintThreadSafe))
	void Prototype_ThreadSafeAnimNodeCall(const FAnimNodeContext& NodeContext) {}
#endif
	
	/** Get the anim instance that hosts this anim node */
	UFUNCTION(BlueprintPure, Category = "Anim Node", meta=(BlueprintThreadSafe))
	static UAnimInstance* GetAnimInstance(const FAnimNodeContext& NodeContext);

	/** Returns whether the node hosts an instance (e.g. linked anim graph or layer) */
	UFUNCTION(BlueprintPure, Category = "Anim Node", meta=(BlueprintThreadSafe))
	static bool HasLinkedAnimInstance(const FAnimNodeContext& NodeContext);

	/** Get the linked instance is hosted by this node. If the node does not host an instance then HasLinkedAnimInstance will return false */
	UFUNCTION(BlueprintPure, Category = "Anim Node", meta=(BlueprintThreadSafe))
	static UAnimInstance* GetLinkedAnimInstance(const FAnimNodeContext& NodeContext);
};