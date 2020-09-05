// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorElements.h"
#include "TypedElementRegistry.h"

#include "Elements/Actor/ActorElementEditorSelectionInterface.h"

#include "Elements/Component/ComponentElementEditorSelectionInterface.h"

void RegisterEditorActorElements()
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	Registry->RegisterElementInterface<UTypedElementSelectionInterface>(NAME_Actor, NewObject<UActorElementEditorSelectionInterface>(), /*bAllowOverride*/true);
}

void RegisterEditorComponentElements()
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	Registry->RegisterElementInterface<UTypedElementSelectionInterface>(NAME_Components, NewObject<UComponentElementEditorSelectionInterface>(), /*bAllowOverride*/true);
}

void RegisterEditorElements()
{
	RegisterEditorActorElements();
	RegisterEditorComponentElements();
}
