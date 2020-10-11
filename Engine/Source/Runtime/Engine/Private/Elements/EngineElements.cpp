// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/EngineElements.h"
#include "Modules/ModuleManager.h"

#include "TypedElementRegistry.h"

#include "Elements/Actor/ActorElementData.h"
#include "Elements/Actor/ActorElementSelectionInterface.h"

#include "Elements/Component/ComponentElementData.h"
#include "Elements/Component/ComponentElementSelectionInterface.h"

FSimpleMulticastDelegate OnRegisterEngineElementsDelegate;

void RegisterEngineActorElements()
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	Registry->RegisterElementType<FActorElementData>(NAME_Actor);
	Registry->RegisterElementInterface<UTypedElementSelectionInterface>(NAME_Actor, NewObject<UActorElementSelectionInterface>());
}

void RegisterEngineComponentElements()
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

	RegisterEngineActorElements();
	RegisterEngineComponentElements();

	OnRegisterEngineElementsDelegate.Broadcast();
}
