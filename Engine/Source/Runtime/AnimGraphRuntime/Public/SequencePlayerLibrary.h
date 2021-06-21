// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Animation/AnimNodeReference.h"
#include "SequencePlayerLibrary.generated.h"

struct FAnimNode_SequencePlayer;

USTRUCT(BlueprintType)
struct FSequencePlayerReference : public FAnimNodeReference
{
	GENERATED_BODY()

	typedef FAnimNode_SequencePlayer FInternalNodeType;
};

// Exposes operations to be performed on a sequence player anim node
// Note: Experimental and subject to change!
UCLASS(Experimental)
class ANIMGRAPHRUNTIME_API USequencePlayerLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Get a sequence player context from an anim node context */
	UFUNCTION(BlueprintCallable, Category = "Sequence Player", meta=(BlueprintThreadSafe, ExpandEnumAsExecs = "Result"))
	static FSequencePlayerReference ConvertToSequencePlayerContext(const FAnimNodeReference& Node, EAnimNodeReferenceConversionResult& Result);
	
	/** Set the current accumulated time of the sequence player */
	UFUNCTION(BlueprintCallable, Category = "Sequence Player", meta=(BlueprintThreadSafe))
	static FSequencePlayerReference SetAccumulatedTime(const FSequencePlayerReference& SequencePlayer, float Time);

	/** Set the start position of the sequence player */
	UFUNCTION(BlueprintCallable, Category = "Sequence Player", meta=(BlueprintThreadSafe))
	static FSequencePlayerReference SetStartPosition(const FSequencePlayerReference& SequencePlayer, float StartPosition);

	UFUNCTION(BlueprintCallable, Category = "Sequence Player", meta=(BlueprintThreadSafe))
	static FSequencePlayerReference SetPlayRate(const FSequencePlayerReference& SequencePlayer, float PlayRate);
};