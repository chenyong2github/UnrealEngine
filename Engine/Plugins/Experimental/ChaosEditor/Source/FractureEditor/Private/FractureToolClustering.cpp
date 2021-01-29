// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolClustering.h"

#include "FractureToolContext.h"

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
		FFractureEditorModeToolkit* Toolkit = InToolkit.Pin().Get();

		TArray<FFractureToolContext> Contexts = GetFractureToolContexts();

		for (FFractureToolContext& Context : Contexts)
		{
			const TManagedArray<int32>& Levels = Context.GetGeometryCollection()->GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);

			Context.ConvertSelectionToClusterNodes();

			for (int32 ClusterIndex : Context.GetSelection())
			{
				TArray<int32> LeafBones;
				FGeometryCollectionClusteringUtility::GetLeafBones(Context.GetGeometryCollection().Get(), ClusterIndex, true, LeafBones);
				FGeometryCollectionClusteringUtility::ClusterBonesUnderExistingNode(Context.GetGeometryCollection().Get(), ClusterIndex, LeafBones);

				// Cleanup: Remove any clusters remaining in the flattened branch.
				FGeometryCollectionClusteringUtility::RemoveDanglingClusters(Context.GetGeometryCollection().Get());

			}

			Refresh(Context, Toolkit);
		}

		SetOutlinerComponents(Contexts, Toolkit);
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
		FFractureEditorModeToolkit* Toolkit = InToolkit.Pin().Get();

		int32 CurrentLevelView = Toolkit->GetLevelViewValue();

		TArray<FFractureToolContext> Contexts = GetFractureToolContexts();

		for (FFractureToolContext& Context : Contexts)
		{
			Context.RemoveRootNodes();
			Context.Sanitize();

			if (Context.GetSelection().Num() > 1)
			{
				// Cluster selected bones beneath common parent
				int32 LowestCommonAncestor = FGeometryCollectionClusteringUtility::FindLowestCommonAncestor(Context.GetGeometryCollection().Get(), Context.GetSelection());

				if (LowestCommonAncestor != INDEX_NONE)
				{
					// ClusterBonesUnderNewNode expects a sibling of the new cluster so we require a child node of the common ancestor.
					const TManagedArray<TSet<int32>>& Children = Context.GetGeometryCollection()->GetAttribute<TSet<int32>>("Children", FGeometryCollection::TransformGroup);
					TArray<int32> Siblings = Children[LowestCommonAncestor].Array();
					if (Siblings.Num() > 0)
					{
						FGeometryCollectionClusteringUtility::ClusterBonesUnderNewNode(Context.GetGeometryCollection().Get(), Siblings[0], Context.GetSelection(), true);
					}
				}

				Refresh(Context, Toolkit);
			}
		}

		if (CurrentLevelView != Toolkit->GetLevelViewValue())
		{
			Toolkit->OnSetLevelViewValue(CurrentLevelView);
		}

		SetOutlinerComponents(Contexts, Toolkit);
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
		FFractureEditorModeToolkit* Toolkit = InToolkit.Pin().Get();

		TArray<FFractureToolContext> Contexts = GetFractureToolContexts();

		for (FFractureToolContext& Context : Contexts)
		{
			const TManagedArray<TSet<int32>>& Children = Context.GetGeometryCollection()->GetAttribute<TSet<int32>>("Children", FGeometryCollection::TransformGroup);
			const TManagedArray<int32>& Levels = Context.GetGeometryCollection()->GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);

			Context.ConvertSelectionToClusterNodes();
			Context.RemoveRootNodes();

			// Once the operation is complete, we'll select the children that were re-leveled
			TArray<int32> NewSelection;
			for (int32 Cluster : Context.GetSelection())
			{
				NewSelection.Append(Children[Cluster].Array());
			}

			FGeometryCollectionClusteringUtility::CollapseHierarchyOneLevel(Context.GetGeometryCollection().Get(), Context.GetSelection());
			Context.SetSelection(NewSelection);
			
			Refresh(Context, Toolkit);
		}

		SetOutlinerComponents(Contexts, Toolkit);
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
		FFractureEditorModeToolkit* Toolkit = InToolkit.Pin().Get();

		TArray<FFractureToolContext> Contexts = GetFractureToolContexts();

		for (FFractureToolContext& Context: Contexts)
		{
			Context.ConvertSelectionToRigidNodes();
			FGeometryCollectionClusteringUtility::MoveUpOneHierarchyLevel(Context.GetGeometryCollection().Get(), Context.GetSelection());
			Refresh(Context, Toolkit);
		}
		
		SetOutlinerComponents(Contexts, Toolkit);
	}
}

#undef LOCTEXT_NAMESPACE
