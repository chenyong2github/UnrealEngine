// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FractureToolBrick.h"

#include "FractureEditorModeToolkit.h"
#include "FractureEditorStyle.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "FractureEditorStyle.h"
#include "GameFramework/Actor.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "PlanarCut.h"

#define LOCTEXT_NAMESPACE "FractureBrick"

void UFractureBrickSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (OwnerTool != nullptr)
	{
		OwnerTool->PostEditChangeProperty(PropertyChangedEvent);
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UFractureBrickSettings::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (OwnerTool != nullptr)
	{
		OwnerTool->PostEditChangeChainProperty(PropertyChangedEvent);
	}
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}


UFractureToolBrick::UFractureToolBrick(const FObjectInitializer& ObjInit) 
	: Super(ObjInit) 
{
	BrickSettings = NewObject<UFractureBrickSettings>(GetTransientPackage(), UFractureBrickSettings::StaticClass());
	BrickSettings->OwnerTool = this;
}

FText UFractureToolBrick::GetDisplayText() const
{ 
	return FText(NSLOCTEXT("Fracture", "FractureToolBrick", "Brick")); 
}

FText UFractureToolBrick::GetTooltipText() const 
{ 
	return FText(NSLOCTEXT("Fracture", "FractureToolBrickTooltip", "Brick Fracture")); 
}

FSlateIcon UFractureToolBrick::GetToolIcon() const 
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.Brick");
}

void UFractureToolBrick::RegisterUICommand( FFractureEditorCommands* BindingContext ) 
{
	UI_COMMAND_EXT( BindingContext, UICommandInfo, "Brick", "Brick", "Brick Voronoi Fracture", EUserInterfaceActionType::ToggleButton, FInputChord() );
	BindingContext->Brick = UICommandInfo;
}

TArray<UObject*> UFractureToolBrick::GetSettingsObjects() const 
{ 
	TArray<UObject*> Settings; 
	Settings.Add(GetMutableDefault<UFractureCommonSettings>());
	Settings.Add(GetMutableDefault<UFractureBrickSettings>());
	return Settings;
}

void UFractureToolBrick::GenerateBrickTransforms(const FBox& Bounds)
{
	const UFractureBrickSettings* LocalBrickSettings = GetMutableDefault<UFractureBrickSettings>();

	const FVector& Min = Bounds.Min;
	const FVector& Max = Bounds.Max;
	const FVector Extents(Bounds.Max - Bounds.Min);

	const FQuat HeaderRotation(FVector::UpVector, 1.5708);

	const float HalfHeight = LocalBrickSettings->BrickHeight * 0.5f;
	const float HalfDepth = LocalBrickSettings->BrickDepth * 0.5f;
	const float HalfLength = LocalBrickSettings->BrickLength * 0.5f;

	if (LocalBrickSettings->Bond == EFractureBrickBond::Stretcher)
	{
		bool OddY = false;
		for (float yy = 0.f; yy <= Extents.Y; yy += LocalBrickSettings->BrickDepth)
		{
			bool Oddline = false;
			for (float zz = HalfHeight; zz <= Extents.Z; zz += LocalBrickSettings->BrickHeight)
			{
				for (float xx = 0.f; xx <= Extents.X; xx += LocalBrickSettings->BrickLength)
				{
					FVector BrickPosition(Min + FVector(Oddline ^ OddY ? xx : xx + HalfLength, yy, zz));
					BrickTransforms.Emplace(FTransform(BrickPosition));
				}
				Oddline = !Oddline;
			}
				OddY = !OddY;
		}
	}
	else if (LocalBrickSettings->Bond == EFractureBrickBond::Stack)
	{
		bool OddY = false;
		for (float yy = 0.f; yy <= Extents.Y; yy += LocalBrickSettings->BrickDepth)
		{
			for (float zz = HalfHeight; zz <= Extents.Z; zz += LocalBrickSettings->BrickHeight)
			{
				for (float xx = 0.f; xx <= Extents.X; xx += LocalBrickSettings->BrickLength)
				{
					FVector BrickPosition(Min + FVector(OddY ? xx : xx + HalfLength, yy, zz));
					BrickTransforms.Emplace(FTransform(BrickPosition));
				}
			}
			OddY = !OddY;
		}
	}
	else if (LocalBrickSettings->Bond == EFractureBrickBond::English)
	{
		float HalfLengthDepthDifference = HalfLength - HalfDepth - HalfDepth;
		bool OddY = false;
		for (float yy = 0.f; yy <= Extents.Y; yy += LocalBrickSettings->BrickDepth)
		{
			bool Oddline = false;
			for (float zz = HalfHeight; zz <= Extents.Z; zz += LocalBrickSettings->BrickHeight)
			{
				if (Oddline && !OddY) // header row
				{
					for (float xx = 0.f; xx <= Extents.X; xx += LocalBrickSettings->BrickDepth)
					{
						FVector BrickPosition(Min + FVector(Oddline ^ OddY ? xx : xx + HalfDepth, yy + HalfDepth, zz));
						BrickTransforms.Emplace(FTransform(HeaderRotation, BrickPosition));
					}
				}
				else if(!Oddline) // stretchers
				{
					for (float xx = 0.f; xx <= Extents.X; xx += LocalBrickSettings->BrickLength)
					{
						FVector BrickPosition(Min + FVector(Oddline ^ OddY ? xx : xx + HalfLength, OddY ? yy + HalfLengthDepthDifference : yy - HalfLengthDepthDifference , zz));
						BrickTransforms.Emplace(FTransform(BrickPosition));
					}
				}
				Oddline = !Oddline;
			}
			OddY = !OddY;
		}
	}
	else if (LocalBrickSettings->Bond == EFractureBrickBond::Header)
	{
		bool OddY = false;
		for (float yy = 0.f; yy <= Extents.Y; yy += LocalBrickSettings->BrickLength)
		{
			bool Oddline = false;
			for (float zz = HalfHeight; zz <= Extents.Z; zz += LocalBrickSettings->BrickHeight)
			{
				for (float xx = 0.f; xx <= Extents.X; xx += LocalBrickSettings->BrickDepth)
				{
					FVector BrickPosition(Min + FVector(Oddline ^ OddY ? xx : xx + HalfDepth, yy, zz));
					BrickTransforms.Emplace(FTransform(HeaderRotation, BrickPosition));
				}
				Oddline = !Oddline;
			}
			OddY = !OddY;
		}
	}
	else if (LocalBrickSettings->Bond == EFractureBrickBond::Flemish)
	{
		float HalfLengthDepthDifference = HalfLength - LocalBrickSettings->BrickDepth  ;
		bool OddY = false;
		int32 RowY = 0;
		for (float yy = 0.f; yy <= Extents.Y; yy += LocalBrickSettings->BrickDepth)
		{
			bool OddZ = false;
			for (float zz = HalfHeight; zz <= Extents.Z; zz += LocalBrickSettings->BrickHeight)
			{
				bool OddX = OddZ;
				for (float xx = 0.f; xx <= Extents.X; xx += HalfLength + HalfDepth)
				{
//					FVector BrickPosition(Min + FVector(OddY ? xx : xx + HalfLength, yy, zz));
					FVector BrickPosition(Min + FVector(xx,yy,zz));
					if (OddX)
					{
						if(OddY) // runner
						{
							BrickTransforms.Emplace(FTransform(BrickPosition + FVector(0, HalfLengthDepthDifference, 0))); // runner
						}
						else
						{
							BrickTransforms.Emplace(FTransform(BrickPosition - FVector(0, HalfLengthDepthDifference, 0))); // runner

						}
					}
					else if (!OddY) // header
					{
						BrickTransforms.Emplace(FTransform(HeaderRotation, BrickPosition + FVector(0, HalfDepth, 0))); // header
					}
					OddX = !OddX;
				}
				OddZ = !OddZ;
			}
			OddY = !OddY;
			++RowY;
// 			if (RowY % 2)
// 			{
// 				yy += HalfLengthDepthDifference;
// 			}
		}
	}

	FVector BrickMax(HalfLength, HalfDepth, HalfHeight);
	FVector BrickMin(-BrickMax);


	for (const auto& Transform : BrickTransforms)
	{
		AddBoxEdges(Transform.TransformPosition(BrickMin), Transform.TransformPosition(BrickMax));
	}
}

#if WITH_EDITOR
void UFractureToolBrick::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	const UFractureCommonSettings* LocalCommonSettings = GetDefault<UFractureCommonSettings>();
	const UFractureBrickSettings* LocalBrickSettings = GetMutableDefault<UFractureBrickSettings>();

	FFractureContext FractureContext;

	FractureContext.Bounds = FBox(ForceInitToZero);

	USelection* SelectionSet = GEditor->GetSelectedActors();

	TArray<AActor*> SelectedActors;
	SelectedActors.Reserve(SelectionSet->Num());

	FractureContext.RandomSeed = FMath::Rand();
	if (LocalCommonSettings->RandomSeed > -1)
	{
		FractureContext.RandomSeed = LocalCommonSettings->RandomSeed;
	}

	FBox SelectedMeshBounds(ForceInit);

	SelectionSet->GetSelectedObjects(SelectedActors);
	BrickTransforms.Empty();
	Edges.Empty();

	for (AActor* Actor : SelectedActors)
	{
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		Actor->GetComponents(PrimitiveComponents);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			FractureContext.OriginalActor = Actor;
			FractureContext.OriginalPrimitiveComponent = PrimitiveComponent;
			// 				FractureContext.Transform = PrimitiveComponent->GetComponentTransform();
			FractureContext.Transform = Actor->GetTransform();
			FVector Origin;
			FVector BoxExtent;
			Actor->GetActorBounds(false, Origin, BoxExtent);
			if (LocalCommonSettings->bGroupFracture)
			{
				FractureContext.Bounds += FBox::BuildAABB(Origin, BoxExtent);
			}
			else
			{
				FractureContext.Bounds = FBox::BuildAABB(Origin, BoxExtent);
				GenerateBrickTransforms(FractureContext.Bounds);
			}
		}
	}

	if (LocalCommonSettings->bGroupFracture)
	{
		GenerateBrickTransforms(FractureContext.Bounds);
	}

}
#endif


void UFractureToolBrick::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	const UFractureCommonSettings* LocalCommonSettings = GetDefault<UFractureCommonSettings>();


	//TODO :Plane cutting drawing

	for (const FTransform& Transform : BrickTransforms)
	{
		PDI->DrawPoint(Transform.GetLocation(), FLinearColor::Green, 4.f, SDPG_Foreground);
	}

	if (LocalCommonSettings->bDrawDiagram)
	{
		PDI->AddReserveLines(SDPG_Foreground, Edges.Num(), false, false);
		for (int32 ii = 0, ni = Edges.Num(); ii < ni; ++ii)
		{
			PDI->DrawLine(Edges[ii].Get<0>(), Edges[ii].Get<1>(), FLinearColor(255, 0, 0), SDPG_Foreground);
		}
	}

}


void UFractureToolBrick::AddBoxEdges(const FVector& Min, const FVector& Max)
{
	Edges.Emplace(MakeTuple(Min, FVector(Min.X, Max.Y, Min.Z)));
	Edges.Emplace(MakeTuple(Min, FVector(Min.X, Min.Y, Max.Z)));
	Edges.Emplace(MakeTuple(FVector(Min.X, Max.Y, Max.Z), FVector(Min.X, Max.Y, Min.Z)));
	Edges.Emplace(MakeTuple(FVector(Min.X, Max.Y, Max.Z), FVector(Min.X, Min.Y, Max.Z)));

	Edges.Emplace(MakeTuple(FVector(Max.X, Min.Y, Min.Z), FVector(Max.X, Max.Y, Min.Z)));
	Edges.Emplace(MakeTuple(FVector(Max.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Max.Z)));
	Edges.Emplace(MakeTuple(Max, FVector(Max.X, Max.Y, Min.Z)));
	Edges.Emplace(MakeTuple(Max, FVector(Max.X, Min.Y, Max.Z)));

	Edges.Emplace(MakeTuple(Min, FVector(Max.X, Min.Y, Min.Z)));
	Edges.Emplace(MakeTuple(FVector(Min.X, Min.Y, Max.Z), FVector(Max.X, Min.Y, Max.Z)));
	Edges.Emplace(MakeTuple(FVector(Min.X, Max.Y, Min.Z), FVector(Max.X, Max.Y, Min.Z)));
	Edges.Emplace(MakeTuple(FVector(Min.X, Max.Y, Max.Z), Max));
}

void UFractureToolBrick::ExecuteFracture(const FFractureContext& FractureContext)
{
	if (FractureContext.FracturedGeometryCollection != nullptr)
	{
 		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = FractureContext.FracturedGeometryCollection->GetGeometryCollection();
 		if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
 		{
			BrickTransforms.Empty();
			GenerateBrickTransforms(FractureContext.Bounds);

			const UFractureCommonSettings* LocalCommonSettings = GetDefault<UFractureCommonSettings>();
			const UFractureBrickSettings* LocalBrickSettings = GetMutableDefault<UFractureBrickSettings>();

			const FQuat HeaderRotation(FVector::UpVector, 1.5708);

			const float HalfHeight = LocalBrickSettings->BrickHeight * 0.5f;
			const float HalfDepth = LocalBrickSettings->BrickDepth * 0.5f;
			const float HalfLength = LocalBrickSettings->BrickLength * 0.5f;

			FVector Max(HalfLength, HalfDepth, HalfHeight);
			TArray<FBox> BricksToCut;

			for (const FTransform& Trans : BrickTransforms)
			{
				BricksToCut.Add(FBox(-Max * 0.95f, Max * 0.95f).TransformBy(Trans));
			}

 			FPlanarCells VoronoiPlanarCells = FPlanarCells(BricksToCut);

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

bool UFractureToolBrick::CanExecuteFracture() const
{
	return FFractureEditorModeToolkit::IsLeafBoneSelected();
}

#undef LOCTEXT_NAMESPACE
