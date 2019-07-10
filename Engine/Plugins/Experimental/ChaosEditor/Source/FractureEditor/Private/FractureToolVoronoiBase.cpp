// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "FractureToolVoronoiBase.h"

#include "Editor.h"
#include "Engine/Selection.h"
#include "FractureEditorStyle.h"
#include "GameFramework/Actor.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "PlanarCut.h"
#include "SceneManagement.h"
#include "Voronoi/Voronoi.h"
#include "FractureEditorModeToolkit.h"


UFractureToolVoronoiBase::UFractureToolVoronoiBase(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	for (int32 ii = 0; ii < 100; ++ii)
	{
		Colors.Emplace(FMath::FRand() , FMath::FRand() , FMath::FRand() );;
	}
}

UFractureToolVoronoiBase::~UFractureToolVoronoiBase()
{
}

void UFractureToolVoronoiBase::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	// TODO: move all this to UFractureTool for reuse
	FractureContextChanged();

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}

void UFractureToolVoronoiBase::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	const UFractureCommonSettings* LocalCommonSettings = GetDefault<UFractureCommonSettings>();

	if (LocalCommonSettings->bDrawSites)
	{
		for (const FVector& Site : VoronoiSites)
		{
			PDI->DrawPoint(Site, FLinearColor::Green, 4.f, SDPG_Foreground);
		}
	}

	if (LocalCommonSettings->bDrawDiagram)
	{
		PDI->AddReserveLines(SDPG_Foreground, VoronoiEdges.Num(), false, false);
		for (int32 ii = 0, ni = VoronoiEdges.Num(); ii < ni; ++ii)
		{
			PDI->DrawLine(VoronoiEdges[ii].Get<0>(), VoronoiEdges[ii].Get<1>(), Colors[CellMember[ii] % 100], SDPG_Foreground);
		}
	}
}

void UFractureToolVoronoiBase::FractureContextChanged()
{
	const UFractureCommonSettings* LocalCommonSettings = GetDefault<UFractureCommonSettings>();

	TArray<FFractureContext> FractureContexts;
	FFractureEditorModeToolkit::GetFractureContexts(FractureContexts);

	VoronoiSites.Empty();
	CellMember.Empty();
	VoronoiEdges.Empty();;

	for (FFractureContext& FractureContext : FractureContexts)
	{
		// Move the local bounds to the actor so we weill draw in the correct location
		FractureContext.Bounds = FractureContext.Bounds.TransformBy(FractureContext.Transform);
		GenerateVoronoiSites(FractureContext, VoronoiSites);
		if (LocalCommonSettings->bDrawDiagram)
		{
			GetVoronoiEdges(VoronoiSites, FractureContext.Bounds, VoronoiEdges, CellMember);
		}
	}
}

void UFractureToolVoronoiBase::ExecuteFracture(const FFractureContext& FractureContext)
{
	if (FractureContext.FracturedGeometryCollection != nullptr)
	{
		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = FractureContext.FracturedGeometryCollection->GetGeometryCollection();
		if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
		{
			TArray<FVector> Sites;
			GenerateVoronoiSites(FractureContext, Sites);
			FVoronoiDiagram Voronoi(Sites, FractureContext.Bounds, .1f);

			const UFractureCommonSettings* LocalCommonSettings = GetDefault<UFractureCommonSettings>();

			FPlanarCells VoronoiPlanarCells = FPlanarCells(Sites, Voronoi);

			FNoiseSettings NoiseSettings;
			if (LocalCommonSettings->Amplitude > 0.0f)
			{
				NoiseSettings.Amplitude = LocalCommonSettings->Amplitude;
				NoiseSettings.Frequency = LocalCommonSettings->Frequency;
				NoiseSettings.Octaves = LocalCommonSettings->OctaveNumber;
				NoiseSettings.PointSpacing = LocalCommonSettings->SurfaceResolution;
				VoronoiPlanarCells.InternalSurfaceMaterials.NoiseSettings = NoiseSettings;
			}

			CutMultipleWithPlanarCells(VoronoiPlanarCells, *GeometryCollection, FractureContext.SelectedBones);
		}
	}
}
