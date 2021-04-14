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

			// Proximity is invalidated.
			ClearProximity(Context.GetGeometryCollection().Get());

			Refresh(Context, Toolkit, true);
		}

		SetOutlinerComponents(Contexts, Toolkit);
	}
}



FText UFractureToolValidate::GetDisplayText() const
{
	return FText(NSLOCTEXT("FractureToolEditingOps", "FractureToolValidate", "Validate"));
}

FText UFractureToolValidate::GetTooltipText() const
{
	return FText(NSLOCTEXT("FractureToolEditingOps", "FractureToolValidateTooltip", "Ensure that geometry collection is valid and clean."));
}

FSlateIcon UFractureToolValidate::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.Validate");
}

void UFractureToolValidate::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "Validate", "Validate", "Ensure that geometry collection is valid and clean.", EUserInterfaceActionType::Button, FInputChord());
	BindingContext->Validate = UICommandInfo;
}

void UFractureToolValidate::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (InToolkit.IsValid())
	{
		FFractureEditorModeToolkit* Toolkit = InToolkit.Pin().Get();

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
					bool bDirty = false;

					// Ensure that clusters do not point to geometry
					TManagedArray<int32>& TransformToGeometry = GeometryCollection->TransformToGeometryIndex;
					const int32 ElementCount = TransformToGeometry.Num();
					for (int32 Idx = 0; Idx < ElementCount; ++Idx)
					{
						if (GeometryCollection->IsClustered(Idx) && TransformToGeometry[Idx] != INDEX_NONE)
						{
							TransformToGeometry[Idx] = INDEX_NONE;
							UE_LOG(LogFractureTool, Warning, TEXT("Removed geometry index from cluster %d."), Idx);
							bDirty = true;
						}
					}

					// Remove any unreferenced geometry
					TManagedArray<int32>& TransformIndex = GeometryCollection->TransformIndex;
					const int32 GeometryCount = TransformIndex.Num();

					TArray<int32> RemoveGeometry;
					RemoveGeometry.Reserve(GeometryCount);

					for (int32 Idx = 0; Idx < GeometryCount; ++Idx)
					{
						if ((TransformIndex[Idx] == INDEX_NONE) || (TransformToGeometry[TransformIndex[Idx]] != Idx))
						{
							RemoveGeometry.Add(Idx);
							UE_LOG(LogFractureTool, Warning, TEXT("Removed dangling geometry at index %d."), Idx);
							bDirty = true;
						}
					}

					if (RemoveGeometry.Num() > 0)
					{
						GeometryCollection->RemoveElements(FGeometryCollection::GeometryGroup, RemoveGeometry);
					}

					// remove dangling clusters
					// #todo leaving this out for the moment because we don't want to invalidate existing caches.
					// FGeometryCollectionClusteringUtility::RemoveDanglingClusters(GeometryCollection);

					bDirty |= StripUnnecessaryAttributes(GeometryCollection);

					if (bDirty)
					{
						FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(GeometryCollection, -1);
						AddSingleRootNodeIfRequired(GeometryCollectionObject);
						GeometryCollectionComponent->MarkRenderStateDirty();
						GeometryCollectionObject->MarkPackageDirty();
					}

				}
			}

			GeometryCollectionComponent->InitializeEmbeddedGeometry();

			FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
			EditBoneColor.ResetBoneSelection();
			EditBoneColor.ResetHighlightedBones();
		}

		Toolkit->OnSetLevelViewValue(-1);
		Toolkit->SetOutlinerComponents(GeomCompSelection.Array());	
	}
}

bool UFractureToolValidate::StripUnnecessaryAttributes(FGeometryCollection* GeometryCollection)
{	
	static TMap<FName, TArray<FName>> Necessary = {
		{ "Transform", 
			{
			"GUID",
			"Transform",
			"BoneColor",
			"Parent",
			"Children",
			"TransformToGeometryIndex",
			"SimulationType",
			"StatusFlags",
			"InitialDynamicState",
			"SimulatableParticlesAttribute",
			"InertiaTensor",
			"Mass",
			"ExemplarIndex",
			"MassToLocal",
			"DefaultMaterialIndex"
			} 
		},
		
		{ "Vertices",
			{
			"Vertex",
			"UV",
			"Color",
			"TangentU",
			"TangentV",
			"Normal",
			"BoneMap"
			}
		},
		
		{ "Faces",
			{
			"Indices",
			"Visible",
			"MaterialIndex",
			"MaterialID"
			}
		},

		{ "Geometry",
			{
			"TransformIndex",
			"BoundingBox",
			"InnerRadius",
			"OuterRadius",
			"VertexStart",
			"VertexCount",
			"FaceStart",
			"FaceCount"
			}
		},

		{ "Material",
			{
			"Sections"
			}
		}
	};

	bool bChangesMade = false;

	const TArray<FName> GroupNames = GeometryCollection->GroupNames();
	for (const FName Group : GroupNames)
	{
		if (Necessary.Contains(Group))
		{
			const TArray<FName> AttributeNames = GeometryCollection->AttributeNames(Group);
			const TArray<FName>& NecessaryAttributes = Necessary[Group];
			for (const FName AttributeName : AttributeNames)
			{
				if (!NecessaryAttributes.Contains(AttributeName))
				{
					GeometryCollection->RemoveAttribute(AttributeName, Group);
					bChangesMade = true;
				}
			}
		}
		else
		{
			GeometryCollection->RemoveGroup(Group);
			bChangesMade = true;
		}
	}

	return bChangesMade;
}

#undef LOCTEXT_NAMESPACE

