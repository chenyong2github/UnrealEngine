// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "UnHideAllGeometryCollectionCommand.h"

#include "Editor.h"
#include "EditorSupportDelegates.h"
#include "Engine/Selection.h"
#include "FractureToolDelegates.h"
#include "Framework/Commands/UIAction.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "IMeshEditorModeEditingContract.h"
#include "IMeshEditorModeUIContract.h"
#include "MeshFractureSettings.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "UnHideAllGeometryCollectionCommand "

DEFINE_LOG_CATEGORY(LogUnHideAllGeometryCommand);

FUIAction UUnHideAllGeometryCollectionCommand::MakeUIAction(class IMeshEditorModeUIContract& MeshEditorMode)
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

void UUnHideAllGeometryCollectionCommand::RegisterUICommand( FBindingContext* BindingContext )
{
	UI_COMMAND_EXT( BindingContext, /* Out */ UICommandInfo, "UnHideAllMeshClusters", "UnHide All", "UnHide All Mesh Clusters.", EUserInterfaceActionType::Button, FInputChord() );
}

void UUnHideAllGeometryCollectionCommand::Execute(IMeshEditorModeEditingContract& MeshEditorMode)
{
	if (MeshEditorMode.GetActiveAction() != NAME_None)
	{
		return;
	}

	if (MeshEditorMode.GetSelectedEditableMeshes().Num() == 0)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("RemoveSelectedMeshChunks", "Remove Selected Mesh Chunks"));

	MeshEditorMode.CommitSelectedMeshes();

	TArray<UEditableMesh*> SelectedMeshes = MeshEditorMode.GetSelectedEditableMeshes();

	for (UEditableMesh* Mesh : SelectedMeshes)
	{
		UGeometryCollectionComponent* GeometryCollectionComponent = GetGeometryCollectionComponent(Mesh);
		if (GeometryCollectionComponent)
		{
			FGeometryCollectionEdit GeometryCollectionEdit = GeometryCollectionComponent->EditRestCollection();
			if (UGeometryCollection* GeometryCollectionObject = GeometryCollectionEdit.GetRestCollection())
			{
				TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
				if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
				{
					FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
					EditBoneColor.SelectBones(GeometryCollection::ESelectionMode::AllGeometry);

					TArray<int32> SelectedBones(EditBoneColor.GetSelectedBones());

					GeometryCollection->UpdateGeometryVisibility(SelectedBones, true);

					EditBoneColor.ResetBoneSelection();
					EditBoneColor.ResetHighlightedBones();
				}
			}
		}
	}
	FFractureToolDelegates::Get().OnFractureExpansionEnd.Broadcast();
}

#undef LOCTEXT_NAMESPACE
