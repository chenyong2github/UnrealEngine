// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolEditing.h"

#include "Editor.h"

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "FractureToolContext.h"


#define LOCTEXT_NAMESPACE "FractureToolEditing"


FText UFractureToolDeleteBranch::GetDisplayText() const
{
	return FText(NSLOCTEXT("FractureToolEditingOps", "FractureToolDeleteBranch", "Delete"));
}

FText UFractureToolDeleteBranch::GetTooltipText() const
{
	return FText(NSLOCTEXT("FractureToolEditingOps", "FractureToolDeleteBranchTooltip", "Delete all nodes in selected branch. Empty clusters will be eliminated."));
}

FSlateIcon UFractureToolDeleteBranch::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.DeleteBranch");
}

void UFractureToolDeleteBranch::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "DeleteBranch", "Prune", "Delete all nodes in selected branch. Empty clusters will be eliminated.", EUserInterfaceActionType::Button, FInputChord());
	BindingContext->DeleteBranch = UICommandInfo;
}

void UFractureToolDeleteBranch::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (InToolkit.IsValid())
	{
		FFractureEditorModeToolkit* Toolkit = InToolkit.Pin().Get();

		TArray<FFractureToolContext> Contexts = GetFractureToolContexts();

		for (FFractureToolContext& Context : Contexts)
		{
			FGeometryCollection* GeometryCollection = Context.GetGeometryCollection().Get();
			UGeometryCollection* FracturedGeometryCollection = Context.GetFracturedGeometryCollection();

			const TManagedArray<int32>& ExemplarIndex = GeometryCollection->ExemplarIndex;
			const TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;
			
			// Removing the root node amounts to full deletion -- we don't allow this here.
			Context.RemoveRootNodes();
			Context.Sanitize();

			TArray<int32> NodesForDeletion;

			for (int32 Select : Context.GetSelection())
			{
				FGeometryCollectionClusteringUtility::RecursiveAddAllChildren(Children, Select, NodesForDeletion);
			}

			// Clean up any embedded geometry removal
			TArray<int32> UninstancedExemplars;
			UninstancedExemplars.Reserve(NodesForDeletion.Num());
			for (int32 DeleteNode : NodesForDeletion)
			{
				if (ExemplarIndex[DeleteNode] > INDEX_NONE)
				{
					if ((--FracturedGeometryCollection->EmbeddedGeometryExemplar[ExemplarIndex[DeleteNode]].InstanceCount) < 1)
					{
						UE_LOG(LogFractureTool, Warning, TEXT("Exemplar Index %d is empty. Removing Exemplar from Geometry Collection."), ExemplarIndex[DeleteNode]);
						UninstancedExemplars.Add(ExemplarIndex[DeleteNode]);
					}
				}
			}

			UninstancedExemplars.Sort();
			FracturedGeometryCollection->RemoveExemplars(UninstancedExemplars);
			GeometryCollection->ReindexExemplarIndices(UninstancedExemplars);

			NodesForDeletion.Sort();
			GeometryCollection->RemoveElements(FGeometryCollection::TransformGroup, NodesForDeletion);

			FGeometryCollectionClusteringUtility::RemoveDanglingClusters(GeometryCollection);

			Context.GetGeometryCollectionComponent()->InitializeEmbeddedGeometry();

			Refresh(Context, Toolkit, true);
		}

		SetOutlinerComponents(Contexts, Toolkit);
	}
}


#undef LOCTEXT_NAMESPACE

