// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolClustering.h"

#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"


#define LOCTEXT_NAMESPACE "FractureToolClusteringOps"

FText UFractureToolFlattenAll::GetDisplayText() const
{
	return FText(NSLOCTEXT("FractureToolClusteringOps", "FractureToolFlattenAll", "Flatten"));
}

FText UFractureToolFlattenAll::GetTooltipText() const
{
	return FText(NSLOCTEXT("FractureToolClusteringOps", "FractureToolFlattenAllTooltip", "Flattens all bones to level 1"));
}

FSlateIcon UFractureToolFlattenAll::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.Flatten");
}

void UFractureToolFlattenAll::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "Flatten", "Flatten", "Flattens all bones to level 1.", EUserInterfaceActionType::Button, FInputChord());
	BindingContext->Flatten = UICommandInfo;
}

void UFractureToolFlattenAll::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (InToolkit.IsValid())
	{
		TSet<UGeometryCollectionComponent*> GeomCompSelection;
		GetSelectedGeometryCollectionComponents(GeomCompSelection);
		for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
		{
			FGeometryCollectionEdit GCEdit = GeometryCollectionComponent->EditRestCollection();
			if (UGeometryCollection* GCObject = GCEdit.GetRestCollection())
			{
				TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GCObject->GetGeometryCollection();
				if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
				{
					AddAdditionalAttributesIfRequired(GCObject);

					AddSingleRootNodeIfRequired(GCObject);

					int32 NumElements = GCObject->NumElements(FGeometryCollection::TransformGroup);
					TArray<int32> Elements;
					Elements.Reserve(NumElements);

					for (int32 Element = 0; Element < NumElements; ++Element)
					{
						if (GeometryCollection->Parent[Element] != FGeometryCollection::Invalid)
						{
							Elements.Add(Element);
						}
					}

					if (Elements.Num() > 0)
					{
						FGeometryCollectionClusteringUtility::ClusterBonesUnderExistingRoot(GeometryCollection, Elements);
					}

					FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(GeometryCollection, -1);

					FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
					EditBoneColor.ResetBoneSelection();

					InToolkit.Pin()->OnSetLevelViewValue(1);

					GeometryCollectionComponent->MarkRenderDynamicDataDirty();
					GeometryCollectionComponent->MarkRenderStateDirty();
				}
			}
		}

		InToolkit.Pin()->SetOutlinerComponents(GeomCompSelection.Array());
	}	
}



FText UFractureToolCluster::GetDisplayText() const
{
	return FText(NSLOCTEXT("FractureToolClusteringOps", "FractureToolCluster", "Cluster"));
}

FText UFractureToolCluster::GetTooltipText() const
{
	return FText(NSLOCTEXT("FractureToolClusteringOps", "FractureToolClusterTooltip", "Clusters selected bones under a new parent."));
}

FSlateIcon UFractureToolCluster::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.Cluster");
}

void UFractureToolCluster::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "Cluster", "Cluster", "Clusters selected bones under a new parent.", EUserInterfaceActionType::Button, FInputChord());
	BindingContext->Cluster = UICommandInfo;
}

void UFractureToolCluster::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (InToolkit.IsValid())
	{
		TSet<UGeometryCollectionComponent*> GeomCompSelection;
		GetSelectedGeometryCollectionComponents(GeomCompSelection);
		for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
		{
			const TArray<int32> SelectedBones = GeometryCollectionComponent->GetSelectedBones();

			if (SelectedBones.Num() > 1)
			{
				FGeometryCollectionEdit GCEdit = GeometryCollectionComponent->EditRestCollection();
				if (UGeometryCollection* GCObject = GCEdit.GetRestCollection())
				{
					TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GCObject->GetGeometryCollection();
					if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
					{
						const TManagedArray<TSet<int32>>& Children = GeometryCollection->GetAttribute<TSet<int32>>("Children", FGeometryCollection::TransformGroup);

						// sort the selection list so ClusterBonesUnderNewNode() happens in the correct order for leaf nodes
						TArray<int32> SortedSelectedBones;
						SortedSelectedBones.Reserve(SelectedBones.Num());
						for (int32 SelectedBone : SelectedBones)
						{
							if (Children[SelectedBone].Num() > 0)
							{
								SortedSelectedBones.Insert(SelectedBone, 0);
							}
							else
							{
								SortedSelectedBones.Add(SelectedBone);
							}
						}
						// cluster Selected Bones under the first selected bone
						int32 InsertAtIndex = SortedSelectedBones[0];

						FGeometryCollectionClusteringUtility::ClusterBonesUnderNewNode(GeometryCollection, InsertAtIndex, SortedSelectedBones, false);
						FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(GeometryCollection, -1);

						FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
						EditBoneColor.ResetBoneSelection();
						EditBoneColor.ResetHighlightedBones();
						GeometryCollectionComponent->MarkRenderDynamicDataDirty();
						GeometryCollectionComponent->MarkRenderStateDirty();
						InToolkit.Pin()->SetBoneSelection(GeometryCollectionComponent, EditBoneColor.GetSelectedBones(), true);
					}
				}
			}
		}

		InToolkit.Pin()->SetOutlinerComponents(GeomCompSelection.Array());
	}
}



FText UFractureToolUncluster::GetDisplayText() const
{
	return FText(NSLOCTEXT("FractureToolClusteringOps", "FractureToolUncluster", "Uncluster"));
}

FText UFractureToolUncluster::GetTooltipText() const
{
	return FText(NSLOCTEXT("FractureToolClusteringOps", "FractureToolUnclusterTooltip", "Remove parent cluster and move bones up a level."));
}

FSlateIcon UFractureToolUncluster::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.Uncluster");
}

void UFractureToolUncluster::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "Uncluster", "Uncluster", "Remove parent cluster and move bones up a level.", EUserInterfaceActionType::Button, FInputChord());
	BindingContext->Uncluster = UICommandInfo;
}

void UFractureToolUncluster::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (InToolkit.IsValid())
	{
		int32 FractureLevel = InToolkit.Pin()->GetLevelViewValue();
		TSet<UGeometryCollectionComponent*> GeomCompSelection;
		GetSelectedGeometryCollectionComponents(GeomCompSelection);
		for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
		{
			// scoped edit of collection
			FGeometryCollectionEdit GeometryCollectionEdit = GeometryCollectionComponent->EditRestCollection();
			if (UGeometryCollection* GeometryCollectionObject = GeometryCollectionEdit.GetRestCollection())
			{
				TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
				if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
				{
					FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(GeometryCollection, -1);
					FGeometryCollectionClusteringUtility::CollapseSelectedHierarchy(FractureLevel, GeometryCollectionComponent->GetSelectedBones(), GeometryCollection);

					FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
					EditBoneColor.ResetBoneSelection();
					EditBoneColor.ResetHighlightedBones();
					GeometryCollectionComponent->MarkRenderDynamicDataDirty();
					GeometryCollectionComponent->MarkRenderStateDirty();
					InToolkit.Pin()->SetBoneSelection(GeometryCollectionComponent, EditBoneColor.GetSelectedBones(), true);
				}
			}
		}
		InToolkit.Pin()->SetOutlinerComponents(GeomCompSelection.Array());
	}
}




FText UFractureToolMoveUp::GetDisplayText() const
{
	return FText(NSLOCTEXT("FractureToolClusteringOps", "FractureToolMoveUp", "Level Up"));
}

FText UFractureToolMoveUp::GetTooltipText() const
{
	return FText(NSLOCTEXT("FractureToolClusteringOps", "FractureToolMoveUpTooltip", "Move bones up a level."));
}

FSlateIcon UFractureToolMoveUp::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.MoveUp");
}

void UFractureToolMoveUp::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "MoveUp", "Level Up", "Move bones up a level.", EUserInterfaceActionType::Button, FInputChord());
	BindingContext->MoveUp = UICommandInfo;
}

void UFractureToolMoveUp::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (InToolkit.IsValid())
	{
		TSet<UGeometryCollectionComponent*> GeomCompSelection;
		GetSelectedGeometryCollectionComponents(GeomCompSelection);
		for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
		{
			FGeometryCollectionEdit GeometryCollectionEdit = GeometryCollectionComponent->EditRestCollection();
			if (UGeometryCollection* GeometryCollectionObject = GeometryCollectionEdit.GetRestCollection())
			{
				TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
				if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
				{
					TArray<int32> Selected = GeometryCollectionComponent->GetSelectedBones();
					FGeometryCollectionClusteringUtility::MoveUpOneHierarchyLevel(GeometryCollection, Selected);

					GeometryCollectionComponent->MarkRenderDynamicDataDirty();
					GeometryCollectionComponent->MarkRenderStateDirty();
				}
			}
		}

		InToolkit.Pin()->SetOutlinerComponents(GeomCompSelection.Array());
	}
}

#undef LOCTEXT_NAMESPACE
