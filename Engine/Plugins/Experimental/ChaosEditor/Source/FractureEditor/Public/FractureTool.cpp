// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureTool.h"

#include "Editor.h"
#include "Engine/Selection.h"

#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
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


void UFractureModalTool::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (!InToolkit.IsValid())
	{
		return;
	}

	TArray<FFractureToolContext> FractureContexts;
	GetFractureContexts(FractureContexts);

	TArray<UGeometryCollectionComponent*> NewComponents;
	for (FFractureToolContext& FractureContext : FractureContexts)
	{	
		FractureContext.FracturedGeometryCollection->Modify();

		ExecuteFracture(FractureContext);

		{
			TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = FractureContext.FracturedGeometryCollection->GetGeometryCollection();
			FGeometryCollection* OutGeometryCollection = GeometryCollectionPtr.Get();
			FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(OutGeometryCollection, -1);
		}

		UGeometryCollectionComponent* GeometryCollectionComponent = Cast<UGeometryCollectionComponent>(FractureContext.OriginalPrimitiveComponent);
		NewComponents.AddUnique(GeometryCollectionComponent);
		FGeometryCollectionEdit GCEdit = GeometryCollectionComponent->EditRestCollection();
		UGeometryCollection* GCObject = GCEdit.GetRestCollection();
		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GCObject->GetGeometryCollection();
		FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(GeometryCollectionPtr.Get(), -1);

		FScopedColorEdit EditBoneColor(GeometryCollectionComponent, true);
		EditBoneColor.SelectBones(GeometryCollection::ESelectionMode::None);
		InToolkit.Pin()->SetBoneSelection(GeometryCollectionComponent, EditBoneColor.GetSelectedBones(), true);

		InToolkit.Pin()->UpdateExplodedVectors(GeometryCollectionComponent);

		GeometryCollectionComponent->MarkRenderDynamicDataDirty();
		GeometryCollectionComponent->MarkRenderStateDirty();
		
	}

	InToolkit.Pin()->SetOutlinerComponents(NewComponents);
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

void UFractureModalTool::GetFractureContexts(TArray<FFractureToolContext>& FractureContexts) const
{
	TSet<UGeometryCollectionComponent*> GeometryCollectionComponents;
	GetSelectedGeometryCollectionComponents(GeometryCollectionComponents);

	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeometryCollectionComponents)
	{
		FGeometryCollectionEdit RestCollection = GeometryCollectionComponent->EditRestCollection(GeometryCollection::EEditUpdate::None);
		UGeometryCollection* FracturedGeometryCollection = RestCollection.GetRestCollection();
		if (FracturedGeometryCollection == nullptr)
		{
			continue;
		}

		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = FracturedGeometryCollection->GetGeometryCollection();
		FGeometryCollection* OutGeometryCollection = GeometryCollectionPtr.Get();

		TArray<int32> FilteredBones = FilterBones(GeometryCollectionComponent->GetSelectedBones(), OutGeometryCollection);


		const TManagedArray<FTransform>& Transform = OutGeometryCollection->GetAttribute<FTransform>("Transform", FGeometryCollection::TransformGroup);
		const TManagedArray<int32>& TransformToGeometryIndex = OutGeometryCollection->GetAttribute<int32>("TransformToGeometryIndex", FGeometryCollection::TransformGroup);
		const TManagedArray<FBox>& BoundingBoxes = OutGeometryCollection->GetAttribute<FBox>("BoundingBox", FGeometryCollection::GeometryGroup);

		TArray<FTransform> Transforms;
		GeometryCollectionAlgo::GlobalMatrices(Transform, OutGeometryCollection->Parent, Transforms);


		TMap<int32, FBox> BoundsToBone;
		for (int32 Idx = 0, ni = FracturedGeometryCollection->NumElements(FGeometryCollection::TransformGroup); Idx < ni; ++Idx)
		{
			if (TransformToGeometryIndex[Idx] > -1)
			{
				ensure(TransformToGeometryIndex[Idx] > -1);
				BoundsToBone.Add(Idx, BoundingBoxes[TransformToGeometryIndex[Idx]].TransformBy(Transforms[Idx]));
			}
		}

		GenerateFractureToolContext(
			GeometryCollectionComponent->GetOwner(),
			GeometryCollectionComponent,
			FracturedGeometryCollection,
			FilteredBones,
			BoundsToBone,
			TransformToGeometryIndex,
			-1,
			FractureContexts);

	}
}

TArray<int32> UFractureModalTool::FilterBones(const TArray<int32>& SelectedBonesOriginal, const FGeometryCollection* const GeometryCollection) const
{
	// Keep only leaf nodes
	const TManagedArray<TSet<int32>>& Children = GeometryCollection->GetAttribute<TSet<int32>>("Children", FGeometryCollection::TransformGroup);

	TArray<int32> SelectedBones;
	SelectedBones.Reserve(SelectedBonesOriginal.Num());
	for (int32 BoneIndex : SelectedBonesOriginal)
	{
		if (Children[BoneIndex].Num() == 0)
		{
			SelectedBones.Add(BoneIndex);
		}
	}

	return SelectedBones;
}

void UFractureModalTool::GenerateFractureToolContext(
	AActor* InActor, 
	UGeometryCollectionComponent* InComponent, 
	UGeometryCollection* InGeometryCollection, 
	const TArray<int32>& InSelectedBones, 
	TMap<int32, FBox>& InBoundsToBone,
	const TManagedArray<int32>& TransformToGeometryIndex,
	int32 RandomSeed, 
	TArray<FFractureToolContext>& OutFractureContexts
) const
{
	OutFractureContexts.AddDefaulted();
	FFractureToolContext& FractureContext = OutFractureContexts.Last();
	FractureContext.RandomSeed = FMath::Rand();
	if (RandomSeed > -1)
	{
		// make sure it's unique for each context if it's specified.
		FractureContext.RandomSeed = RandomSeed + OutFractureContexts.Num();
	}

	FractureContext.OriginalActor = InActor;
	FractureContext.Transform = InActor->GetActorTransform();
	FractureContext.OriginalPrimitiveComponent = InComponent;
	FractureContext.FracturedGeometryCollection = InGeometryCollection;
	FractureContext.SelectedBones = InSelectedBones;

	FractureContext.Bounds = FBox(ForceInit);
	for (int32 BoneIndex : FractureContext.SelectedBones)
	{
		if (TransformToGeometryIndex[BoneIndex] > INDEX_NONE)
		{
			FractureContext.Bounds += InBoundsToBone[BoneIndex];
		}
	}
}
