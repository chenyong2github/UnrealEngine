// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExampleCharacterFXEditorSubsystem.h"
#include "ExampleCharacterFXEditor.h"
#include "ExampleCharacterFXEditorMode.h"
#include "ToolTargets/DynamicMeshComponentToolTarget.h"
#include "ToolTargets/StaticMeshToolTarget.h"
#include "ToolTargets/SkeletalMeshToolTarget.h"
#include "ToolTargetManager.h"

using namespace UE::Geometry;

void UExampleCharacterFXEditorSubsystem::CreateToolTargetFactories(TArray<TObjectPtr<UToolTargetFactory>>& Factories) const
{
	Factories.Add(NewObject<UStaticMeshToolTargetFactory>(ToolTargetManager));
	Factories.Add(NewObject<USkeletalMeshToolTargetFactory>(ToolTargetManager));
	Factories.Add(NewObject<UDynamicMeshComponentToolTargetFactory>(ToolTargetManager));
}

void UExampleCharacterFXEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// The subsystem has its own tool target manager because it must exist before any ExampleCharacterFXEditors exist,
	// to see if the editor can be started.
	ToolTargetManager = NewObject<UToolTargetManager>(this);
	ToolTargetManager->Initialize();

	TArray<TObjectPtr<UToolTargetFactory>> ToolTargetFactories;
	CreateToolTargetFactories(ToolTargetFactories);

	for (TObjectPtr<UToolTargetFactory> Factory : ToolTargetFactories)
	{
		ToolTargetManager->AddTargetFactory(Factory);
	}
}

void UExampleCharacterFXEditorSubsystem::Deinitialize()
{
	ToolTargetManager->Shutdown();
	ToolTargetManager = nullptr;
}

void UExampleCharacterFXEditorSubsystem::BuildTargets(const TArray<TObjectPtr<UObject>>& ObjectsIn,
	const FToolTargetTypeRequirements& TargetRequirements,
	TArray<TObjectPtr<UToolTarget>>& TargetsOut)
{
	TargetsOut.Reset();

	for (UObject* Object : ObjectsIn)
	{
		UToolTarget* Target = ToolTargetManager->BuildTarget(Object, TargetRequirements);
		if (Target)
		{
			TargetsOut.Add(Target);
		}
	}
}

bool UExampleCharacterFXEditorSubsystem::AreObjectsValidTargets(const TArray<UObject*>& InObjects) const
{
	if (InObjects.IsEmpty())
	{
		return false;
	}

	for (UObject* Object : InObjects)
	{
		if (!ToolTargetManager->CanBuildTarget(Object, UExampleCharacterFXEditorMode::GetToolTargetRequirements()))
		{
			return false;
		}
	}

	return true;
}

void UExampleCharacterFXEditorSubsystem::StartExampleCharacterFXEditor(TArray<TObjectPtr<UObject>> ObjectsToEdit)
{
	// We don't allow opening a new instance if any of the objects are already opened
	// in an existing instance. Instead, we bring such an instance to the front.
	// Note that the asset editor subsystem takes care of this for "primary" asset editors, 
	// i.e., the editors that open when one double clicks an asset or selects "edit". Since
	// the editor is not a "primary" asset editor for any asset type, we do this management 
	// ourselves.
	// NOTE: If your asset class is associated with your editor, the asset editor subsystem can handle this
	for (TObjectPtr<UObject>& Object : ObjectsToEdit)
	{
		if (OpenedEditorInstances.Contains(Object))
		{
			OpenedEditorInstances[Object]->GetInstanceInterface()->FocusWindow(Object);
			return;
		}
	}

	// If we got here, there's not an instance already opened.
	UExampleCharacterFXEditor* CharacterFXEditor = NewObject<UExampleCharacterFXEditor>();

	// Among other things, this call registers the editor with the asset editor subsystem,
	// which will prevent it from being garbage collected.
	CharacterFXEditor->Initialize(ObjectsToEdit);

	for (TObjectPtr<UObject>& Object : ObjectsToEdit)
	{
		OpenedEditorInstances.Add(Object, CharacterFXEditor);
	}
}

void UExampleCharacterFXEditorSubsystem::NotifyThatExampleCharacterFXEditorClosed(TArray<TObjectPtr<UObject>> ObjectsItWasEditing)
{
	for (TObjectPtr<UObject>& Object : ObjectsItWasEditing)
	{
		OpenedEditorInstances.Remove(Object);
	}
}
