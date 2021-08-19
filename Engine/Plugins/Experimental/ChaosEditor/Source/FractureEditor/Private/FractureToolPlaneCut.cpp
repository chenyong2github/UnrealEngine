// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolPlaneCut.h"
#include "FractureEditorStyle.h"
#include "PlanarCut.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "FractureEditorModeToolkit.h"
#include "FractureToolContext.h"

#define LOCTEXT_NAMESPACE "FracturePlanar"


UFractureToolPlaneCut::UFractureToolPlaneCut(const FObjectInitializer& ObjInit) 
	: Super(ObjInit) 
{
	PlaneCutSettings = NewObject<UFracturePlaneCutSettings>(GetTransientPackage(), UFracturePlaneCutSettings::StaticClass());
	PlaneCutSettings->OwnerTool = this;

	GizmoSettings = NewObject<UFractureTransformGizmoSettings>(GetTransientPackage(), UFractureTransformGizmoSettings::StaticClass());
	GizmoSettings->OwnerTool = this;
}


void UFractureToolPlaneCut::Setup()
{
	GizmoSettings->Setup(this);
}


void UFractureToolPlaneCut::Shutdown()
{
	GizmoSettings->Shutdown();
}


FText UFractureToolPlaneCut::GetDisplayText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolPlaneCut", "Plane Cut Fracture")); 
}

FText UFractureToolPlaneCut::GetTooltipText() const 
{
	return FText(NSLOCTEXT("Fracture", "FractureToolPlaneCutTooltip", "Planar fracture can be used to make cuts along a plane in your Geometry Collection. You can apply noise to planar cuts for more organic results.  Click the Fracture Button to commit the fracture to the geometry collection."));
}

FSlateIcon UFractureToolPlaneCut::GetToolIcon() const 
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.Planar");
}

void UFractureToolPlaneCut::RegisterUICommand( FFractureEditorCommands* BindingContext ) 
{
	UI_COMMAND_EXT( BindingContext, UICommandInfo, "Planar", "Planar", "Planar Voronoi Fracture", EUserInterfaceActionType::ToggleButton, FInputChord() );
	BindingContext->Planar = UICommandInfo;
}

void UFractureToolPlaneCut::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	const UFracturePlaneCutSettings* LocalCutSettings = PlaneCutSettings;
	if (CutterSettings->bDrawDiagram)
	{
		// Draw a point centered at plane origin, and a square on the plane around it.
		auto DrawPlane = [&PDI](const FTransform& Transform, float PlaneSize)
		{
			FVector Center = Transform.GetLocation();
			FVector X = (PlaneSize * .5) * Transform.GetUnitAxis(EAxis::X);
			FVector Y = (PlaneSize * .5) * Transform.GetUnitAxis(EAxis::Y);
			FVector Corners[4]
			{
				Center - X - Y,
				Center + X - Y,
				Center + X + Y,
				Center - X + Y
			};
			PDI->DrawPoint(Center, FLinearColor::Green, 4.f, SDPG_Foreground);
			PDI->DrawLine(Corners[0], Corners[1], FLinearColor(255, 0, 0), SDPG_Foreground);
			PDI->DrawLine(Corners[1], Corners[2], FLinearColor(0, 255, 0), SDPG_Foreground);
			PDI->DrawLine(Corners[2], Corners[3], FLinearColor(255, 0, 0), SDPG_Foreground);
			PDI->DrawLine(Corners[3], Corners[0], FLinearColor(0, 255, 0), SDPG_Foreground);
		};

		if (GizmoSettings->IsGizmoEnabled())
		{
			const FTransform& Transform = GizmoSettings->GetTransform();
			DrawPlane(Transform, 100.f);
		}
		else // draw from computed transforms
		{
			for (const FTransform& Transform : RenderCuttingPlanesTransforms)
			{
				DrawPlane(Transform, RenderCuttingPlaneSize);
			}
		}
	}
}

TArray<UObject*> UFractureToolPlaneCut::GetSettingsObjects() const
 {
	TArray<UObject*> Settings;
	Settings.Add(CutterSettings);
	Settings.Add(GizmoSettings);
	Settings.Add(CollisionSettings);
	Settings.Add(PlaneCutSettings);
	return Settings;
}

void UFractureToolPlaneCut::FractureContextChanged()
{
	UpdateDefaultRandomSeed();
	TArray<FFractureToolContext> FractureContexts = GetFractureToolContexts();

	RenderCuttingPlanesTransforms.Empty();

	RenderCuttingPlaneSize = FLT_MAX;
	for (FFractureToolContext& FractureContext : FractureContexts)
	{
		// Move the local bounds to the actor so we we'll draw in the correct location
		FBox Bounds = FractureContext.GetWorldBounds();
		GenerateSliceTransforms(FractureContext, RenderCuttingPlanesTransforms);

		if (Bounds.GetExtent().GetMax() < RenderCuttingPlaneSize)
		{
			RenderCuttingPlaneSize = Bounds.GetExtent().GetMax();
		}
	}
}

int32 UFractureToolPlaneCut::ExecuteFracture(const FFractureToolContext& FractureContext)
{
	if (FractureContext.IsValid())
	{
		const UFracturePlaneCutSettings* LocalCutSettings = PlaneCutSettings;

		TArray<FPlane> CuttingPlanes;
		if (GizmoSettings->IsGizmoEnabled())
		{
			FTransform Transform = GizmoSettings->GetTransform();
			CuttingPlanes.Add(FPlane(Transform.GetLocation(), Transform.GetUnitAxis(EAxis::Z)));
		}
		else
		{
			TArray<FTransform> CuttingPlaneTransforms;
			GenerateSliceTransforms(FractureContext, CuttingPlaneTransforms);
			for (const FTransform& Transform : CuttingPlaneTransforms)
			{
				CuttingPlanes.Add(FPlane(Transform.GetLocation(), Transform.GetUnitAxis(EAxis::Z)));
			}
		}

		FInternalSurfaceMaterials InternalSurfaceMaterials;
		FNoiseSettings NoiseSettings;
		if (CutterSettings->Amplitude > 0.0f)
		{
			NoiseSettings.Amplitude = CutterSettings->Amplitude;
			NoiseSettings.Frequency = CutterSettings->Frequency;
			NoiseSettings.Octaves = CutterSettings->OctaveNumber;
			NoiseSettings.PointSpacing = CutterSettings->SurfaceResolution;
			InternalSurfaceMaterials.NoiseSettings = NoiseSettings;
		}

		// Proximity is invalidated.
		ClearProximity(FractureContext.GetGeometryCollection().Get());

		return CutMultipleWithMultiplePlanes(CuttingPlanes, InternalSurfaceMaterials, *FractureContext.GetGeometryCollection(), FractureContext.GetSelection(), CutterSettings->Grout, CollisionSettings->PointSpacing, FractureContext.GetTransform());
	}

	return INDEX_NONE;
}

void UFractureToolPlaneCut::GenerateSliceTransforms(const FFractureToolContext& Context, TArray<FTransform>& CuttingPlaneTransforms)
{
	FRandomStream RandStream(Context.GetSeed());

	FBox Bounds = Context.GetWorldBounds();
	const FVector Extent(Bounds.Max - Bounds.Min);

	CuttingPlaneTransforms.Reserve(CuttingPlaneTransforms.Num() + PlaneCutSettings->NumberPlanarCuts);
	for (int32 ii = 0; ii < PlaneCutSettings->NumberPlanarCuts; ++ii)
	{
		FVector Position(Bounds.Min + FVector(RandStream.FRand(), RandStream.FRand(), RandStream.FRand()) * Extent);
		CuttingPlaneTransforms.Emplace(FTransform(FRotator(RandStream.FRand() * 360.0f, RandStream.FRand() * 360.0f, 0.0f), Position));
	}
}

void UFractureToolPlaneCut::SelectedBonesChanged()
{
	GizmoSettings->ResetGizmo();
	Super::SelectedBonesChanged();
}

#undef LOCTEXT_NAMESPACE