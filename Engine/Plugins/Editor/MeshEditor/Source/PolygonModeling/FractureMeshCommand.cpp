// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FractureMeshCommand.h"
#include "IMeshEditorModeEditingContract.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "MeshAttributes.h"
#include "MeshDescription.h"
#include "PackageTools.h"
#include "FractureMesh.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionCreationParameters.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "EditorSupportDelegates.h"
#include "FractureToolDelegates.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionUtility.h"

#include "MeshUtilities.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "GeometryCollection/GeometryCollectionProximityUtility.h"
#include "HAL/ThreadSafeBool.h"


#define LOCTEXT_NAMESPACE "FractureMeshCommand"

DEFINE_LOG_CATEGORY(LogFractureCommand);

void UFractureMeshCommand::RegisterUICommand( FBindingContext* BindingContext )
{
	UI_COMMAND_EXT( BindingContext, /* Out */ UICommandInfo, "FractureMesh", " Fracture Mesh", "Performs fracture on selected mesh.", EUserInterfaceActionType::Button, FInputChord() );
}

void UFractureMeshCommand::Execute(IMeshEditorModeEditingContract& MeshEditorMode)
{
	if (MeshEditorMode.GetActiveAction() != NAME_None)

	{
		return;
	}

	if (MeshEditorMode.GetSelectedEditableMeshes().Num() == 0)
	{
		return;
	}

	const UMeshFractureSettings* FractureSettings = MeshEditorMode.GetFractureSettings();

	FScopedTransaction Transaction(LOCTEXT("FractureMesh", "Fracture Mesh"));

	MeshEditorMode.CommitSelectedMeshes();

	TArray<UEditableMesh*> SelectedMeshes = MeshEditorMode.GetSelectedEditableMeshes();

	TArray<AActor*> PlaneActors;
	TArray<UPlaneCut> PlaneCuts;
	if (FractureSettings->CommonSettings->FractureMode == EMeshFractureMode::PlaneCut)
	{
		ExtractPlaneCutsFromPlaneActors(SelectedMeshes, PlaneCuts, PlaneActors);
	}

	TArray<AActor*> SelectedActors = GetSelectedActors();

	FBox SelectedMeshBounds(ForceInit);
	for (AActor* Actor : SelectedActors)
	{
		UEditableMesh* Mesh = GetEditableMeshForActor(Actor, SelectedMeshes);
		FBox LocalBounds = Mesh->ComputeBoundingBox();
		SelectedMeshBounds += LocalBounds.TransformBy(Actor->ActorToWorld());
	}

	int32 RandomSeed = FMath::Rand();
	if( FractureSettings->CommonSettings->RandomSeed > -1 )
	{
		RandomSeed = FractureSettings->CommonSettings->RandomSeed;
	}

	for (AActor* SelectedActor : SelectedActors)
	{
		check(SelectedActor);

		if (FractureSettings->CommonSettings->FractureMode == EMeshFractureMode::PlaneCut)
		{
			if (IsPlaneActor(SelectedActor, PlaneActors))
				continue;

			FractureSettings->PlaneCutSettings->PlaneCuts.Empty();
			for (UPlaneCut Cut : PlaneCuts)
			{
				UPlaneCut LocalCut;
				// values need to be relative to the cut actor's transform
				LocalCut.Position = Cut.Position - SelectedActor->GetTransform().GetTranslation();
				LocalCut.Normal = Cut.Normal;
				FractureSettings->PlaneCutSettings->PlaneCuts.Push(LocalCut);
			}
		}

		TArray<UActorComponent*> PrimitiveComponents = SelectedActor->GetComponentsByClass(UPrimitiveComponent::StaticClass());
		for (UActorComponent* PrimitiveComponent : PrimitiveComponents)
		{
			const FTransform& ComponentTransform = CastChecked<UPrimitiveComponent>(PrimitiveComponent)->GetComponentTransform();
			if (UEditableMesh* EditableMesh = GetEditableMeshForComponent(PrimitiveComponent, SelectedMeshes))
			{
				// if we're shattering each mesh individually.
				if (!FractureSettings->CommonSettings->bGroupFracture)
				{
					SelectedMeshBounds = EditableMesh->ComputeBoundingBox();
					SelectedMeshBounds = SelectedMeshBounds.TransformBy(SelectedActor->ActorToWorld());
				}

				EditableMesh->StartModification(EMeshModificationType::Final, EMeshTopologyChange::TopologyChange);
				{
					FractureMesh(SelectedActor, MeshEditorMode, EditableMesh, ComponentTransform, RandomSeed, *FractureSettings, SelectedMeshBounds);

					PrimitiveComponent->MarkRenderDynamicDataDirty();
					PrimitiveComponent->MarkRenderStateDirty();
				}
				EditableMesh->EndModification();

				MeshEditorMode.TrackUndo(EditableMesh, EditableMesh->MakeUndo());
			}
		}

		if (!FractureSettings->CommonSettings->bGroupFracture)
		{
			++RandomSeed;
		}
	}

	UpdateExplodedView(MeshEditorMode, EViewResetType::RESET_ALL);
}

void UFractureMeshCommand::ExtractPlaneCutsFromPlaneActors(TArray<UEditableMesh*>& SelectedMeshes, TArray<UPlaneCut>& PlaneCuts, TArray<AActor*>& PlaneActors)
{
	const TArray<AActor*> SelectedActors = GetSelectedActors();

	for (AActor* Actor : SelectedActors)
	{
		if (Actor->GetName().StartsWith("Plane", ESearchCase::IgnoreCase))
		{
			UEditableMesh* CuttingMesh = GetEditableMeshForActor(Actor, SelectedMeshes);

			if (CuttingMesh)
			{
				FTransform PlaneTransform = Actor->GetTransform();
				UPlaneCut PlaneCutSettings;

				for (const auto PolygonID : CuttingMesh->GetMeshDescription()->Polygons().GetElementIDs())
				{
					PlaneCutSettings.Position = PlaneTransform.TransformPosition(CuttingMesh->ComputePolygonCenter(PolygonID));
					PlaneCutSettings.Normal = PlaneTransform.TransformVector(CuttingMesh->ComputePolygonNormal(PolygonID));
					PlaneCuts.Push(PlaneCutSettings);
					break;
				}
				PlaneActors.Push(Actor);
			}
		}
	}
}

bool UFractureMeshCommand::IsPlaneActor(const AActor* SelectedActor, TArray<AActor *>& PlaneActors)
{
	for (const AActor* Actor : PlaneActors)
	{
		if (SelectedActor == Actor)
			return true;
	}

	return false;
}


void UFractureMeshCommand::FractureMesh(AActor* OriginalActor, IMeshEditorModeEditingContract& MeshEditorMode, UEditableMesh* SourceMesh, const FTransform& Transform, int32 RandomSeed, const UMeshFractureSettings& FractureSettings, const FBox& Bounds)
{
	UFractureMesh* FractureIF = NewObject<UFractureMesh>(GetTransientPackage());

	const FString& Name = OriginalActor->GetActorLabel();

	// Try Get the GeometryCollectionComponent from the editable mesh
	UGeometryCollectionComponent* GeometryCollectionComponent = GetGeometryCollectionComponent(SourceMesh);
	TArray<FGeneratedFracturedChunk> GeneratedChunks;
	TArray<int32> DeletedChunks;

	FRandomStream RandomStream(RandomSeed);

	// if no GeometryCollectionComponent exists then create a Geometry Collection Actor
	if (GeometryCollectionComponent == nullptr)
	{
		if (RandomStream.GetFraction() <= FractureSettings.CommonSettings->ChanceToFracture)
		{
			// create new GeometryCollectionActor
			AGeometryCollectionActor* NewActor = CreateNewGeometryActor(Name, Transform, SourceMesh, true);

			FGeometryCollectionEdit GeometryCollectionEdit = NewActor->GetGeometryCollectionComponent()->EditRestCollection();
			UGeometryCollection* GeometryCollectionObject = GeometryCollectionEdit.GetRestCollection();
			TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollection = GeometryCollectionObject->GetGeometryCollection();
			check(GeometryCollectionObject);

			// add fracture chunks to this geometry collection
			FractureIF->FractureMesh(SourceMesh, Name, FractureSettings, -1, Transform, RandomSeed, GeometryCollectionObject, GeneratedChunks, DeletedChunks, Bounds, OriginalActor->GetActorLocation());

			check(DeletedChunks.Num() == 0);

			// recompute tangents for geometry collection
			FGeometryCollectionCreationParameters GeometryCollectionParameters(*GeometryCollection.Get(), false, true);

			for (FGeneratedFracturedChunk& GeneratedChunk : GeneratedChunks)
			{
				GeometryCollectionObject->AppendGeometry(*GeneratedChunk.GeometryCollectionObject, false);
				FractureIF->FixupHierarchy(0, GeometryCollectionObject, GeneratedChunk, Name);
			}

			// select the new actor in the editor
			GEditor->SelectActor(OriginalActor, false, true);
			GEditor->SelectActor(NewActor, true, true);

			if (FractureSettings.CommonSettings->DeleteSourceMesh)
			{
				RemoveActor(OriginalActor);
			}

			GeometryCollectionObject->InitializeMaterials();

			ensure(GeometryCollectionObject->GetGeometryCollection()->HasContiguousFaces());
			ensure(GeometryCollectionObject->GetGeometryCollection()->HasContiguousVertices());
		}
	}
	else if (GeometryCollectionComponent->GetSelectedBones().Num() > 0)
	{
		// scoped edit of collection
		FGeometryCollectionEdit GeometryCollectionEdit = GeometryCollectionComponent->EditRestCollection();
		UGeometryCollection* GeometryCollectionObject = GeometryCollectionEdit.GetRestCollection();
		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollection = GeometryCollectionObject->GetGeometryCollection();


		TArray<FTransform> Transforms;
		if (GeometryCollection)
		{
			GeometryCollectionAlgo::GlobalMatrices(GeometryCollection->Transform, GeometryCollection->Parent, Transforms);
		}

		AddAdditionalAttributesIfRequired(GeometryCollectionObject);
		AddSingleRootNodeIfRequired(GeometryCollectionObject);

		TArray<int32> TransformIndexToGeometryIndex;
		GeometryCollectionAlgo::BuildTransformGroupToGeometryGroupMap(*GeometryCollection, TransformIndexToGeometryIndex);

		FBox FractureBoundingBox(ForceInitToZero);;
		for (int32 FracturedChunkIndex : GeometryCollectionComponent->GetSelectedBones())
		{
			TArray<int32> LeafBones;
			FGeometryCollectionClusteringUtility::GetLeafBones(GeometryCollection.Get(), FracturedChunkIndex, LeafBones);
			for (int32 LeafBone : LeafBones)
			{
				int32 GeometryIndex = TransformIndexToGeometryIndex[LeafBone];
				const FBox &BoundingBox = GeometryCollection->BoundingBox[GeometryIndex];
				const FVector Location = Transforms[LeafBone].GetLocation();
				FractureBoundingBox += BoundingBox.ShiftBy( Location );
			}
		}


		FThreadSafeBool bFractureSuccessful(true);
		const TArray<int32>& SelectedBones = GeometryCollectionComponent->GetSelectedBones();
		ParallelFor(SelectedBones.Num(), [&](int32 Idx) {
			if (bFractureSuccessful || !FractureSettings.CommonSettings->bCancelOnBadGeo)
			{
				int32 FracturedChunkIndex = SelectedBones[Idx];
				if (RandomStream.GetFraction() <= FractureSettings.CommonSettings->ChanceToFracture)
				{
					bool bFractureGood = false;
					TArray<int32> LeafBones;
					FGeometryCollectionClusteringUtility::GetLeafBones(GeometryCollection.Get(), FracturedChunkIndex, LeafBones);
					for (int32 LeafBone : LeafBones)
					{
						int32 GeometryIndex = TransformIndexToGeometryIndex[LeafBone];
						const FVector Location = Transforms[LeafBone].GetLocation();

						if (FractureSettings.CommonSettings->bGroupFracture)
						{
							bFractureGood = FractureIF->FractureMesh(SourceMesh, Name, FractureSettings, LeafBone, Transform, RandomSeed, GeometryCollectionObject, GeneratedChunks, DeletedChunks, FractureBoundingBox, Location);
						}
						else
						{
							FBox BoundingBox = GeometryCollection->BoundingBox[GeometryIndex];
							BoundingBox = BoundingBox.ShiftBy(Location);

							bFractureGood = FractureIF->FractureMesh(SourceMesh, Name, FractureSettings, LeafBone, Transform, RandomSeed + Idx, GeometryCollectionObject, GeneratedChunks, DeletedChunks, BoundingBox, Location);
						}
						bFractureSuccessful = bFractureSuccessful & bFractureGood;
					}
				}
			}
		}, !FractureSettings.CommonSettings->bThreadedFracture);

		// If we're not checking, always pass.
		if (!FractureSettings.CommonSettings->bCancelOnBadGeo)
		{
			bFractureSuccessful = true;
		}

		if (bFractureSuccessful)
		{
			if (FractureSettings.CommonSettings->RetainUnfracturedMeshes)
			{
				// hide the parent chunk that has just been fractured into component chunks
				GeometryCollection.Get()->UpdateGeometryVisibility(DeletedChunks, false);
			}
			else
			{
				// Find geometry connected to this transform
				TArray<int32> GeometryIndices;
				const TManagedArray<int32>& GeometryToTransformIndex = GeometryCollection.Get()->TransformIndex;
				for (int i = 0; i < GeometryToTransformIndex.Num(); i++)
				{
					if (DeletedChunks.Contains(GeometryToTransformIndex[i]))
					{
						GeometryIndices.Add(i);
					}
				}

				// delete the parent chunk that has just been fractured into component chunks
				GeometryCollection.Get()->RemoveElements(FGeometryCollection::GeometryGroup, GeometryIndices);
			}

			// recompute tangents for geometry collection
			FGeometryCollectionCreationParameters GeometryCollectionParameters(*GeometryCollection.Get(), false, true);

			// add the new fracture chunks to the existing geometry collection
			for (FGeneratedFracturedChunk& GeneratedChunk : GeneratedChunks)
			{
				if (FractureSettings.CommonSettings->bHealHoles)
				{
					FGeometryCollection *ChunkGeometryCollection = GeneratedChunk.GeometryCollectionObject->GetGeometryCollection().Get();
					// try to fill holes
					TArray<TArray<TArray<int32>>> BoundaryVertexIndices;
					GeometryCollectionAlgo::FindOpenBoundaries(ChunkGeometryCollection, 1e-2, BoundaryVertexIndices);
					if (BoundaryVertexIndices.Num() > 0)
					{
						GeometryCollectionAlgo::TriangulateBoundaries(ChunkGeometryCollection, BoundaryVertexIndices);
					}
				}
				// add to collection
				GeometryCollectionObject->AppendGeometry(*GeneratedChunk.GeometryCollectionObject, false);
				FractureIF->FixupHierarchy(GeneratedChunk.FracturedChunkIndex, GeometryCollectionObject, GeneratedChunk, Name);
			}

			// rebuilds material sections
			GeometryCollectionObject->ReindexMaterialSections();

			if (2 <= GeometryCollection->NumElements(FGeometryCollection::GeometryGroup))
			{
		 			FGeometryCollectionProximityUtility::UpdateProximity(GeometryCollection.Get());
			}

			ensure(GeometryCollectionObject->GetGeometryCollection()->HasContiguousFaces());
			ensure(GeometryCollectionObject->GetGeometryCollection()->HasContiguousVertices());
		}
	}

	delete FractureIF;

}

#undef LOCTEXT_NAMESPACE
