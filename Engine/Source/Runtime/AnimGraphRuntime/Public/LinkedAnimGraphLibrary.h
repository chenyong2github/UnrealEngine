// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Animation/AnimNodeReference.h"
#include "LinkedAnimGraphLibrary.generated.h"

struct FAnimNode_LinkedAnimGraph;

USTRUCT(BlueprintType)
struct FLinkedAnimGraphReference : public FAnimNodeReference
{
	GENERATED_BODY()

	typedef FAnimNode_LinkedAnimGraph FInternalNodeType;
};

// Exposes operations to be performed on anim node contexts
UCLASS()
class ANIMGRAPHRUNTIME_API ULinkedAnimGraphLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Get a sequence player context from an anim node context */
	UFUNCTION(BlueprintCallable, Category = "Linked Anim Graph", meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "Result"))
	static FLinkedAnimGraphReference ConvertToLinkedAnimGraphContext(const FAnimNodeReference& Node, EAnimNodeReferenceConversionResult& Result);
	
	/** Returns whether the node hosts an instance (e.g. linked anim graph or layer) */
	UFUNCTION(BlueprintPure, Category = "Linked Anim Graph", meta=(BlueprintThreadSafe))
	static bool HasLinkedAnimInstance(const FLinkedAnimGraphReference& Node);

	/** Get the linked instance is hosted by this node. If the node does not host an instance then HasLinkedAnimInstance will return false */
	UFUNCTION(BlueprintPure, Category = "Linked Anim Graph", meta=(BlueprintThreadSafe))
	static UAnimInstance* GetLinkedAnimInstance(const FLinkedAnimGraphReference& Node);
};