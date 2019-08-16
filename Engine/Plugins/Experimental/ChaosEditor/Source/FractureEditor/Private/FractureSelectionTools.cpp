// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FractureSelectionTools.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "EditorSupportDelegates.h"

void FFractureSelectionTools::ToggleSelectedBones(UGeometryCollectionComponent* GeometryCollectionComponent, TArray<int32>& BoneIndices, bool bClearCurrentSelection)
{
	if (GeometryCollectionComponent)
	{
		FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();

		if (const UGeometryCollection* MeshGeometryCollection = GeometryCollectionComponent->RestCollection)
		{
			TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = MeshGeometryCollection->GetGeometryCollection();
			if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
			{
				EditBoneColor.SetEnableBoneSelection(true);

				// if multiselect then append new BoneSelected to what is already selected, otherwise we just clear and replace the old selection with BoneSelected
				if (bClearCurrentSelection)
				{
					EditBoneColor.ResetBoneSelection();
				}
		
				EditBoneColor.ToggleSelectedBones(BoneIndices);

				// The actual selection made is based on the hierarchy and the view mode
				if (GeometryCollection)
				{
					const TArray<int32>& Selected = EditBoneColor.GetSelectedBones();
					TArray<int32> RevisedSelected;
					TArray<int32> Highlighted;
					FGeometryCollectionClusteringUtility::ContextBasedClusterSelection(GeometryCollection, EditBoneColor.GetViewLevel(), Selected, RevisedSelected, Highlighted);
					EditBoneColor.SetSelectedBones(RevisedSelected);
					EditBoneColor.SetHighlightedBones(Highlighted);

					// @todo FractureEd:  Replace with delegate for custom fracture tree view
					//SceneOutliner::FSceneOutlinerDelegates::Get().OnComponentSelectionChanged.Broadcast(GeometryCollectionComponent);
				}
			}

			FEditorSupportDelegates::RedrawAllViewports.Broadcast();

		}
	}

}

void FFractureSelectionTools::ClearSelectedBones(UGeometryCollectionComponent* GeometryCollectionComponent)
{
	FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
	EditBoneColor.ResetBoneSelection();
}


void FFractureSelectionTools::SelectNeighbors(UGeometryCollectionComponent* GeometryCollectionComponent)
{
	if (GeometryCollectionComponent)
	{
		FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
		EditBoneColor.SelectBones(GeometryCollection::ESelectionMode::Neighbors);
	}
}
