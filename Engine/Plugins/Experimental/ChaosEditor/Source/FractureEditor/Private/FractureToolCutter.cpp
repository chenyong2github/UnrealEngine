// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolCutter.h"

#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"

#include "Voronoi/Voronoi.h"
#include "PlanarCut.h"


UFractureToolCutterBase::UFractureToolCutterBase(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	CutterSettings = NewObject<UFractureCutterSettings>(GetTransientPackage(), UFractureCutterSettings::StaticClass());
	CutterSettings->OwnerTool = this;
}

bool UFractureToolCutterBase::CanExecute() const
{
	if (!IsGeometryCollectionSelected())
	{
		return false;
	}

	if (IsStaticMeshSelected())
	{
		return false;
	}

	return IsLeafBoneSelected();
}


void UFractureToolCutterBase::GetFractureContexts(TArray<FFractureToolContext>& FractureContexts) const
{
	FRandomStream RandomStream(CutterSettings->RandomSeed > -1 ? CutterSettings->RandomSeed : FMath::Rand());

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

		if (CutterSettings->bGroupFracture)
		{
			GenerateFractureToolContext(
				GeometryCollectionComponent->GetOwner(),
				GeometryCollectionComponent,
				FracturedGeometryCollection,
				FilteredBones,
				BoundsToBone,
				TransformToGeometryIndex,
				CutterSettings->RandomSeed,
				FractureContexts);
		}
		else
		{
			for (int32 BoneIndex : FilteredBones)
			{
				GenerateFractureToolContext(
					GeometryCollectionComponent->GetOwner(),
					GeometryCollectionComponent,
					FracturedGeometryCollection,
					{ BoneIndex },
					BoundsToBone,
					TransformToGeometryIndex,
					CutterSettings->RandomSeed + FractureContexts.Num(),
					FractureContexts);
			}
		}
		
	}
}


bool UFractureToolCutterBase::IsLeafBoneSelected()
{
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);
	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		const TArray<int32> SelectedBones = GeometryCollectionComponent->GetSelectedBones();

		if (SelectedBones.Num() > 0)
		{
			if (const UGeometryCollection* GCObject = GeometryCollectionComponent->GetRestCollection())
			{
				TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GCObject->GetGeometryCollection();
				if (const FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
				{
					const TManagedArray<TSet<int32>>& Children = GeometryCollection->GetAttribute<TSet<int32>>("Children", FGeometryCollection::TransformGroup);

					for (int32 BoneIndex : SelectedBones)
					{
						if (Children[BoneIndex].Num() == 0)
						{
							return true;
						}
					}
				}
			}
		}
	}
	return false;
}

TArray<int32> UFractureToolCutterBase::FilterBones(const TArray<int32>& SelectedBonesOriginal, const FGeometryCollection* const GeometryCollection) const
{
	FRandomStream RandomStream(CutterSettings->RandomSeed > -1 ? CutterSettings->RandomSeed : FMath::Rand());

	// Keep only leaf nodes and implement skip probability
	const TManagedArray<TSet<int32>>& Children = GeometryCollection->GetAttribute<TSet<int32>>("Children", FGeometryCollection::TransformGroup);

	TArray<int32> SelectedBones;
	SelectedBones.Reserve(SelectedBonesOriginal.Num());
	for (int32 BoneIndex : SelectedBonesOriginal)
	{
		if (Children[BoneIndex].Num() == 0 && RandomStream.FRand() < CutterSettings->ChanceToFracture)
		{
			SelectedBones.Add(BoneIndex);
		}
	}

	return SelectedBones;
}



UFractureToolVoronoiCutterBase::UFractureToolVoronoiCutterBase(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	for (int32 ii = 0; ii < 100; ++ii)
	{
		Colors.Emplace(FMath::FRand(), FMath::FRand(), FMath::FRand());;
	}
}

void UFractureToolVoronoiCutterBase::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	if (CutterSettings->bDrawSites)
	{
		for (const FVector& Site : VoronoiSites)
		{
			PDI->DrawPoint(Site, FLinearColor::Green, 4.f, SDPG_Foreground);
		}
	}

	if (CutterSettings->bDrawDiagram)
	{
		PDI->AddReserveLines(SDPG_Foreground, VoronoiEdges.Num(), false, false);
		for (int32 ii = 0, ni = VoronoiEdges.Num(); ii < ni; ++ii)
		{
			PDI->DrawLine(VoronoiEdges[ii].Get<0>(), VoronoiEdges[ii].Get<1>(), Colors[CellMember[ii] % 100], SDPG_Foreground);
		}
	}
}

void UFractureToolVoronoiCutterBase::FractureContextChanged()
{
	TArray<FFractureToolContext> FractureContexts;
	GetFractureContexts(FractureContexts);

	VoronoiSites.Empty();
	CellMember.Empty();
	VoronoiEdges.Empty();;

	for (FFractureToolContext& FractureContext : FractureContexts)
	{
		// Move the local bounds to the actor so we we'll draw in the correct location
		FractureContext.Bounds = FractureContext.Bounds.TransformBy(FractureContext.Transform);
		GenerateVoronoiSites(FractureContext, VoronoiSites);
		if (CutterSettings->bDrawDiagram)
		{
			GetVoronoiEdges(VoronoiSites, FractureContext.Bounds, VoronoiEdges, CellMember);
		}
	}
}

void UFractureToolVoronoiCutterBase::ExecuteFracture(const FFractureToolContext& FractureContext)
{
	if (FractureContext.FracturedGeometryCollection != nullptr)
	{
		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = FractureContext.FracturedGeometryCollection->GetGeometryCollection();
		if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
		{
			TArray<FVector> Sites;
			GenerateVoronoiSites(FractureContext, Sites);
			FBox VoronoiBounds = FractureContext.Bounds;
			if (CutterSettings->Amplitude > 0.0f)
			{
				// expand bounds to make sure noise-perturbed voronoi cells still contain the whole input mesh
				VoronoiBounds = VoronoiBounds.ExpandBy(CutterSettings->Amplitude);
			}
			FVoronoiDiagram Voronoi(Sites, VoronoiBounds, .1f);

			FPlanarCells VoronoiPlanarCells = FPlanarCells(Sites, Voronoi);

			FNoiseSettings NoiseSettings;
			if (CutterSettings->Amplitude > 0.0f)
			{
				NoiseSettings.Amplitude = CutterSettings->Amplitude;
				NoiseSettings.Frequency = CutterSettings->Frequency;
				NoiseSettings.Octaves = CutterSettings->OctaveNumber;
				NoiseSettings.PointSpacing = CutterSettings->SurfaceResolution;
				VoronoiPlanarCells.InternalSurfaceMaterials.NoiseSettings = NoiseSettings;
			}

			CutMultipleWithPlanarCells(VoronoiPlanarCells, *GeometryCollection, FractureContext.SelectedBones);
		}
	}
}
