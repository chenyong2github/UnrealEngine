// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolPlaneCut.h"
#include "FractureEditorStyle.h"
#include "PlanarCut.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "FractureEditorModeToolkit.h"

#define LOCTEXT_NAMESPACE "FracturePlanar"

void UFracturePlaneCutSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (OwnerTool != nullptr)
	{
		OwnerTool->PostEditChangeProperty(PropertyChangedEvent);
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UFracturePlaneCutSettings::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (OwnerTool != nullptr)
	{
		OwnerTool->PostEditChangeChainProperty(PropertyChangedEvent);
	}
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}




UFractureToolPlaneCut::UFractureToolPlaneCut(const FObjectInitializer& ObjInit) 
	: Super(ObjInit) 
{
	PlaneCutSettings = NewObject<UFracturePlaneCutSettings>(GetTransientPackage(), UFracturePlaneCutSettings::StaticClass());
	PlaneCutSettings->OwnerTool = this;
}

FText UFractureToolPlaneCut::GetDisplayText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolPlaneCut", "PlaneCut")); 
}

FText UFractureToolPlaneCut::GetTooltipText() const 
{
	return FText(NSLOCTEXT("Fracture", "FractureToolPlaneCutTooltip", "PlaneCut Mesh")); 
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
	Settings.Add(GetMutableDefault<UFractureCommonSettings>());
	Settings.Add(GetMutableDefault<UFracturePlaneCutSettings>());
	return Settings;
}

void UFractureToolPlaneCut::FractureContextChanged()
{
	const UFractureCommonSettings* LocalCommonSettings = GetDefault<UFractureCommonSettings>();

	TArray<FFractureContext> FractureContexts;
	FFractureEditorModeToolkit::GetFractureContexts(FractureContexts);

	RenderCuttingPlanesTransforms.Empty();

	RenderCuttingPlaneSize = FLT_MAX;
	for (FFractureContext& FractureContext : FractureContexts)
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

void UFractureToolPlaneCut::ExecuteFracture(const FFractureContext& FractureContext)
{
	const UFractureCommonSettings* LocalCommonSettings = GetDefault<UFractureCommonSettings>();

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
			if (LocalCommonSettings->Amplitude > 0.0f)
			{
				NoiseSettings.Amplitude = LocalCommonSettings->Amplitude;
				NoiseSettings.Frequency = LocalCommonSettings->Frequency;
				NoiseSettings.Octaves = LocalCommonSettings->OctaveNumber;
				NoiseSettings.PointSpacing = LocalCommonSettings->SurfaceResolution;
				InternalSurfaceMaterials.NoiseSettings = NoiseSettings;
			}

			CutMultipleWithMultiplePlanes(CuttingPlanes, InternalSurfaceMaterials, *GeometryCollection, FractureContext.SelectedBones);
		}
	}
}

bool UFractureToolPlaneCut::CanExecuteFracture() const
{
	return FFractureEditorModeToolkit::IsLeafBoneSelected();
}

#if WITH_EDITOR
void UFractureToolPlaneCut::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	FractureContextChanged();
}
#endif


void UFractureToolPlaneCut::GenerateSliceTransforms(const FFractureContext& Context, TArray<FTransform>& CuttingPlaneTransforms)
{
	const UFracturePlaneCutSettings* LocalCutSettings = GetMutableDefault<UFracturePlaneCutSettings>();

	FRandomStream RandStream(Context.RandomSeed);

	const FVector Extent(Context.Bounds.Max - Context.Bounds.Min);

	CuttingPlaneTransforms.Reserve(CuttingPlaneTransforms.Num() + LocalCutSettings->NumberPlanarCuts);
	for (int32 ii = 0; ii < LocalCutSettings->NumberPlanarCuts; ++ii)
	{
		FVector Position(Context.Bounds.Min + FVector(RandStream.FRand(), RandStream.FRand(), RandStream.FRand()) * Extent);
		CuttingPlaneTransforms.Emplace(FTransform(FRotator(RandStream.FRand() * 360.0f, RandStream.FRand() * 360.0f, 0.0f), Position));
	}
}

#undef LOCTEXT_NAMESPACE