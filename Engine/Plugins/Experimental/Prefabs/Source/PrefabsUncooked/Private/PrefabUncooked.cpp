// Copyright Epic Games, Inc. All Rights Reserved.

#include "PrefabUncooked.h"
#include "PrefabCompilationManager.h"

void UPrefabUncooked::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FPrefabCompilationManager::NotifyPrefabEdited(this);
}

void UPrefabUncooked::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	FPrefabCompilationManager::NotifyPrefabEdited(this);
}
