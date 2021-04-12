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
		if (LocalCutSettings->ReferenceActor != nullptr) // so we update with the ref actor realtime
		{
			FTransform Transform(LocalCutSettings->ReferenceActor->GetActorTransform());
			PDI->DrawPoint(Transform.GetLocation(), FLinearColor::Green, 4.f, SDPG_Foreground);

			PDI->DrawLine(Transform.GetLocation(), Transform.GetLocation() + Transform.GetUnitAxis(EAxis::X) * 100.f, FLinearColor(255, 0, 0), SDPG_Foreground);
			PDI->DrawLine(Transform.GetLocation(), Transform.GetLocation() + Transform.GetUnitAxis(EAxis::Y) * 100.f, FLinearColor(0, 255, 0), SDPG_Foreground);

			PDI->DrawLine(Transform.GetLocation() + Transform.GetUnitAxis(EAxis::X) * 100.f, Transform.GetLocation() + Transform.GetUnitAxis(EAxis::X) * 100.f + Transform.GetUnitAxis(EAxis::Y) * 100.f, FLinearColor(255, 0, 0), SDPG_Foreground);
			PDI->DrawLine(Transform.GetLocation() + Transform.GetUnitAxis(EAxis::Y) * 100.f, Transform.GetLocation() + Transform.GetUnitAxis(EAxis::X) * 100.f + Transform.GetUnitAxis(EAxis::Y) * 100.f, FLinearColor(0, 255, 0), SDPG_Foreground);
		}
		else // draw from computed transforms
		{
			for (const FTransform& Transform : RenderCuttingPlanesTransforms)
			{
				PDI->DrawPoint(Transform.GetLocation(), FLinearColor::Green, 4.f, SDPG_Foreground);

				PDI->DrawLine(Transform.GetLocation(), Transform.GetLocation() + Transform.GetUnitAxis(EAxis::X) * RenderCuttingPlaneSize, FLinearColor(255, 0, 0), SDPG_Foreground);
				PDI->DrawLine(Transform.GetLocation(), Transform.GetLocation() + Transform.GetUnitAxis(EAxis::Y) * RenderCuttingPlaneSize, FLinearColor(0, 255, 0), SDPG_Foreground);

				PDI->DrawLine(Transform.GetLocation() + Transform.GetUnitAxis(EAxis::X) * RenderCuttingPlaneSize, Transform.GetLocation() + Transform.GetUnitAxis(EAxis::X) * RenderCuttingPlaneSize + Transform.GetUnitAxis(EAxis::Y) * RenderCuttingPlaneSize, FLinearColor(255, 0, 0), SDPG_Foreground);
				PDI->DrawLine(Transform.GetLocation() + Transform.GetUnitAxis(EAxis::Y) * RenderCuttingPlaneSize, Transform.GetLocation() + Transform.GetUnitAxis(EAxis::X) * RenderCuttingPlaneSize + Transform.GetUnitAxis(EAxis::Y) * RenderCuttingPlaneSize, FLinearColor(0, 255, 0), SDPG_Foreground);
			}
		}
	}
}

TArray<UObject*> UFractureToolPlaneCut::GetSettingsObjects() const
 {
	TArray<UObject*> Settings;
	Settings.Add(CutterSettings);
	Settings.Add(CollisionSettings);
	Settings.Add(PlaneCutSettings);
	return Settings;
}

void UFractureToolPlaneCut::FractureContextChanged()
{
	TArray<FFractureToolContext> FractureContexts = GetFractureToolContexts();

	RenderCuttingPlanesTransforms.Empty();

	RenderCuttingPlaneSize = FLT_MAX;
	for (FFractureToolContext& FractureContext : FractureContexts)
	{
		// Move the local bounds to the actor so we we'll draw in the correct location
		FractureContext.TransformBoundsToWorld();
		GenerateSliceTransforms(FractureContext, RenderCuttingPlanesTransforms);

		if (FractureContext.GetBounds().GetExtent().GetMax() < RenderCuttingPlaneSize)
		{
			RenderCuttingPlaneSize = FractureContext.GetBounds().GetExtent().GetMax();
		}
	}
}

int32 UFractureToolPlaneCut::ExecuteFracture(const FFractureToolContext& FractureContext)
{
	if (FractureContext.IsValid())
	{
		const UFracturePlaneCutSettings* LocalCutSettings = PlaneCutSettings;

		TArray<FPlane> CuttingPlanes;
		if (LocalCutSettings->ReferenceActor != nullptr)
		{
			FTransform Transform(LocalCutSettings->ReferenceActor->GetActorTransform());
			CuttingPlanes.Add(FPlane(Transform.GetLocation() - FractureContext.GetTransform().GetLocation(), Transform.GetUnitAxis(EAxis::Z)));
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
		return CutMultipleWithMultiplePlanes(CuttingPlanes, InternalSurfaceMaterials, *FractureContext.GetGeometryCollection(), FractureContext.GetSelection(), 0, 0);
	}

	return INDEX_NONE;
}

void UFractureToolPlaneCut::GenerateSliceTransforms(const FFractureToolContext& Context, TArray<FTransform>& CuttingPlaneTransforms)
{
	FRandomStream RandStream(Context.GetSeed());

	const FVector Extent(Context.GetBounds().Max - Context.GetBounds().Min);

	CuttingPlaneTransforms.Reserve(CuttingPlaneTransforms.Num() + PlaneCutSettings->NumberPlanarCuts);
	for (int32 ii = 0; ii < PlaneCutSettings->NumberPlanarCuts; ++ii)
	{
		FVector Position(Context.GetBounds().Min + FVector(RandStream.FRand(), RandStream.FRand(), RandStream.FRand()) * Extent);
		CuttingPlaneTransforms.Emplace(FTransform(FRotator(RandStream.FRand() * 360.0f, RandStream.FRand() * 360.0f, 0.0f), Position));
	}
}

#undef LOCTEXT_NAMESPACE