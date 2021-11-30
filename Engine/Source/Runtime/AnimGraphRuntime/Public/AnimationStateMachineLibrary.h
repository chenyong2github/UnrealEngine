// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Animation/AnimExecutionContext.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Animation/AnimNodeReference.h"
#include "AnimationStateMachineLibrary.generated.h"

struct FAnimNode_AnimationStateMachine;
struct FAnimNode_StateResult;

USTRUCT(BlueprintType, DisplayName = "Animation State Reference")
struct FAnimationStateResultReference : public FAnimNodeReference
{
	GENERATED_BODY()

	typedef FAnimNode_StateResult FInternalNodeType;
};

// Exposes operations to be performed on anim state machine node contexts
UCLASS(Experimental)
class ANIMGRAPHRUNTIME_API UAnimationStateMachineLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Get an anim state reference from an anim node reference */
	UFUNCTION(BlueprintCallable, Category = "State Machine", meta=(BlueprintThreadSafe, DisplayName = "Convert to Animation State", ExpandEnumAsExecs = "Result"))
	static void ConvertToAnimationStateResult(const FAnimNodeReference& Node, FAnimationStateResultReference& AnimationState, EAnimNodeReferenceConversionResult& Result);

	/** Get an anim state reference from an anim node reference (pure) */
	UFUNCTION(BlueprintPure, Category = "State Machine", meta=(BlueprintThreadSafe, DisplayName = "Convert to Animation State"))
	static void ConvertToAnimationStateResultPure(const FAnimNodeReference& Node, FAnimationStateResultReference& AnimationState, bool& Result)
	{
		EAnimNodeReferenceConversionResult ConversionResult;
		 ConvertToAnimationStateResult(Node, AnimationState, ConversionResult);
		Result = (ConversionResult == EAnimNodeReferenceConversionResult::Succeeded);	
	}
	
	/** Returns whether the state the node belongs to is blending in */
	UFUNCTION(BlueprintPure, Category = "State Machine", meta=(BlueprintThreadSafe))
	static bool IsStateBlendingIn(const FAnimUpdateContext& UpdateContext, const FAnimationStateResultReference& Node);

	/** Returns whether the state the node belongs to is blending out */
	UFUNCTION(BlueprintPure, Category = "State Machine", meta=(BlueprintThreadSafe))
	static bool IsStateBlendingOut(const FAnimUpdateContext& UpdateContext, const FAnimationStateResultReference& Node);
};