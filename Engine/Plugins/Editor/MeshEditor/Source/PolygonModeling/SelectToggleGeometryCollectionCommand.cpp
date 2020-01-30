// Copyright Epic Games, Inc. All Rights Reserved.

#include "SelectToggleGeometryCollectionCommand.h"

#include "Editor.h"
#include "EditorSupportDelegates.h"
#include "Engine/Selection.h"
#include "Framework/Commands/UIAction.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "IMeshEditorModeEditingContract.h"
#include "IMeshEditorModeUIContract.h"
#include "MeshFractureSettings.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "SelectedToggleGeometryCollectionCommand"

DEFINE_LOG_CATEGORY(LogSelectToggleGeometryCommand);

FUIAction USelectToggleGeometryCollectionCommand::MakeUIAction(class IMeshEditorModeUIContract& MeshEditorMode)
{
	FUIAction UIAction;
	{
		FExecuteAction ExecuteAction(FExecuteAction::CreateLambda([&MeshEditorMode, this]
		{
			this->Execute(MeshEditorMode);
		}));

		UIAction = FUIAction(
			ExecuteAction,
			FCanExecuteAction::CreateLambda([&MeshEditorMode] { return (MeshEditorMode.GetSelectedEditableMeshes().Num() > 0); }));
	}
	return UIAction;

}

void USelectToggleGeometryCollectionCommand::RegisterUICommand( FBindingContext* BindingContext )
{
	UI_COMMAND_EXT( BindingContext, /* Out */ UICommandInfo, "SelectToggleMeshChunks", "Select Toggle", "Select Toggle Mesh Chunks.", EUserInterfaceActionType::Button, FInputChord() );
}

void USelectToggleGeometryCollectionCommand::Execute(IMeshEditorModeEditingContract& MeshEditorMode)
{
	if (MeshEditorMode.GetActiveAction() != NAME_None)
	{
		return;
	}

	if (MeshEditorMode.GetSelectedEditableMeshes().Num() == 0)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("SelectAllMeshChunks", "Select Toggle Mesh Chunks"));

	MeshEditorMode.CommitSelectedMeshes();

	TArray<UEditableMesh*> SelectedMeshes = MeshEditorMode.GetSelectedEditableMeshes();

	for (UEditableMesh* Mesh : SelectedMeshes)
	{
		UGeometryCollectionComponent* GeometryCollectionComponent = GetGeometryCollectionComponent(Mesh);
		if (GeometryCollectionComponent)
		{
			FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
			EditBoneColor.SelectBones(GeometryCollection::ESelectionMode::InverseGeometry);
		}
	}
}



#undef LOCTEXT_NAMESPACE
