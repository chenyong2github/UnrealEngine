// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_GAMEPLAY_DEBUGGER

#include "GameplayDebuggerCategory.h"

class APlayerController;
class AActor;

class FGameplayDebuggerCategory_SmartObject : public FGameplayDebuggerCategory
{
public:
	FGameplayDebuggerCategory_SmartObject();

	static TSharedRef<FGameplayDebuggerCategory> MakeInstance();

protected:
	virtual void CollectData(APlayerController* OwnerPC, AActor* DebugActor) override;
};

#endif // WITH_GAMEPLAY_DEBUGGER
