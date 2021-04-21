// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorSubsystem.h"

#include "ToolTargetManager.h"
#include "ToolTargets/StaticMeshUVMeshToolTarget.h"
#include "TargetInterfaces/UVUnwrapDynamicMesh.h"
#include "UVEditor.h"

const FToolTargetTypeRequirements UUVEditorSubsystem::UVUnwrapMeshTargetRequirements = 
	FToolTargetTypeRequirements(UUVUnwrapDynamicMesh::StaticClass());

void UUVEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// The subsystem has its own tool target manager because it must exist before any UV editors exist,
	// to see if a UV editor can be started.
	ToolTargetManager = NewObject<UToolTargetManager>(this);
	ToolTargetManager->Initialize();

	// Add new target factories here and in UUVEditorMode::Enter() as they are developed.
	ToolTargetManager->AddTargetFactory(NewObject<UStaticMeshUVMeshToolTargetFactory>(ToolTargetManager));
}

void UUVEditorSubsystem::Deinitialize()
{
	ToolTargetManager->Shutdown();
	ToolTargetManager = nullptr;
}

bool UUVEditorSubsystem::AreObjectsValidTargets(const TArray<UObject*>& InObjects) const
{
	if (InObjects.IsEmpty())
	{
		return false;
	}

	for (UObject* Object : InObjects)
	{
		if (!ToolTargetManager->CanBuildTarget(Object, UVUnwrapMeshTargetRequirements))
		{
			return false;
		}
	}

	return true;
}

void UUVEditorSubsystem::StartUVEditor(TArray<TObjectPtr<UObject>> ObjectsToEdit)
{
	// We don't allow opening a new instance if any of the objects are already opened
	// in an existing instance. Instead, we bring such an instance to the front.
	// Note that the asset editor subsystem takes care of this for "primary" asset editors, 
	// i.e., the editors that open when one double clicks an asset or selects "edit". Since
	// the UV editor is not a "primary" asset editor for any asset type, we do this management 
	// ourselves.
	for (TObjectPtr<UObject>& Object : ObjectsToEdit)
	{
		if (OpenedEditorInstances.Contains(Object))
		{
			OpenedEditorInstances[Object]->GetInstanceInterface()->FocusWindow(Object);
			return;
		}
	}

	// If we got here, there's not an instance already opened.

	UUVEditor* UVEditor = NewObject<UUVEditor>(this);

	// Among other things, this call registers the UV editor with the asset editor subsystem,
	// which will prevent it from being garbage collected.
	UVEditor->Initialize(ObjectsToEdit);

	for (TObjectPtr<UObject>& Object : ObjectsToEdit)
	{
		OpenedEditorInstances.Add(Object, UVEditor);
	}
}

void UUVEditorSubsystem::NotifyThatUVEditorClosed(TArray<TObjectPtr<UObject>> ObjectsItWasEditing)
{
	for (TObjectPtr<UObject>& Object : ObjectsItWasEditing)
	{
		OpenedEditorInstances.Remove(Object);
	}
}

