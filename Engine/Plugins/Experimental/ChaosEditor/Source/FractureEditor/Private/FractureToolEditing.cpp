// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolEditing.h"

#include "Editor.h"
#include "Dialogs/Dialogs.h"

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

bool UFractureToolValidate::StripAttributes(FGeometryCollection* GeometryCollection, const TMap<FName, TArray<FName>>& Whitelist)
{
	bool bChangesMade = false;

	const TArray<FName> GroupNames = GeometryCollection->GroupNames();
	for (const FName Group : GroupNames)
	{
		if (Whitelist.Contains(Group))
		{
			const TArray<FName> AttributeNames = GeometryCollection->AttributeNames(Group);
			const TArray<FName>& WhitelistAttributes = Whitelist[Group];
			for (const FName AttributeName : AttributeNames)
			{
				if (!WhitelistAttributes.Contains(AttributeName))
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
			"DefaultMaterialIndex",
			"Implicits",
			"CollisionParticles"
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

	return StripAttributes(GeometryCollection, Necessary);
}




FText UFractureToolStripSimulationData::GetDisplayText() const
{
	return FText(NSLOCTEXT("FractureToolEditingOps", "StripSimulationData", "Strip"));
}

FText UFractureToolStripSimulationData::GetTooltipText() const
{
	return FText(NSLOCTEXT("FractureToolEditingOps", "FractureToolStripSimulationDataTooltip", "Remove data needed for simulation. WARNING: Geometry Collectin will no longer accurately simulate!"));
}

FSlateIcon UFractureToolStripSimulationData::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.StripSimulationData");
}

void UFractureToolStripSimulationData::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "StripSimulationData", "Strip", "Remove data needed for simulation. WARNING: Geometry Collection will no longer accurately simulate!", EUserInterfaceActionType::Button, FInputChord());
	BindingContext->StripSimulationData = UICommandInfo;
}

void UFractureToolStripSimulationData::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
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
					static TMap<FName, TArray<FName>> Removals = {
						{ "Transform",
							{
							"SimulatableParticlesAttribute",
							"Implicits",
							"CollisionParticles"
							}
						}
					};

					FSuppressableWarningDialog::FSetupInfo Info(LOCTEXT("WarningStripSimulationData", "This will strip important simulation data from this GeometryCollection. This is a suitable choice if the GeometryCollection will only be used for cached playback. It will result in unpredictable behaviour if the GeometryCollection becomes dynamic. Do you want to continue?"), LOCTEXT("WarningStripSimulationData_Title", "Stripping Simulation Data"), TEXT("bStripSimulationDataWarning"), GEditorSettingsIni);
					Info.ConfirmText = LOCTEXT("OK", "OK");
					Info.CancelText = LOCTEXT("Cancel", "Cancel");
					Info.bDefaultToSuppressInTheFuture = false;
					FSuppressableWarningDialog StripSimulationDataWarning(Info);
					if(StripSimulationDataWarning.ShowModal() != FSuppressableWarningDialog::EResult::Cancel)
					{
						for (const TPair<FName, TArray<FName>> Removal : Removals)
						{
							for (const FName AttributeName : Removal.Value)
							{
								if (GeometryCollection->HasAttribute(AttributeName, Removal.Key))
								{
									GeometryCollection->RemoveAttribute(AttributeName, Removal.Key);
								}
							}
						}
					}				
				}
			}
		}

		Toolkit->SetOutlinerComponents(GeomCompSelection.Array());
	}
}


#undef LOCTEXT_NAMESPACE

