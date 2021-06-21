// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Animation/AnimNodeReference.h"
#include "Animation/AnimExecutionContext.h"
#include "AnimExecutionContextLibrary.generated.h"

class UAnimInstance;

// Exposes operations to be performed on anim node contexts
UCLASS()
class ANIMGRAPHRUNTIME_API UAnimExecutionContextLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
#if WITH_EDITOR
	/** Prototype function for thread-safe anim node calls */
	UFUNCTION(BlueprintInternalUseOnly, meta=(BlueprintThreadSafe))
	void Prototype_ThreadSafeAnimNodeCall(const FAnimExecutionContext& Context, const FAnimNodeReference& Node) {}
#endif
	
	/** Get the anim instance that hosts this anim node */
	UFUNCTION(BlueprintPure, Category = "Anim Node", meta=(BlueprintThreadSafe))
	static UAnimInstance* GetAnimInstance(const FAnimExecutionContext& Context);

	/** Internal compiler use only - Get a reference to an anim node by index */
	UFUNCTION(BlueprintPure, BlueprintInternalUseOnly, meta=(BlueprintThreadSafe))
	static FAnimNodeReference GetAnimNodeReference(UAnimInstance* Instance, int32 Index);
};