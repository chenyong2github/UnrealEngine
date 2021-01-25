// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/EditorElements.h"
#include "Elements/Framework/TypedElementRegistry.h"

#include "Elements/Object/ObjectElementDetailsInterface.h"
#include "Elements/Object/ObjectElementEditorSelectionInterface.h"

#include "Elements/Actor/ActorElementDetailsInterface.h"
#include "Elements/Actor/ActorElementEditorWorldInterface.h"
#include "Elements/Actor/ActorElementEditorSelectionInterface.h"
#include "Elements/Actor/ActorElementEditorAssetDataInterface.h"

#include "Elements/Component/ComponentElementDetailsInterface.h"
#include "Elements/Component/ComponentElementEditorWorldInterface.h"
#include "Elements/Component/ComponentElementEditorSelectionInterface.h"

FSimpleMulticastDelegate OnRegisterEditorElementsDelegate;

void RegisterEditorObjectElements()
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	Registry->RegisterElementInterface<UTypedElementDetailsInterface>(NAME_Object, NewObject<UObjectElementDetailsInterface>());
	Registry->RegisterElementInterface<UTypedElementSelectionInterface>(NAME_Object, NewObject<UObjectElementEditorSelectionInterface>(), /*bAllowOverride*/true);
}

void RegisterEditorActorElements()
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	Registry->RegisterElementInterface<UTypedElementDetailsInterface>(NAME_Actor, NewObject<UActorElementDetailsInterface>());
	Registry->RegisterElementInterface<UTypedElementWorldInterface>(NAME_Actor, NewObject<UActorElementEditorWorldInterface>(), /*bAllowOverride*/true);
	Registry->RegisterElementInterface<UTypedElementSelectionInterface>(NAME_Actor, NewObject<UActorElementEditorSelectionInterface>(), /*bAllowOverride*/true);
	Registry->RegisterElementInterface<UTypedElementAssetDataInterface>(NAME_Actor, NewObject<UActorElementEditorAssetDataInterface>(), /*bAllowOverride*/true);
}

void RegisterEditorComponentElements()
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	Registry->RegisterElementInterface<UTypedElementDetailsInterface>(NAME_Components, NewObject<UComponentElementDetailsInterface>());
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
