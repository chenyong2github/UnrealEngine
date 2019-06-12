// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SelectProximityCommand.h"
#include "IMeshEditorModeEditingContract.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "EditorSupportDelegates.h"
#include "SceneOutlinerDelegates.h"

#define LOCTEXT_NAMESPACE "SelectProximityCommand"

void USelectProximityCommand::RegisterUICommand( FBindingContext* BindingContext )
{
	UI_COMMAND_EXT( BindingContext, /* Out */ UICommandInfo, "SelectNeighbors", "Select Neighbors", "Additionally select the Neighbors of the selected node.", EUserInterfaceActionType::Button, FInputChord() );
}

void USelectProximityCommand::Execute(IMeshEditorModeEditingContract& MeshEditorMode)
{
	if (MeshEditorMode.GetActiveAction() != NAME_None)
	{
		return;
	}

	if (MeshEditorMode.GetSelectedEditableMeshes().Num() == 0)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("SelectNeighbors", "Select Neighbors"));

	MeshEditorMode.CommitSelectedMeshes();

	TArray<AActor*> SelectedActors = GetSelectedActors();
	SelectNeighbors(MeshEditorMode, SelectedActors);

	UpdateExplodedView(MeshEditorMode, EViewResetType::RESET_ALL);
}

void USelectProximityCommand::SelectNeighbors(IMeshEditorModeEditingContract& MeshEditorMode, TArray<AActor*>& SelectedActors)
{
	TArray<UEditableMesh*> SelectedMeshes = MeshEditorMode.GetSelectedEditableMeshes();

	for (AActor* SelectedActor : SelectedActors)
	{
		UEditableMesh* EditableMesh = GetEditableMeshForActor(SelectedActor, SelectedMeshes);
		if (EditableMesh)
		{
			UGeometryCollectionComponent* GeometryCollectionComponent = GetGeometryCollectionComponent(EditableMesh);
			if (GeometryCollectionComponent != nullptr)
			{
				FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
				EditBoneColor.SelectBones(GeometryCollection::ESelectionMode::Neighbors);
			}
		}
	}

}

#undef LOCTEXT_NAMESPACE
