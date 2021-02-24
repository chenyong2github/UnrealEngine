// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolCutter.h"

#include "FractureToolContext.h"

#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"

#include "Voronoi/Voronoi.h"
#include "PlanarCut.h"

#define LOCTEXT_NAMESPACE "FractureToolCutter"


UFractureToolCutterBase::UFractureToolCutterBase(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	CutterSettings = NewObject<UFractureCutterSettings>(GetTransientPackage(), UFractureCutterSettings::StaticClass());
	CutterSettings->OwnerTool = this;
	CollisionSettings = NewObject<UFractureCollisionSettings>(GetTransientPackage(), UFractureCollisionSettings::StaticClass());
	CollisionSettings->OwnerTool = this;
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

	return true;
}

TArray<FFractureToolContext> UFractureToolCutterBase::GetFractureToolContexts() const
{
	TArray<FFractureToolContext> Contexts;

	FRandomStream RandomStream(CutterSettings->RandomSeed > -1 ? CutterSettings->RandomSeed : FMath::Rand());

	// A context is gathered for each selected GeometryCollection component, or for each individual bone if Group Fracture is not used.
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);

	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		// Generate a context for each selected node
		FFractureToolContext FullSelection(GeometryCollectionComponent);
		FullSelection.ConvertSelectionToRigidNodes();
		
		// Update global transforms and bounds		
		const TManagedArray<FTransform>& Transform = FullSelection.GetGeometryCollection()->GetAttribute<FTransform>("Transform", FGeometryCollection::TransformGroup);
		const TManagedArray<int32>& TransformToGeometryIndex = FullSelection.GetGeometryCollection()->GetAttribute<int32>("TransformToGeometryIndex", FGeometryCollection::TransformGroup);
		const TManagedArray<FBox>& BoundingBoxes = FullSelection.GetGeometryCollection()->GetAttribute<FBox>("BoundingBox", FGeometryCollection::GeometryGroup);

		TArray<FTransform> Transforms;
		GeometryCollectionAlgo::GlobalMatrices(Transform, FullSelection.GetGeometryCollection()->Parent, Transforms);

		TMap<int32, FBox> BoundsToBone;
		int32 TransformCount = Transform.Num();
		for (int32 Index = 0; Index < TransformCount; ++Index)
		{
			if (TransformToGeometryIndex[Index] > INDEX_NONE)
			{
				BoundsToBone.Add(Index, BoundingBoxes[TransformToGeometryIndex[Index]].TransformBy(Transforms[Index]));
			}
		}

		if (CutterSettings->bGroupFracture)
		{
			FullSelection.SetSeed(CutterSettings->RandomSeed > -1 ? CutterSettings->RandomSeed : FMath::Rand());

			FBox Bounds(ForceInit);
			for (int32 BoneIndex : FullSelection.GetSelection())
			{
				if (TransformToGeometryIndex[BoneIndex] > INDEX_NONE)
				{
					Bounds += BoundsToBone[BoneIndex];
				}
			}
			FullSelection.SetBounds(Bounds);

			Contexts.Add(FullSelection);
		}
		else
		{
			// Generate a context for each selected node
			for (int32 Index : FullSelection.GetSelection())
			{
				Contexts.Emplace(GeometryCollectionComponent);
				FFractureToolContext& FractureContext = Contexts.Last();

				FractureContext.SetSeed(CutterSettings->RandomSeed > -1 ? CutterSettings->RandomSeed + Index : FMath::Rand());
				FractureContext.SetBounds(BoundsToBone[Index]);
			}
		}
	}

	return Contexts;
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
	TArray<FFractureToolContext> FractureContexts = GetFractureToolContexts();

	VoronoiSites.Empty();
	CellMember.Empty();
	VoronoiEdges.Empty();;

	for (FFractureToolContext& FractureContext : FractureContexts)
	{
		// Move the local bounds to the actor so we we'll draw in the correct location
		FractureContext.TransformBoundsToWorld();
		GenerateVoronoiSites(FractureContext, VoronoiSites);
		if (CutterSettings->bDrawDiagram)
		{
			GetVoronoiEdges(VoronoiSites, FractureContext.GetBounds(), VoronoiEdges, CellMember);
		}
	}
}

int32 UFractureToolVoronoiCutterBase::ExecuteFracture(const FFractureToolContext& FractureContext)
{
	if (FractureContext.IsValid())
	{
		TArray<FVector> Sites;
		GenerateVoronoiSites(FractureContext, Sites);
		FBox VoronoiBounds = FractureContext.GetBounds();
			
		// expand bounds to make sure noise-perturbed voronoi cells still contain the whole input mesh
		VoronoiBounds = VoronoiBounds.ExpandBy(CutterSettings->Amplitude + CutterSettings->Grout);
			
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

		return CutMultipleWithPlanarCells(VoronoiPlanarCells, *(FractureContext.GetGeometryCollection()), FractureContext.GetSelection(), 0, 0);
	}

	return INDEX_NONE;
}

#undef LOCTEXT_NAMESPACE
