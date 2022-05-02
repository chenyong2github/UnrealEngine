// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayStateTreeModule.h"
#include "Modules/ModuleManager.h"


//----------------------------------------------------------------------//
// IGameplayStateTreeModule
//----------------------------------------------------------------------//
IGameplayStateTreeModule& IGameplayStateTreeModule::Get()
{
	return FModuleManager::LoadModuleChecked<IGameplayStateTreeModule>("StateTreeGameplayModule");
}

bool IGameplayStateTreeModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("StateTreeGameplayModule");
}


//----------------------------------------------------------------------//
// FStateTreeGameplayModule
//----------------------------------------------------------------------//
class FStateTreeGameplayModule : public IGameplayStateTreeModule
{
};

IMPLEMENT_MODULE(FStateTreeGameplayModule, StateTreeGameplayModule)
