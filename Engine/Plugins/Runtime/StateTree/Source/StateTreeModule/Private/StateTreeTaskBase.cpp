// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeTaskBase.h"
#include "CoreMinimal.h"


#if WITH_GAMEPLAY_DEBUGGER
void FStateTreeTask2Base::AppendDebugInfoString(FString& DebugString, const FStateTreeExecutionContext& Context) const
{
	DebugString += FString::Printf(TEXT("[%s]\n"), *Name.ToString());
}
#endif
