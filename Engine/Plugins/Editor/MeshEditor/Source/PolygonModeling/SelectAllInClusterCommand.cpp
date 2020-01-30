// Copyright Epic Games, Inc. All Rights Reserved.

#include "SelectAllInClusterCommand.h"
#include "IMeshEditorModeEditingContract.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "EditorSupportDelegates.h"
#include "SceneOutlinerDelegates.h"


#define LOCTEXT_NAMESPACE "SelectAllInClusterCommand"


void USelectAllInClusterCommand::RegisterUICommand( FBindingContext* BindingContext )
{
	UI_COMMAND_EXT( BindingContext, /* Out */ UICommandInfo, "SelectAllInCluster", "Select All In Cluster", "Select all nodes in cluster.", EUserInterfaceActionType::Button, FInputChord() );
}

void USelectAllInClusterCommand::Execute(IMeshEditorModeEditingContract& MeshEditorMode)
{
	if (MeshEditorMode.GetActiveAction() != NAME_None)
	{
		return;
	}

	if (MeshEditorMode.GetSelectedEditableMeshes().Num() == 0)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("SelectAllInCluster", "Select All In Cluster"));

	MeshEditorMode.CommitSelectedMeshes();

	TArray<AActor*> SelectedActors = GetSelectedActors();
	SelectAllInCluster(MeshEditorMode, SelectedActors);

	UpdateExplodedView(MeshEditorMode, EViewResetType::RESET_ALL);
}


void USelectAllInClusterCommand::SelectAllInCluster(IMeshEditorModeEditingContract& MeshEditorMode, TArray<AActor*>& SelectedActors)
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
				EditBoneColor.SelectBones(GeometryCollection::ESelectionMode::AllInCluster);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
