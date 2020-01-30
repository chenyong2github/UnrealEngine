// Copyright Epic Games, Inc. All Rights Reserved.

#include "SelectSiblingsCommand.h"
#include "IMeshEditorModeEditingContract.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "EditorSupportDelegates.h"
#include "SceneOutlinerDelegates.h"

#define LOCTEXT_NAMESPACE "SelectSiblingsCommand"

void USelectSiblingsCommand::RegisterUICommand( FBindingContext* BindingContext )
{
	UI_COMMAND_EXT( BindingContext, /* Out */ UICommandInfo, "SelectSiblings", "Select Siblings", "Additionally select the siblings of the selected node.", EUserInterfaceActionType::Button, FInputChord() );
}

void USelectSiblingsCommand::Execute(IMeshEditorModeEditingContract& MeshEditorMode)
{
	if (MeshEditorMode.GetActiveAction() != NAME_None)
	{
		return;
	}

	if (MeshEditorMode.GetSelectedEditableMeshes().Num() == 0)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("SelectSiblings", "Select Siblings"));

	MeshEditorMode.CommitSelectedMeshes();

	TArray<AActor*> SelectedActors = GetSelectedActors();
	SelectSiblings(MeshEditorMode, SelectedActors);

	UpdateExplodedView(MeshEditorMode, EViewResetType::RESET_ALL);
}

void USelectSiblingsCommand::SelectSiblings(IMeshEditorModeEditingContract& MeshEditorMode, TArray<AActor*>& SelectedActors)
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
				EditBoneColor.SelectBones(GeometryCollection::ESelectionMode::Siblings);
			}
		}
	}

}

#undef LOCTEXT_NAMESPACE
