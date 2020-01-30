// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoveUpOneLevelCommand.h"
#include "IMeshEditorModeEditingContract.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "StaticMeshAttributes.h"
#include "PackageTools.h"
#include "MeshFractureSettings.h"
#include "EditableMeshFactory.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "EditorSupportDelegates.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"


#define LOCTEXT_NAMESPACE "MoveUpOneLevelCommand"

void UMoveUpOneLevelCommand::RegisterUICommand( FBindingContext* BindingContext )
{
	UI_COMMAND_EXT( BindingContext, /* Out */ UICommandInfo, "MoveUpOneLevel", "Move Up One Level", "Move selected nodes up one level.", EUserInterfaceActionType::Button, FInputChord() );
}

void UMoveUpOneLevelCommand::Execute(IMeshEditorModeEditingContract& MeshEditorMode)
{
	if (MeshEditorMode.GetActiveAction() != NAME_None)
	{
		return;
	}

	if (MeshEditorMode.GetSelectedEditableMeshes().Num() == 0)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("MoveUpOneLevel", "Move Up One Level"));

	MeshEditorMode.CommitSelectedMeshes();

	TArray<AActor*> SelectedActors = GetSelectedActors();
	MoveUpOneLevel(MeshEditorMode, SelectedActors);

	UpdateExplodedView(MeshEditorMode, EViewResetType::RESET_ALL);
}

void UMoveUpOneLevelCommand::MoveUpOneLevel(IMeshEditorModeEditingContract& MeshEditorMode, TArray<AActor*>& SelectedActors)
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
				FGeometryCollectionEdit GeometryCollectionEdit = GeometryCollectionComponent->EditRestCollection();
				if (UGeometryCollection* GeometryCollectionObject = GeometryCollectionEdit.GetRestCollection())
				{
					TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
					if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
					{
						TArray<int32> Selected = GeometryCollectionComponent->GetSelectedBones();
						FGeometryCollectionClusteringUtility::MoveUpOneHierarchyLevel(GeometryCollection, Selected);
					}
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
