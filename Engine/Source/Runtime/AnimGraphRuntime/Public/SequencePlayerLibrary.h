// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Animation/AnimNodeContext.h"
#include "SequencePlayerLibrary.generated.h"

// Exposes operations to be performed on a sequence player anim node
// Note: Experimental and subject to change!
UCLASS(Experimental)
class ANIMGRAPHRUNTIME_API USequencePlayerLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	/** Set the current accumulated time of the sequence player */
	UFUNCTION(BlueprintCallable, Category = "Sequence Player", meta=(BlueprintThreadSafe))
	static void SetAccumulatedTime(const FAnimNodeContext& NodeContext, float Time);

	/** Set the start position of the sequence player */
	UFUNCTION(BlueprintCallable, Category = "Sequence Player", meta=(BlueprintThreadSafe))
	static void SetStartPosition(const FAnimNodeContext& NodeContext, float StartPosition);

	UFUNCTION(BlueprintCallable, Category = "Sequence Player", meta=(BlueprintThreadSafe))
	static void SetPlayRate(const FAnimNodeContext& NodeContext, float PlayRate);
};