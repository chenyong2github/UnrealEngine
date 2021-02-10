// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolEmbed.h"

#include "Editor.h"
#include "ScopedTransaction.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"

#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionConversion.h"
#include "GeometryCollection/GeometryCollectionConvexUtility.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "Chaos/Convex.h"
#include "FractureToolContext.h"


#define LOCTEXT_NAMESPACE "FractureToolEmbed"



FText UFractureToolAddEmbeddedGeometry::GetDisplayText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolAddEmbeddedGeometry", "Embed"));
}

FText UFractureToolAddEmbeddedGeometry::GetTooltipText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolAddEmbeddedGeometryTooltip", "Embed selected static mesh as passive geometry parented to selected bone. Will be lost if GeometryCollection is Reset!"));
}

FSlateIcon UFractureToolAddEmbeddedGeometry::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.AddEmbeddedGeometry");
}

void UFractureToolAddEmbeddedGeometry::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "AddEmbeddedGeometry", "Embed", "Embed", EUserInterfaceActionType::ToggleButton, FInputChord());
	BindingContext->AddEmbeddedGeometry = UICommandInfo;
}

bool UFractureToolAddEmbeddedGeometry::CanExecute() const
{
	return (IsStaticMeshSelected() && IsGeometryCollectionSelected());
}

void UFractureToolAddEmbeddedGeometry::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (!InToolkit.IsValid())
	{
		return;
	}

	// Get selected static meshes
	TArray<UStaticMeshComponent*> SelectedStaticMeshComponents = GetSelectedStaticMeshComponents();

	TArray<FFractureToolContext> Contexts = GetFractureToolContexts();

	for (FFractureToolContext& Context : Contexts)
	{
		FGeometryCollectionEdit GeometryCollectionEdit = Context.GetGeometryCollectionComponent()->EditRestCollection(GeometryCollection::EEditUpdate::RestPhysicsDynamic);
		UGeometryCollection* FracturedGeometryCollection = GeometryCollectionEdit.GetRestCollection();

		FFractureToolContext::FGeometryCollectionPtr GeometryCollection = Context.GetGeometryCollection();
		const TManagedArray<FTransform>& Transform = GeometryCollection->Transform;
		TManagedArray<int32>& Parent = GeometryCollection->Parent;
		TManagedArray<int32>& SimType = GeometryCollection->SimulationType;
		TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;
		TManagedArray<bool>& CollectionSimulatableParticles =
			GeometryCollection->GetAttribute<bool>(
				FGeometryCollection::SimulatableParticlesAttribute, FTransformCollection::TransformGroup);

		const FTransform TargetActorTransform(Context.GetGeometryCollectionComponent()->GetOwner()->GetTransform());

		Context.ConvertSelectionToRigidNodes();
		const TArray<int32>& SelectedBones = Context.GetSelection();
		for (const int32 SelectedBone : SelectedBones)
		{
			FTransform BoneGlobalTransform = GeometryCollectionAlgo::GlobalMatrix(Transform, Parent, SelectedBone);
			
			for (UStaticMeshComponent* SelectedStaticMeshComponent : SelectedStaticMeshComponents)
			{
				const AActor* Actor = SelectedStaticMeshComponent->GetOwner();
				check(Actor)

				const FTransform SMActorTransform(Actor->GetTransform());

				UStaticMesh* ComponentStaticMesh = SelectedStaticMeshComponent->GetStaticMesh();
				if (ComponentStaticMesh)
				{
					// If any of the static meshes have Nanite enabled, also enable on the new geometry collection asset for convenience.
					FracturedGeometryCollection->EnableNanite |= ComponentStaticMesh->NaniteSettings.bEnabled;
				}

				FTransform ComponentTransform = SMActorTransform.GetRelativeTransform(TargetActorTransform);
				FTransform BoneTransform = ComponentTransform.GetRelativeTransform(BoneGlobalTransform);

				decltype(FGeometryCollectionSource::SourceMaterial) SourceMaterials(SelectedStaticMeshComponent->GetMaterials());

				// Add static mesh geometry to context's geometry collection.
				FGeometryCollectionConversion::AppendStaticMesh(ComponentStaticMesh, SelectedStaticMeshComponent, BoneTransform, FracturedGeometryCollection, true);

				int32 NewTransformIndex = Parent.Num() - 1;
				check(NewTransformIndex > -1)

				// Reparent the new transform to the selected bone
				Parent[NewTransformIndex] = SelectedBone;
				Children[SelectedBone].Add(NewTransformIndex);

				// Set properties appropriate to embedded geometry
				SimType[NewTransformIndex] = FGeometryCollection::ESimulationTypes::FST_None;
				CollectionSimulatableParticles[NewTransformIndex] = false;
			}
		}

		FracturedGeometryCollection->InitializeMaterials();

		FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(GeometryCollection.Get(), -1);
		Context.GetGeometryCollectionComponent()->MarkRenderStateDirty();

		FracturedGeometryCollection->MarkPackageDirty();
	}

	SetOutlinerComponents(Contexts, InToolkit.Pin().Get());
}

TArray<UStaticMeshComponent*> UFractureToolAddEmbeddedGeometry::GetSelectedStaticMeshComponents()
{
	TArray<UStaticMeshComponent*> SelectedStaticMeshComponents;

	USelection* SelectedActors = GEditor->GetSelectedActors();
	for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
	{
		AActor* Actor = Cast<AActor>(*Iter);
		if (Actor)
		{
			// We don't want to include any static meshes owned by a geometry collection.
			// (This might be an ISMC or swap out static geometry.)
			if (Actor->FindComponentByClass<UGeometryCollectionComponent>())
			{
				continue;
			}

			TArray<UStaticMeshComponent*> StaticMeshComponents;
			Actor->GetComponents<UStaticMeshComponent>(StaticMeshComponents, true);
			SelectedStaticMeshComponents.Append(StaticMeshComponents);
		}
	}

	return SelectedStaticMeshComponents;
}



FText UFractureToolAutoEmbedGeometry::GetDisplayText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolAutoEmbedGeometry", "Auto"));
}

FText UFractureToolAutoEmbedGeometry::GetTooltipText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolAutoEmbedGeometryTooltip", "Embed selected static meshes as passive geometry parented to nearest bone. Will be lost if GeometryCollection is Reset!"));
}

FSlateIcon UFractureToolAutoEmbedGeometry::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.AutoEmbedGeometry");
}

void UFractureToolAutoEmbedGeometry::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "AutoEmbedGeometry", "Auto", "Auto", EUserInterfaceActionType::ToggleButton, FInputChord());
	BindingContext->AutoEmbedGeometry = UICommandInfo;
}

bool UFractureToolAutoEmbedGeometry::CanExecute() const
{
	return (IsStaticMeshSelected() && IsGeometryCollectionSelected());
}

void UFractureToolAutoEmbedGeometry::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (!InToolkit.IsValid())
	{
		return;
	}

	// Get selected static meshes
	TArray<UStaticMeshComponent*> SelectedStaticMeshComponents = GetSelectedStaticMeshComponents();
	TArray<FFractureToolContext> Contexts = GetFractureToolContexts();

	// For each static mesh component, we iterate all the convex hulls and determine which convex hull best contains the worldspace pivot of the static mesh.
	for (UStaticMeshComponent* SelectedStaticMeshComponent : SelectedStaticMeshComponents)
	{
		// Static Mesh world space location
		FVector SMLocation = SelectedStaticMeshComponent->GetComponentLocation();

		// Determine the "closest" bone
		FFractureToolContext::FGeometryCollectionPtr ClosestGeometryCollection;
		int32 ClosestConvex = INDEX_NONE;
		Chaos::FReal ClosestPhi = TNumericLimits<float>::Max();
		UGeometryCollectionComponent* ClosestComponent = nullptr;
		
		for (FFractureToolContext& Context : Contexts)
		{
			FGeometryCollection* GeometryCollection = Context.GetGeometryCollection().Get();
			FGeometryCollectionConvexUtility::FGeometryCollectionConvexData ConvexData = FGeometryCollectionConvexUtility::GetValidConvexHullData(GeometryCollection);
			const TManagedArray<FTransform>& Transform = GeometryCollection->Transform;
			const TManagedArray<int32>& Parent = GeometryCollection->Parent;
			TArray<FTransform> BoneGlobalTransforms;
			GeometryCollectionAlgo::GlobalMatrices(Transform, Parent, BoneGlobalTransforms);
			
			FTransform WorldToComponent = Context.GetGeometryCollectionComponent()->GetComponentToWorld().Inverse();
			FVector ComponentSpaceLocation = WorldToComponent.TransformPosition(SMLocation);

			int32 NumConvex = ConvexData.ConvexHull.Num();
			for (int32 ConvexIndex = 0; ConvexIndex < NumConvex; ++ConvexIndex)
			{
				// transform into bone space where the convex hull is described
				int32 TransformIndex = ConvexData.TransformToConvexIndex.Find(ConvexIndex);
				FTransform ComponentToBone = BoneGlobalTransforms[TransformIndex].Inverse();
				FVector BoneSpaceLocation = ComponentToBone.TransformPosition(ComponentSpaceLocation);
				
				Chaos::FVec3 Normal;
				Chaos::FReal Phi = ConvexData.ConvexHull[ConvexIndex]->PhiWithNormal(BoneSpaceLocation, Normal);
				UE_LOG(LogTemp, Warning, TEXT(" Cone %d Phi %f"), ConvexData.TransformToConvexIndex.Find(ConvexIndex), Phi);
				if (Phi < ClosestPhi)
				{
					ClosestPhi = Phi;
					ClosestGeometryCollection = Context.GetGeometryCollection();
					ClosestConvex = ConvexIndex;
					ClosestComponent = Context.GetGeometryCollectionComponent();
				}
			}
		}

		if (ClosestGeometryCollection.IsValid())
		{
			// Which bone points to the closest convex?
			const TManagedArray<FTransform>& Transform = ClosestGeometryCollection->Transform;
			TManagedArray<int32>& Parent = ClosestGeometryCollection->Parent;
			TManagedArray<int32>& SimType = ClosestGeometryCollection->SimulationType;
			TManagedArray<TSet<int32>>& Children = ClosestGeometryCollection->Children;
			TManagedArray<bool>& CollectionSimulatableParticles =
				ClosestGeometryCollection->GetAttribute<bool>(
					FGeometryCollection::SimulatableParticlesAttribute, FTransformCollection::TransformGroup);
			TManagedArray<int32>& TransformToConvexIndex =
				ClosestGeometryCollection->GetAttribute<int32>("TransformToConvexIndex", FTransformCollection::TransformGroup);

			const int32 BoneIndex = TransformToConvexIndex.Find(ClosestConvex);

			if (BoneIndex > INDEX_NONE)
			{
				// We found the closest bone, now we embed the geometry.
				FGeometryCollectionEdit GeometryCollectionEdit = ClosestComponent->EditRestCollection(GeometryCollection::EEditUpdate::RestPhysicsDynamic);
				UGeometryCollection* FracturedGeometryCollection = GeometryCollectionEdit.GetRestCollection();

				FTransform BoneGlobalTransform = GeometryCollectionAlgo::GlobalMatrix(Transform, Parent, BoneIndex);

				const FTransform TargetActorTransform(ClosestComponent->GetOwner()->GetTransform());
				const AActor* Actor = SelectedStaticMeshComponent->GetOwner();
				check(Actor)

				const FTransform SMActorTransform(Actor->GetTransform());
				FTransform ComponentTransform = SMActorTransform.GetRelativeTransform(TargetActorTransform);
				FTransform BoneTransform = ComponentTransform.GetRelativeTransform(BoneGlobalTransform);

				UStaticMesh* ComponentStaticMesh = SelectedStaticMeshComponent->GetStaticMesh();
				if (ComponentStaticMesh)
				{
					// If any of the static meshes have Nanite enabled, also enable on the new geometry collection asset for convenience.
					FracturedGeometryCollection->EnableNanite |= ComponentStaticMesh->NaniteSettings.bEnabled;
				}

				// Add static mesh geometry to context's geometry collection.
				FGeometryCollectionConversion::AppendStaticMesh(ComponentStaticMesh, SelectedStaticMeshComponent, BoneTransform, FracturedGeometryCollection, true);

				// A new transform has been appended to the TransformGroup
				int32 NewTransformIndex = Parent.Num() - 1;
				check(NewTransformIndex > -1)

				// Reparent the new transform to the selected bone
				Parent[NewTransformIndex] = BoneIndex;
				Children[BoneIndex].Add(NewTransformIndex);

				// Set properties appropriate to embedded geometry
				SimType[NewTransformIndex] = FGeometryCollection::ESimulationTypes::FST_None;
				CollectionSimulatableParticles[NewTransformIndex] = false;

				FracturedGeometryCollection->InitializeMaterials();

				FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(ClosestGeometryCollection.Get(), -1);
				ClosestComponent->MarkRenderStateDirty();

				FracturedGeometryCollection->MarkPackageDirty();
			}
		}
	}

	SetOutlinerComponents(Contexts, InToolkit.Pin().Get());
}

TArray<UStaticMeshComponent*> UFractureToolAutoEmbedGeometry::GetSelectedStaticMeshComponents()
{
	TArray<UStaticMeshComponent*> SelectedStaticMeshComponents;

	USelection* SelectedActors = GEditor->GetSelectedActors();
	for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
	{
		AActor* Actor = Cast<AActor>(*Iter);
		if (Actor)
		{
			// We don't want to include any static meshes owned by a geometry collection.
			// (This might be an ISMC or swap out static geometry.)
			if (Actor->FindComponentByClass<UGeometryCollectionComponent>())
			{
				continue;
			}

			TArray<UStaticMeshComponent*> StaticMeshComponents;
			Actor->GetComponents<UStaticMeshComponent>(StaticMeshComponents, true);
			SelectedStaticMeshComponents.Append(StaticMeshComponents);
		}
	}

	return SelectedStaticMeshComponents;
}




FText UFractureToolDeleteEmbeddedGeometry::GetDisplayText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolDeleteEmbeddedGeometry", "Delete"));
}

FText UFractureToolDeleteEmbeddedGeometry::GetTooltipText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolDeleteEmbeddedGeometryTooltip", "Delete selected embedded geometry nodes. If a cluster or rigid node is selected, all child embedded geometry nodes are deleted."));
}

FSlateIcon UFractureToolDeleteEmbeddedGeometry::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.DeleteEmbeddedGeometry");
}

void UFractureToolDeleteEmbeddedGeometry::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "DeleteEmbeddedGeometry", "Delete", "Delete", EUserInterfaceActionType::ToggleButton, FInputChord());
	BindingContext->DeleteEmbeddedGeometry = UICommandInfo;
}

bool UFractureToolDeleteEmbeddedGeometry::CanExecute() const
{
	return IsGeometryCollectionSelected();
}

void UFractureToolDeleteEmbeddedGeometry::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (!InToolkit.IsValid())
	{
		return;
	}

	// Get selected static meshes
	TArray<FFractureToolContext> Contexts = GetFractureToolContexts();

	for (FFractureToolContext& Context : Contexts)
	{
		Context.Sanitize();
		
		FGeometryCollectionEdit GeometryCollectionEdit = Context.GetGeometryCollectionComponent()->EditRestCollection(GeometryCollection::EEditUpdate::RestPhysicsDynamic);
		UGeometryCollection* FracturedGeometryCollection = GeometryCollectionEdit.GetRestCollection();

		FFractureToolContext::FGeometryCollectionPtr GeometryCollection = Context.GetGeometryCollection();
		const TManagedArray<int32>& SimType = GeometryCollection->SimulationType;

		TArray<int32> EmbeddedGeometryToBeRemoved;
		const TArray<int32>& SelectedBones = Context.GetSelection();
		for (const int32 SelectedBone : SelectedBones)
		{
			if (SimType[SelectedBone] != FGeometryCollection::ESimulationTypes::FST_None)
			{
				// Select all Embedded Geometry found in the selected branch.
				TArray<int32> LeafBones;
				FGeometryCollectionClusteringUtility::GetLeafBones(GeometryCollection.Get(), SelectedBone, false, LeafBones);
				for (int32 LeafBone : LeafBones)
				{
					if (SimType[LeafBone] == FGeometryCollection::ESimulationTypes::FST_None)
					{
						EmbeddedGeometryToBeRemoved.Add(LeafBone);
					}
				}
			}
			else
			{
				// Selected bone is embedded geometry. Only delete this.
				EmbeddedGeometryToBeRemoved.Add(SelectedBone);
			}
		}

		EmbeddedGeometryToBeRemoved.Sort();
		GeometryCollection->RemoveElements(FGeometryCollection::TransformGroup, EmbeddedGeometryToBeRemoved);

		Context.GetGeometryCollectionComponent()->MarkRenderStateDirty();
		FracturedGeometryCollection->MarkPackageDirty();
	}

	SetOutlinerComponents(Contexts, InToolkit.Pin().Get());
}










#undef LOCTEXT_NAMESPACE