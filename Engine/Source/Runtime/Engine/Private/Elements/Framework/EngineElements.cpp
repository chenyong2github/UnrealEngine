// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/EngineElements.h"
#include "Elements/Framework/TypedElementRegistry.h"

#include "Elements/Object/ObjectElementData.h"
#include "Elements/Object/ObjectElementObjectInterface.h"
#include "Elements/Object/ObjectElementSelectionInterface.h"

#include "Elements/Actor/ActorElementData.h"
#include "Elements/Actor/ActorElementObjectInterface.h"
#include "Elements/Actor/ActorElementWorldInterface.h"
#include "Elements/Actor/ActorElementSelectionInterface.h"

#include "Elements/Component/ComponentElementData.h"
#include "Elements/Component/ComponentElementObjectInterface.h"
#include "Elements/Component/ComponentElementWorldInterface.h"
#include "Elements/Component/ComponentElementSelectionInterface.h"

#include "Modules/ModuleManager.h"

FSimpleMulticastDelegate OnRegisterEngineElementsDelegate;

void RegisterEngineObjectElements()
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	Registry->RegisterElementType<FObjectElementData>(NAME_Object);
	Registry->RegisterElementInterface<UTypedElementObjectInterface>(NAME_Object, NewObject<UObjectElementObjectInterface>());
	Registry->RegisterElementInterface<UTypedElementSelectionInterface>(NAME_Object, NewObject<UObjectElementSelectionInterface>());
}

void RegisterEngineActorElements()
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	Registry->RegisterElementType<FActorElementData>(NAME_Actor);
	Registry->RegisterElementInterface<UTypedElementObjectInterface>(NAME_Actor, NewObject<UActorElementObjectInterface>());
	Registry->RegisterElementInterface<UTypedElementWorldInterface>(NAME_Actor, NewObject<UActorElementWorldInterface>());
	Registry->RegisterElementInterface<UTypedElementSelectionInterface>(NAME_Actor, NewObject<UActorElementSelectionInterface>());
}

void RegisterEngineComponentElements()
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	Registry->RegisterElementType<FComponentElementData>(NAME_Components);
	Registry->RegisterElementInterface<UTypedElementObjectInterface>(NAME_Components, NewObject<UComponentElementObjectInterface>());
	Registry->RegisterElementInterface<UTypedElementWorldInterface>(NAME_Components, NewObject<UComponentElementWorldInterface>());
	Registry->RegisterElementInterface<UTypedElementSelectionInterface>(NAME_Components, NewObject<UComponentElementSelectionInterface>());
}

void RegisterEngineElements()
{
	// Ensure the framework and base interfaces are also loaded
	FModuleManager::Get().LoadModuleChecked("TypedElementFramework");
	FModuleManager::Get().LoadModuleChecked("TypedElementInterfaces");

	RegisterEngineObjectElements();
	RegisterEngineActorElements();
	RegisterEngineComponentElements();

	OnRegisterEngineElementsDelegate.Broadcast();
}
