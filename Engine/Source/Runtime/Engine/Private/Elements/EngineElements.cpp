// Copyright Epic Games, Inc. All Rights Reserved.

#include "EngineElements.h"
#include "Modules/ModuleManager.h"

#include "TypedElementRegistry.h"

#include "Actor/ActorElementData.h"
#include "Actor/ActorElementSelectionInterface.h"

#include "Component/ComponentElementData.h"
#include "Component/ComponentElementSelectionInterface.h"

void RegisterActorElements()
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	Registry->RegisterElementType<FActorElementData>(NAME_Actor);
	Registry->RegisterElementInterface<UTypedElementSelectionInterface>(NAME_Actor, NewObject<UActorElementSelectionInterface>());
}

void RegisterComponentElements()
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	Registry->RegisterElementType<FComponentElementData>(NAME_Components);
	Registry->RegisterElementInterface<UTypedElementSelectionInterface>(NAME_Components, NewObject<UComponentElementSelectionInterface>());
}

void RegisterEngineElements()
{
	// Ensure the framework and base interfaces are also loaded
	FModuleManager::Get().LoadModuleChecked("TypedElementFramework");
	FModuleManager::Get().LoadModuleChecked("TypedElementInterfaces");

	RegisterActorElements();
	RegisterComponentElements();
}
