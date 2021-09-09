// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolCutter.h"

#include "FractureTool.h" // for LogFractureTool
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
		if (bCenterOnSelection && !GIsTransacting)
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

void UFractureCutterSettings::TransferNoiseSettings(FNoiseSettings& NoiseSettingsOut)
{
	NoiseSettingsOut.Amplitude = Amplitude;
	NoiseSettingsOut.Frequency = Frequency;
	NoiseSettingsOut.Lacunarity = Lacunarity;
	NoiseSettingsOut.Persistence = Persistence;
	NoiseSettingsOut.Octaves = OctaveNumber;
	NoiseSettingsOut.PointSpacing = SurfaceResolution;
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
		if (IsValid(RestCollection))
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
		EnumerateVisualizationMapping(SitesMappings, VoronoiSites.Num(), [&](int32 Idx, FVector ExplodedVector)
		{
			PDI->DrawPoint(VoronoiSites[Idx] + ExplodedVector, FLinearColor::Green, 4.f, SDPG_Foreground);
		});
	}

	if (CutterSettings->bDrawDiagram)
	{
		PDI->AddReserveLines(SDPG_Foreground, VoronoiEdges.Num(), false, false);
		EnumerateVisualizationMapping(EdgesMappings, VoronoiEdges.Num(), [&](int32 Idx, FVector ExplodedVector)
		{
			PDI->DrawLine(VoronoiEdges[Idx].Get<0>() + ExplodedVector,
				VoronoiEdges[Idx].Get<1>() + ExplodedVector, Colors[CellMember[Idx] % 100], SDPG_Foreground);
		});
	}
}

void UFractureToolVoronoiCutterBase::FractureContextChanged()
{
	UpdateDefaultRandomSeed();
	TArray<FFractureToolContext> FractureContexts = GetFractureToolContexts();

	ClearVisualizations();

	int32 MaxSitesToShowEdges = 100000; // computing all the voronoi diagrams can make the program non-responsive above this
	int32 MaxSitesToShowSites = 1000000; // PDI struggles to render the site positions above this
	bool bEstAboveMaxSites = false;

	for (FFractureToolContext& FractureContext : FractureContexts)
	{
		if (!FractureContext.GetBounds().IsValid) // skip contexts w/ invalid bounds
		{
			continue;
		}
		int32 CollectionIdx = VisualizedCollections.Emplace(FractureContext.GetGeometryCollectionComponent());
		int32 BoneIdx = FractureContext.GetSelection().Num() == 1 ? FractureContext.GetSelection()[0] : INDEX_NONE;
		SitesMappings.AddMapping(CollectionIdx, BoneIdx, VoronoiSites.Num());
		EdgesMappings.AddMapping(CollectionIdx, BoneIdx, VoronoiEdges.Num());

		// Generate voronoi diagrams and cache visualization info
		TArray<FVector> LocalVoronoiSites;
		GenerateVoronoiSites(FractureContext, LocalVoronoiSites);
		// if diagram(s) become too large, skip the visualization
		if (LocalVoronoiSites.Num() * FractureContexts.Num() > MaxSitesToShowSites || VoronoiSites.Num() + LocalVoronoiSites.Num() > MaxSitesToShowSites)
		{
			UE_LOG(LogFractureTool, Warning, TEXT("Voronoi diagram(s) number of sites too large; will not display Voronoi diagram sites"));
			ClearVisualizations();
			break;
		}
		VoronoiSites.Append(LocalVoronoiSites);
		if (bEstAboveMaxSites || LocalVoronoiSites.Num() * FractureContexts.Num() > MaxSitesToShowEdges || VoronoiSites.Num() > MaxSitesToShowEdges)
		{
			UE_LOG(LogFractureTool, Warning, TEXT("Voronoi diagram(s) number of sites too large; will not display Voronoi diagram edges"));
			VoronoiEdges.Empty();
			CellMember.Empty();
			EdgesMappings.Empty();
			bEstAboveMaxSites = true;
		}
		else
		{
			FBox VoronoiBounds = GetVoronoiBounds(FractureContext, LocalVoronoiSites);
			if (CutterSettings->bDrawDiagram)
			{
				GetVoronoiEdges(LocalVoronoiSites, VoronoiBounds, VoronoiEdges, CellMember);
			}
		}
	}
}

int32 UFractureToolVoronoiCutterBase::ExecuteFracture(const FFractureToolContext& FractureContext)
{
	if (FractureContext.IsValid())
	{
		TArray<FVector> Sites;
		GenerateVoronoiSites(FractureContext, Sites);
		FBox VoronoiBounds = GetVoronoiBounds(FractureContext, Sites);
			
		FVoronoiDiagram Voronoi(Sites, VoronoiBounds, .1f);

		FPlanarCells VoronoiPlanarCells = FPlanarCells(Sites, Voronoi);

		FNoiseSettings NoiseSettings;
		if (CutterSettings->Amplitude > 0.0f)
		{
			CutterSettings->TransferNoiseSettings(NoiseSettings);
			VoronoiPlanarCells.InternalSurfaceMaterials.NoiseSettings = NoiseSettings;
		}

		// Proximity is invalidated.
		ClearProximity(FractureContext.GetGeometryCollection().Get());

		return CutMultipleWithPlanarCells(VoronoiPlanarCells, *(FractureContext.GetGeometryCollection()), FractureContext.GetSelection(), CutterSettings->Grout, CollisionSettings->PointSpacing, FractureContext.GetTransform());
	}

	return INDEX_NONE;
}

FBox UFractureToolVoronoiCutterBase::GetVoronoiBounds(const FFractureToolContext& FractureContext, const TArray<FVector>& Sites) const
{
	FBox VoronoiBounds = FractureContext.GetWorldBounds(); 
	if (Sites.Num() > 0)
	{
		VoronoiBounds += FBox(Sites);
	}
	
	return VoronoiBounds.ExpandBy(CutterSettings->GetMaxVertexMovement() + KINDA_SMALL_NUMBER);
}

#undef LOCTEXT_NAMESPACE
