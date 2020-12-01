// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/EditorElements.h"
#include "Elements/Framework/TypedElementRegistry.h"

#include "Elements/Object/ObjectElementEditorSelectionInterface.h"

#include "Elements/Actor/ActorElementEditorWorldInterface.h"
#include "Elements/Actor/ActorElementEditorSelectionInterface.h"

#include "Elements/Component/ComponentElementEditorWorldInterface.h"
#include "Elements/Component/ComponentElementEditorSelectionInterface.h"

FSimpleMulticastDelegate OnRegisterEditorElementsDelegate;

void RegisterEditorObjectElements()
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	Registry->RegisterElementInterface<UTypedElementSelectionInterface>(NAME_Object, NewObject<UObjectElementEditorSelectionInterface>(), /*bAllowOverride*/true);
}

void RegisterEditorActorElements()
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	Registry->RegisterElementInterface<UTypedElementWorldInterface>(NAME_Actor, NewObject<UActorElementEditorWorldInterface>(), /*bAllowOverride*/true);
	Registry->RegisterElementInterface<UTypedElementSelectionInterface>(NAME_Actor, NewObject<UActorElementEditorSelectionInterface>(), /*bAllowOverride*/true);
}

void RegisterEditorComponentElements()
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	Registry->RegisterElementInterface<UTypedElementWorldInterface>(NAME_Components, NewObject<UComponentElementEditorWorldInterface>(), /*bAllowOverride*/true);
	Registry->RegisterElementInterface<UTypedElementSelectionInterface>(NAME_Components, NewObject<UComponentElementEditorSelectionInterface>(), /*bAllowOverride*/true);
}

void RegisterEditorElements()
{
	RegisterEditorObjectElements();
	RegisterEditorActorElements();
	RegisterEditorComponentElements();

	OnRegisterEditorElementsDelegate.Broadcast();
}
