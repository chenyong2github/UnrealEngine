// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FractureToolSlice.h"

#include "Editor.h"
#include "Engine/Selection.h"
#include "FractureTool.h"
#include "FractureEditorStyle.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "PlanarCut.h"
#include "FractureEditorModeToolkit.h"

#define LOCTEXT_NAMESPACE "FractureSlice"

void UFractureSliceSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (OwnerTool != nullptr)
	{
		OwnerTool->PostEditChangeProperty(PropertyChangedEvent);
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UFractureSliceSettings::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (OwnerTool != nullptr)
	{
		OwnerTool->PostEditChangeChainProperty(PropertyChangedEvent);
	}
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}


UFractureToolSlice::UFractureToolSlice(const FObjectInitializer& ObjInit) 
	: Super(ObjInit) 
{
	SliceSettings = NewObject<UFractureSliceSettings>(GetTransientPackage(), UFractureSliceSettings::StaticClass());
	SliceSettings->OwnerTool = this;
}

FText UFractureToolSlice::GetDisplayText() const
{ 
	return FText(NSLOCTEXT("Fracture", "FractureToolSlice", "Slice")); 
}

FText UFractureToolSlice::GetTooltipText() const 
{ 
	return FText(NSLOCTEXT("Fracture", "FractureToolSliceTooltip", "Uniformly Slice Mesh")); 
}

FSlateIcon UFractureToolSlice::GetToolIcon() const 
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.Slice");
}

void UFractureToolSlice::RegisterUICommand( FFractureEditorCommands* BindingContext ) 
{
	UI_COMMAND_EXT( BindingContext, UICommandInfo, "Slice", "Slice", "Slice Voronoi Fracture", EUserInterfaceActionType::ToggleButton, FInputChord() );
	BindingContext->Slice = UICommandInfo;
}

TArray<UObject*> UFractureToolSlice::GetSettingsObjects() const 
{ 
	TArray<UObject*> Settings; 
	Settings.Add(GetMutableDefault<UFractureCommonSettings>());
	Settings.Add(GetMutableDefault<UFractureSliceSettings>());
	return Settings;
}

void UFractureToolSlice::GenerateSliceTransforms(const FFractureContext& Context, TArray<FTransform>& CuttingPlaneTransforms)
{
	const UFractureSliceSettings* LocalSliceSettings = GetMutableDefault<UFractureSliceSettings>();

	const FBox& Bounds = Context.Bounds;
	const FVector& Min = Context.Bounds.Min;
	const FVector& Max = Context.Bounds.Max;
	const FVector  Center = Context.Bounds.GetCenter();
	const FVector Extents(Context.Bounds.Max - Context.Bounds.Min);
	const FVector HalfExtents(Extents * 0.5f);

	const FVector Step(Extents.X / (LocalSliceSettings->SlicesX+1), Extents.Y / (LocalSliceSettings->SlicesY+1), Extents.Z / (LocalSliceSettings->SlicesZ+1));

	CuttingPlaneTransforms.Reserve(LocalSliceSettings->SlicesX * LocalSliceSettings->SlicesY * LocalSliceSettings->SlicesZ);

	FRandomStream RandomStream(Context.RandomSeed);

	const float SliceAngleVariationInRadians = FMath::DegreesToRadians(LocalSliceSettings->SliceAngleVariation);


	const FVector XMin(-Bounds.GetExtent() * FVector(1.0f, 1.0f, 0.0f));
	const FVector XMax(Bounds.GetExtent() * FVector(1.0f, 1.0f, 0.0f));

	for (int32 xx = 0; xx < LocalSliceSettings->SlicesX; ++xx)
	{
		const FVector SlicePosition(FVector(Min.X, Center.Y, Center.Z) + FVector((Step.X * xx) + Step.X, 0.0f, 0.0f) + RandomStream.VRand() * RandomStream.GetFraction() * LocalSliceSettings->SliceOffsetVariation);
		FTransform Transform(FQuat(FVector::RightVector, FMath::DegreesToRadians(90)), SlicePosition);
		const FQuat RotA(FVector::RightVector, RandomStream.FRandRange(0.0f, SliceAngleVariationInRadians));
		const FQuat RotB(FVector::ForwardVector, RandomStream.FRandRange(0.0f, SliceAngleVariationInRadians));
		Transform.ConcatenateRotation(FQuat(RotA * RotB));
		CuttingPlaneTransforms.Emplace(Transform);
	}



	const FVector YMin(-Bounds.GetExtent() * FVector(1.0f, 1.0f, 0.0f));
	const FVector YMax(Bounds.GetExtent() * FVector(1.0f, 1.0f, 0.0f));

	for (int32 yy = 0; yy < LocalSliceSettings->SlicesY; ++yy)
	{
		const FVector SlicePosition(FVector(Center.X, Min.Y, Center.Z) + FVector(0.0f, (Step.Y * yy) + Step.Y, 0.0f) + RandomStream.VRand() * RandomStream.GetFraction() * LocalSliceSettings->SliceOffsetVariation);
		FTransform Transform(FQuat(FVector::ForwardVector, FMath::DegreesToRadians(90)), SlicePosition);
		const FQuat RotA(FVector::RightVector, RandomStream.FRandRange(0.0f, SliceAngleVariationInRadians));
		const FQuat RotB(FVector::ForwardVector, RandomStream.FRandRange(0.0f, SliceAngleVariationInRadians));
		Transform.ConcatenateRotation(RotA * RotB);
		CuttingPlaneTransforms.Emplace(Transform);
	}




	const FVector ZMin(-Bounds.GetExtent() * FVector(1.0f, 1.0f, 0.0f));
	const FVector ZMax(Bounds.GetExtent() * FVector(1.0f, 1.0f, 0.0f));

	for (int32 zz = 0; zz < LocalSliceSettings->SlicesZ; ++zz)
	{
		const FVector SlicePosition(FVector(Center.X, Center.Y, Min.Z) + FVector(0.0f, 0.0f, (Step.Z * zz) + Step.Z) + RandomStream.VRand() * RandomStream.GetFraction() * LocalSliceSettings->SliceOffsetVariation);
		FTransform Transform(SlicePosition);
		const FQuat RotA(FVector::RightVector, RandomStream.FRandRange(0.0f, SliceAngleVariationInRadians));
		const FQuat RotB(FVector::ForwardVector, RandomStream.FRandRange(0.0f, SliceAngleVariationInRadians));
		Transform.ConcatenateRotation(RotA * RotB);
		CuttingPlaneTransforms.Emplace(Transform);
	}
}


#if WITH_EDITOR
void UFractureToolSlice::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	FractureContextChanged();
}
#endif



void UFractureToolSlice::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	const UFractureCommonSettings* LocalCommonSettings = GetDefault<UFractureCommonSettings>();

	for (const FTransform& Transform : RenderCuttingPlanesTransforms)
	{
		PDI->DrawPoint(Transform.GetLocation(), FLinearColor::Green, 4.f, SDPG_Foreground);

		PDI->DrawLine(Transform.GetLocation(), Transform.GetLocation() + Transform.GetUnitAxis(EAxis::X) * RenderCuttingPlaneSize, FLinearColor(255, 0, 0), SDPG_Foreground);
		PDI->DrawLine(Transform.GetLocation(), Transform.GetLocation() + Transform.GetUnitAxis(EAxis::Y) * RenderCuttingPlaneSize, FLinearColor(0, 255, 0), SDPG_Foreground);

		PDI->DrawLine(Transform.GetLocation() + Transform.GetUnitAxis(EAxis::X) * RenderCuttingPlaneSize, Transform.GetLocation() + Transform.GetUnitAxis(EAxis::X) * RenderCuttingPlaneSize + Transform.GetUnitAxis(EAxis::Y) * RenderCuttingPlaneSize, FLinearColor(255, 0, 0), SDPG_Foreground);
		PDI->DrawLine(Transform.GetLocation() + Transform.GetUnitAxis(EAxis::Y) * RenderCuttingPlaneSize, Transform.GetLocation() + Transform.GetUnitAxis(EAxis::X) * RenderCuttingPlaneSize + Transform.GetUnitAxis(EAxis::Y) * RenderCuttingPlaneSize, FLinearColor(0, 255, 0), SDPG_Foreground);
	}
}

void UFractureToolSlice::FractureContextChanged()
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


void UFractureToolSlice::ExecuteFracture(const FFractureContext& FractureContext)
{
	const UFractureCommonSettings* LocalCommonSettings = GetDefault<UFractureCommonSettings>();
	const UFractureSliceSettings* LocalSliceSettings = GetMutableDefault<UFractureSliceSettings>();

	if (FractureContext.FracturedGeometryCollection != nullptr)
	{
		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = FractureContext.FracturedGeometryCollection->GetGeometryCollection();
		if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
		{
			TArray<FTransform> LocalCuttingPlanesTransforms;
			GenerateSliceTransforms(FractureContext, LocalCuttingPlanesTransforms);

			TArray<FPlane> CuttingPlanes;
			CuttingPlanes.Reserve(LocalCuttingPlanesTransforms.Num());

			for (const FTransform& Transform : LocalCuttingPlanesTransforms)
			{
				CuttingPlanes.Add(FPlane(Transform.GetLocation(), Transform.GetUnitAxis(EAxis::Z)));
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

			int32 AddedGeomIdx = CutMultipleWithMultiplePlanes(CuttingPlanes, InternalSurfaceMaterials, *GeometryCollection, FractureContext.SelectedBones);
		}
	}
}

bool UFractureToolSlice::CanExecuteFracture() const
{
	return FFractureEditorModeToolkit::IsLeafBoneSelected();
}

#undef LOCTEXT_NAMESPACE