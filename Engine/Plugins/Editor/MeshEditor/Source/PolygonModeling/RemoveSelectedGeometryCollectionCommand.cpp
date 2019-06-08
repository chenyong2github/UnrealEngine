// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "RemoveSelectedGeometryCollectionCommand.h"

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

#define LOCTEXT_NAMESPACE "RemoveSelectedGeometryCollectionCommand "

DEFINE_LOG_CATEGORY(LogRemoveSelectedGeometryCommand);

FUIAction URemoveSelectedGeometryCollectionCommand::MakeUIAction(class IMeshEditorModeUIContract& MeshEditorMode)
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

void URemoveSelectedGeometryCollectionCommand::RegisterUICommand( FBindingContext* BindingContext )
{
	UI_COMMAND_EXT( BindingContext, /* Out */ UICommandInfo, "RemoveSelectedMeshClusters", "Remove Selected", "Remove Selected Mesh Clusters.", EUserInterfaceActionType::Button, FInputChord() );
}

void URemoveSelectedGeometryCollectionCommand::Execute(IMeshEditorModeEditingContract& MeshEditorMode)
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
					TArray<int32> SelectedBones(EditBoneColor.GetSelectedBones());
					SelectedBones.Sort();

					GeometryCollection->RemoveElements(FTransformCollection::TransformGroup, SelectedBones);

					EditBoneColor.ResetBoneSelection();
					EditBoneColor.ResetHighlightedBones();

					// rebuilds material sections
					GeometryCollectionObject->ReindexMaterialSections();
				}
			}
		}
	}
	FFractureToolDelegates::Get().OnFractureExpansionEnd.Broadcast();
}

#undef LOCTEXT_NAMESPACE
