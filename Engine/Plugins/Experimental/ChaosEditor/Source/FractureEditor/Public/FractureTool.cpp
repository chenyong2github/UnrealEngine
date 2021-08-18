// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureTool.h"

#include "Editor.h"
#include "Engine/Selection.h"

#include "FractureToolContext.h"
#include "FractureSelectionTools.h"

#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionProximityUtility.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"


DEFINE_LOG_CATEGORY(LogFractureTool);

void UFractureToolSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (OwnerTool != nullptr)
	{
		OwnerTool->PostEditChangeProperty(PropertyChangedEvent);
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UFractureToolSettings::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (OwnerTool != nullptr)
	{
		OwnerTool->PostEditChangeChainProperty(PropertyChangedEvent);
	}
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}




const TSharedPtr<FUICommandInfo>& UFractureActionTool::GetUICommandInfo() const
{
	return UICommandInfo;
}

bool UFractureActionTool::CanExecute() const
{
	return IsGeometryCollectionSelected();
}

bool UFractureActionTool::IsGeometryCollectionSelected()
{
	USelection* SelectedActors = GEditor->GetSelectedActors();
	TArray<ULevel*> UniqueLevels;
	for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
	{
		AActor* Actor = Cast<AActor>(*Iter);
		if (Actor)
		{
			if (Actor->FindComponentByClass<UGeometryCollectionComponent>())
			{
				return true;
			}
		}
	}
	return false;
}

bool UFractureActionTool::IsStaticMeshSelected()
{
	USelection* SelectedActors = GEditor->GetSelectedActors();
	for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
	{
		AActor* Actor = Cast<AActor>(*Iter);
		if (Actor)
		{
			TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents;
			Actor->GetComponents<UStaticMeshComponent>(StaticMeshComponents, true);

			if (StaticMeshComponents.Num() > 0)
			{
				return true;
			}
		}
	}
	return false;
}

void UFractureActionTool::AddSingleRootNodeIfRequired(UGeometryCollection* GeometryCollectionObject)
{
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
	if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
	{
		if (FGeometryCollectionClusteringUtility::ContainsMultipleRootBones(GeometryCollection))
		{
			FGeometryCollectionClusteringUtility::ClusterAllBonesUnderNewRoot(GeometryCollection);
		}
	}
}

void UFractureActionTool::AddAdditionalAttributesIfRequired(UGeometryCollection* GeometryCollectionObject)
{
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
	if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
	{
		if (!GeometryCollection->HasAttribute("Level", FGeometryCollection::TransformGroup))
		{
			FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(GeometryCollection, -1);
		}
	}
}

void UFractureActionTool::GetSelectedGeometryCollectionComponents(TSet<UGeometryCollectionComponent*>& GeomCompSelection)
{
	USelection* SelectionSet = GEditor->GetSelectedActors();
	TArray<AActor*> SelectedActors;
	SelectedActors.Reserve(SelectionSet->Num());
	SelectionSet->GetSelectedObjects(SelectedActors);

	GeomCompSelection.Empty(SelectionSet->Num());

	for (AActor* Actor : SelectedActors)
	{
		TInlineComponentArray<UGeometryCollectionComponent*> GeometryCollectionComponents;
		Actor->GetComponents(GeometryCollectionComponents);
		GeomCompSelection.Append(GeometryCollectionComponents);
	}
}

void UFractureActionTool::Refresh(FFractureToolContext& Context, FFractureEditorModeToolkit* Toolkit)
{
	UGeometryCollectionComponent* GeometryCollectionComponent = Context.GetGeometryCollectionComponent();
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = Context.GetGeometryCollection();
	
	FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(GeometryCollectionPtr.Get(), -1);

	Toolkit->RegenerateOutliner();
	Toolkit->RegenerateHistogram();

	FScopedColorEdit EditBoneColor(GeometryCollectionComponent, true);
	EditBoneColor.SetSelectedBones(Context.GetSelection());

	FFractureSelectionTools::ToggleSelectedBones(GeometryCollectionComponent, Context.GetSelection(), true);

	Toolkit->UpdateExplodedVectors(GeometryCollectionComponent);

	GeometryCollectionComponent->MarkRenderDynamicDataDirty();
	GeometryCollectionComponent->MarkRenderStateDirty();
}

void UFractureActionTool::SetOutlinerComponents(TArray<FFractureToolContext>& InContexts, FFractureEditorModeToolkit* Toolkit)
{
	// Extract components from contexts
	TArray<UGeometryCollectionComponent*> Components;
	for (FFractureToolContext& Context : InContexts)
	{
		Components.Add(Context.GetGeometryCollectionComponent());
	}
	Toolkit->SetOutlinerComponents(Components);
}

void UFractureActionTool::ClearProximity(FGeometryCollection* GeometryCollection)
{
	if (GeometryCollection->HasAttribute("Proximity", FGeometryCollection::GeometryGroup))
	{
		GeometryCollection->RemoveAttribute("Proximity", FGeometryCollection::GeometryGroup);
	}
}

void UFractureActionTool::GenerateProximityIfNecessary(FGeometryCollection* GeometryCollection)
{
	if (!GeometryCollection->HasAttribute("Proximity", FGeometryCollection::GeometryGroup))
	{
		FGeometryCollectionProximityUtility::UpdateProximity(GeometryCollection);
	}
}

TArray<FFractureToolContext> UFractureActionTool::GetFractureToolContexts() const
{
	TArray<FFractureToolContext> Contexts;

	// A context is gathered for each selected GeometryCollection component.
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);
	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		Contexts.Emplace(GeometryCollectionComponent);
	}

	return Contexts;
}


void UFractureModalTool::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (InToolkit.IsValid())
	{
		FFractureEditorModeToolkit* Toolkit = InToolkit.Pin().Get();

		TArray<FFractureToolContext> FractureContexts = GetFractureToolContexts();

		for (FFractureToolContext& FractureContext : FractureContexts)
		{
			FractureContext.GetFracturedGeometryCollection()->Modify();

			int32 FirstNewGeometryIndex = ExecuteFracture(FractureContext);
			
			if (FirstNewGeometryIndex > INDEX_NONE)
			{
				// Based on the first new geometry index, generate a list of new transforms generated by the fracture.
				const TManagedArray<int32>& TransformIndex = FractureContext.GetGeometryCollection()->GetAttribute<int32>("TransformIndex", FGeometryCollection::GeometryGroup);
				int32 LastGeometryIndex = TransformIndex.Num();

				TArray<int32> NewTransforms;
				NewTransforms.Reserve(LastGeometryIndex - FirstNewGeometryIndex);
				for (int32 GeometryIndex = FirstNewGeometryIndex; GeometryIndex < LastGeometryIndex; ++GeometryIndex)
				{
					NewTransforms.Add(TransformIndex[GeometryIndex]);
				}

				FractureContext.SetSelection(NewTransforms);

				Toolkit->RegenerateHistogram();
			}
			

			FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(FractureContext.GetGeometryCollection().Get(), -1);

			Refresh(FractureContext, Toolkit);
		}

		SetOutlinerComponents(FractureContexts, Toolkit);
	}
}

bool UFractureModalTool::CanExecute() const
{
	if (!IsGeometryCollectionSelected())
	{
		return false;
	}

	if (IsStaticMeshSelected())
	{
		return false;
	}

	return true;
}

void UFractureModalTool::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	FractureContextChanged();
}

