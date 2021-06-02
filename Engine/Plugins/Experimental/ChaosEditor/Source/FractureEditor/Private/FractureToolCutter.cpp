// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolCutter.h"

#include "FractureToolContext.h"
#include "InteractiveToolsContext.h"
#include "EditorModeManager.h"

#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"

#include "Voronoi/Voronoi.h"
#include "PlanarCut.h"

#define LOCTEXT_NAMESPACE "FractureToolCutter"


UFractureTransformGizmoSettings::UFractureTransformGizmoSettings(const FObjectInitializer& ObjInit) : Super(ObjInit)
{

}

void UFractureTransformGizmoSettings::ResetGizmo(bool bResetRotation)
{
	if (!TransformGizmo || !TransformProxy)
	{
		return;
	}
	if (AttachedCutter)
	{
		AttachedCutter->SetMandateGroupFracture(bUseGizmo);
	}
	if (!bUseGizmo || !AttachedCutter)
	{
		TransformGizmo->SetVisibility(false);
		return;
	}
	FBox CombinedBounds = AttachedCutter->GetCombinedBounds(AttachedCutter->GetFractureToolContexts());
	TransformGizmo->SetVisibility((bool)CombinedBounds.IsValid);
	if (CombinedBounds.IsValid)
	{
		if (bCenterOnSelection)
		{
			FTransform Transform = TransformProxy->GetTransform();
			Transform.SetTranslation(CombinedBounds.GetCenter());
			if (bResetRotation)
			{
				Transform.SetRotation(FQuat::Identity);
			}
			TransformGizmo->SetNewGizmoTransform(Transform);
		}
	}
}

void UFractureTransformGizmoSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UFractureTransformGizmoSettings, bUseGizmo))
	{
		if (AttachedCutter)
		{
			ResetGizmo();
		}
	}
}

void UFractureTransformGizmoSettings::TransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	if (bUseGizmo && AttachedCutter)
	{
		AttachedCutter->FractureContextChanged();
	}
}

void UFractureTransformGizmoSettings::Setup(UFractureToolCutterBase* Cutter)
{
	AttachedCutter = Cutter;
	UInteractiveToolsContext* Context = GLevelEditorModeTools().GetInteractiveToolsContext();
	if (ensure(Context && AttachedCutter))
	{
		UInteractiveGizmoManager* GizmoManager = Context->GizmoManager;
		TransformProxy = NewObject<UTransformProxy>(this);
		TransformGizmo = GizmoManager->CreateCustomTransformGizmo(ETransformGizmoSubElements::StandardTranslateRotate, this);
		TransformGizmo->SetActiveTarget(TransformProxy);
		TransformProxy->OnTransformChanged.AddUObject(this, &UFractureTransformGizmoSettings::TransformChanged);
		ResetGizmo();
	}
}

void UFractureTransformGizmoSettings::Shutdown()
{
	UInteractiveToolsContext* Context = GLevelEditorModeTools().GetInteractiveToolsContext();
	if (Context)
	{
		Context->GizmoManager->DestroyAllGizmosByOwner(this);
	}
}

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
	
	return true;
}

TArray<FFractureToolContext> UFractureToolCutterBase::GetFractureToolContexts() const
{
	TArray<FFractureToolContext> Contexts;

	// A context is gathered for each selected GeometryCollection component, or for each individual bone if Group Fracture is not used.
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);

	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		const UGeometryCollection* RestCollection = GeometryCollectionComponent->GetRestCollection();
		if (RestCollection && !RestCollection->IsPendingKill())
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
				FullSelection.SetSeed(CutterSettings->RandomSeed > -1 ? CutterSettings->RandomSeed : DefaultRandomSeed);

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
					FFractureToolContext& FractureContext = Contexts.Emplace_GetRef(GeometryCollectionComponent);

					TArray<int32> Selection;
					Selection.Add(Index);
					FractureContext.SetSelection(Selection);
					FractureContext.SetSeed(CutterSettings->RandomSeed > -1 ? CutterSettings->RandomSeed + Index : DefaultRandomSeed + Index);
					FractureContext.SetBounds(BoundsToBone[Index]);
				}
			}
		}
	}

	return Contexts;
}


FBox UFractureToolCutterBase::GetCombinedBounds(const TArray<FFractureToolContext>& Contexts) const
{
	FBox CombinedBounds(EForceInit::ForceInit);
	for (const FFractureToolContext& FractureContext : Contexts)
	{
		CombinedBounds += FractureContext.GetWorldBounds();
	}
	return CombinedBounds;
}


void UFractureToolCutterBase::UpdateDefaultRandomSeed()
{
	DefaultRandomSeed = FMath::Rand();
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
	UpdateDefaultRandomSeed();
	TArray<FFractureToolContext> FractureContexts = GetFractureToolContexts();

	VoronoiSites.Empty();
	CellMember.Empty();
	VoronoiEdges.Empty();

	for (FFractureToolContext& FractureContext : FractureContexts)
	{
		// Move the local bounds to the actor so we we'll draw in the correct location
		GenerateVoronoiSites(FractureContext, VoronoiSites);
		if (CutterSettings->bDrawDiagram)
		{
			GetVoronoiEdges(VoronoiSites, FractureContext.GetWorldBounds(), VoronoiEdges, CellMember);
		}
	}
}

int32 UFractureToolVoronoiCutterBase::ExecuteFracture(const FFractureToolContext& FractureContext)
{
	if (FractureContext.IsValid())
	{
		TArray<FVector> Sites;
		GenerateVoronoiSites(FractureContext, Sites);
		FBox VoronoiBounds = FractureContext.GetWorldBounds();
			
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

		// Proximity is invalidated.
		ClearProximity(FractureContext.GetGeometryCollection().Get());

		return CutMultipleWithPlanarCells(VoronoiPlanarCells, *(FractureContext.GetGeometryCollection()), FractureContext.GetSelection(), CutterSettings->Grout, CollisionSettings->PointSpacing, FractureContext.GetTransform());
	}

	return INDEX_NONE;
}

#undef LOCTEXT_NAMESPACE
