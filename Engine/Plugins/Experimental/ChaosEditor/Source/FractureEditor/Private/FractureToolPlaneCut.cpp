// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolPlaneCut.h"
#include "FractureEditorStyle.h"
#include "PlanarCut.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "FractureEditorModeToolkit.h"

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
	const UFracturePlaneCutSettings* LocalCutSettings = GetMutableDefault<UFracturePlaneCutSettings>();

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

TArray<UObject*> UFractureToolPlaneCut::GetSettingsObjects() const
 {
	TArray<UObject*> Settings;
	Settings.Add(CutterSettings);
	Settings.Add(PlaneCutSettings);
	return Settings;
}

void UFractureToolPlaneCut::FractureContextChanged()
{
	TArray<FFractureToolContext> FractureContexts;
	GetFractureContexts(FractureContexts);

	RenderCuttingPlanesTransforms.Empty();

	RenderCuttingPlaneSize = FLT_MAX;
	for (FFractureToolContext& FractureContext : FractureContexts)
	{
		// Move the local bounds to the actor so we weill draw in the correct location
		FractureContext.Bounds = FractureContext.Bounds.TransformBy(FractureContext.Transform);
		GenerateSliceTransforms(FractureContext, RenderCuttingPlanesTransforms);

		if (FractureContext.Bounds.GetExtent().GetMax() < RenderCuttingPlaneSize)
		{
			RenderCuttingPlaneSize = FractureContext.Bounds.GetExtent().GetMax();
		}
	}
}

void UFractureToolPlaneCut::ExecuteFracture(const FFractureToolContext& FractureContext)
{
	if (FractureContext.FracturedGeometryCollection != nullptr)
	{
		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = FractureContext.FracturedGeometryCollection->GetGeometryCollection();
		if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
		{
			const UFracturePlaneCutSettings* LocalCutSettings = GetMutableDefault<UFracturePlaneCutSettings>();

			TArray<FPlane> CuttingPlanes;
			if (LocalCutSettings->ReferenceActor != nullptr)
			{
				FTransform Transform(LocalCutSettings->ReferenceActor->GetActorTransform());
				CuttingPlanes.Add(FPlane(Transform.GetLocation() - FractureContext.Transform.GetLocation(), Transform.GetUnitAxis(EAxis::Z)));
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

			CutMultipleWithMultiplePlanes(CuttingPlanes, InternalSurfaceMaterials, *GeometryCollection, FractureContext.SelectedBones);
		}
	}
}

void UFractureToolPlaneCut::GenerateSliceTransforms(const FFractureToolContext& Context, TArray<FTransform>& CuttingPlaneTransforms)
{
	FRandomStream RandStream(Context.RandomSeed);

	const FVector Extent(Context.Bounds.Max - Context.Bounds.Min);

	CuttingPlaneTransforms.Reserve(CuttingPlaneTransforms.Num() + PlaneCutSettings->NumberPlanarCuts);
	for (int32 ii = 0; ii < PlaneCutSettings->NumberPlanarCuts; ++ii)
	{
		FVector Position(Context.Bounds.Min + FVector(RandStream.FRand(), RandStream.FRand(), RandStream.FRand()) * Extent);
		CuttingPlaneTransforms.Emplace(FTransform(FRotator(RandStream.FRand() * 360.0f, RandStream.FRand() * 360.0f, 0.0f), Position));
	}
}

#undef LOCTEXT_NAMESPACE