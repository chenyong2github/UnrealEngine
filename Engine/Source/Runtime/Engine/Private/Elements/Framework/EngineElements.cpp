// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/EngineElements.h"
#include "Elements/Framework/TypedElementRegistry.h"

#include "Elements/Object/ObjectElementData.h"
#include "Elements/Object/ObjectElementAssetDataInterface.h"
#include "Elements/Object/ObjectElementObjectInterface.h"
#include "Elements/Object/ObjectElementCounterInterface.h"
#include "Elements/Object/ObjectElementSelectionInterface.h"

#include "Elements/Actor/ActorElementData.h"
#include "Elements/Actor/ActorElementAssetDataInterface.h"
#include "Elements/Actor/ActorElementObjectInterface.h"
#include "Elements/Actor/ActorElementCounterInterface.h"
#include "Elements/Actor/ActorElementWorldInterface.h"
#include "Elements/Actor/ActorElementSelectionInterface.h"

#include "Elements/Component/ComponentElementData.h"
#include "Elements/Component/ComponentElementObjectInterface.h"
#include "Elements/Component/ComponentElementCounterInterface.h"
#include "Elements/Component/ComponentElementWorldInterface.h"
#include "Elements/Component/ComponentElementSelectionInterface.h"

#include "Elements/SMInstance/SMInstanceElementData.h"
#include "Elements/SMInstance/SMInstanceElementWorldInterface.h"
#include "Elements/SMInstance/SMInstanceElementSelectionInterface.h"
#include "Elements/SMInstance/SMInstanceElementAssetDataInterface.h"

#include "Modules/ModuleManager.h"

FSimpleMulticastDelegate OnRegisterEngineElementsDelegate;

void RegisterEngineObjectElements()
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	Registry->RegisterElementType<FObjectElementData>(NAME_Object);
	Registry->RegisterElementInterface<UTypedElementAssetDataInterface>(NAME_Object, NewObject<UObjectElementAssetDataInterface>());
	Registry->RegisterElementInterface<UTypedElementObjectInterface>(NAME_Object, NewObject<UObjectElementObjectInterface>());
	Registry->RegisterElementInterface<UTypedElementCounterInterface>(NAME_Object, NewObject<UObjectElementCounterInterface>());
	Registry->RegisterElementInterface<UTypedElementSelectionInterface>(NAME_Object, NewObject<UObjectElementSelectionInterface>());
}

void RegisterEngineActorElements()
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	Registry->RegisterElementType<FActorElementData>(NAME_Actor);
	Registry->RegisterElementInterface<UTypedElementAssetDataInterface>(NAME_Actor, NewObject<UActorElementAssetDataInterface>());
	Registry->RegisterElementInterface<UTypedElementObjectInterface>(NAME_Actor, NewObject<UActorElementObjectInterface>());
	Registry->RegisterElementInterface<UTypedElementCounterInterface>(NAME_Actor, NewObject<UActorElementCounterInterface>());
	Registry->RegisterElementInterface<UTypedElementWorldInterface>(NAME_Actor, NewObject<UActorElementWorldInterface>());
	Registry->RegisterElementInterface<UTypedElementSelectionInterface>(NAME_Actor, NewObject<UActorElementSelectionInterface>());
}

void RegisterEngineComponentElements()
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	Registry->RegisterElementType<FComponentElementData>(NAME_Components);
	Registry->RegisterElementInterface<UTypedElementObjectInterface>(NAME_Components, NewObject<UComponentElementObjectInterface>());
	Registry->RegisterElementInterface<UTypedElementCounterInterface>(NAME_Components, NewObject<UComponentElementCounterInterface>());
	Registry->RegisterElementInterface<UTypedElementWorldInterface>(NAME_Components, NewObject<UComponentElementWorldInterface>());
	Registry->RegisterElementInterface<UTypedElementSelectionInterface>(NAME_Components, NewObject<UComponentElementSelectionInterface>());
}

void RegisterEngineSMInstanceElements()
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	Registry->RegisterElementType<FSMInstanceElementData>(NAME_SMInstance);
	Registry->RegisterElementInterface<UTypedElementWorldInterface>(NAME_SMInstance, NewObject<USMInstanceElementWorldInterface>());
	Registry->RegisterElementInterface<UTypedElementSelectionInterface>(NAME_SMInstance, NewObject<USMInstanceElementSelectionInterface>());
	Registry->RegisterElementInterface<UTypedElementAssetDataInterface>(NAME_SMInstance, NewObject<USMInstanceElementAssetDataInterface>());
}

void RegisterEngineElements()
{
	// Ensure the framework and base interfaces are also loaded
	FModuleManager::Get().LoadModuleChecked("TypedElementFramework");
	FModuleManager::Get().LoadModuleChecked("TypedElementRuntime");

	RegisterEngineObjectElements();
	RegisterEngineActorElements();
	RegisterEngineComponentElements();
	RegisterEngineSMInstanceElements();

	OnRegisterEngineElementsDelegate.Broadcast();
}
