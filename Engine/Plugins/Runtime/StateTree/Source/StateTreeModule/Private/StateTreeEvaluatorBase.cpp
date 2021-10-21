// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEvaluatorBase.h"
#include "CoreMinimal.h"

#if WITH_GAMEPLAY_DEBUGGER
void FStateTreeEvaluator2Base::AppendDebugInfoString(FString& DebugString, const FStateTreeExecutionContext& Context) const
{
	DebugString += FString::Printf(TEXT("[%s]\n"), *Name.ToString());
}
#endif // WITH_GAMEPLAY_DEBUGGER
