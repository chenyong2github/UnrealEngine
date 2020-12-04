// Copyright Epic Games, Inc. All Rights Reserved.

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


UFractureToolBrick::UFractureToolBrick(const FObjectInitializer& ObjInit) 
	: Super(ObjInit) 
{
	BrickSettings = NewObject<UFractureBrickSettings>(GetTransientPackage(), UFractureBrickSettings::StaticClass());
	BrickSettings->OwnerTool = this;
}

FText UFractureToolBrick::GetDisplayText() const
{ 
	return FText(NSLOCTEXT("Fracture", "FractureToolBrick", "Brick Fracture")); 
}

FText UFractureToolBrick::GetTooltipText() const 
{ 
	return FText(NSLOCTEXT("Fracture", "FractureToolBrickTooltip", "This type of fracture enables you to define a pattern to perform the fracture, along with the forward and up axis in which to fracture. You can also adjust the brick length, height, or depth to provide varying results.  Click the Fracture Button to commit the fracture to the geometry collection.")); 
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
	Settings.Add(CutterSettings);
	Settings.Add(BrickSettings);
	return Settings;
}

void UFractureToolBrick::GenerateBrickTransforms(const FBox& Bounds)
{
	const FVector& Min = Bounds.Min;
	const FVector& Max = Bounds.Max;
	const FVector Extents(Bounds.Max - Bounds.Min);

	const FQuat HeaderRotation(FVector::UpVector, 1.5708);

	const float HalfHeight = BrickSettings->BrickHeight * 0.5f;
	const float HalfDepth = BrickSettings->BrickDepth * 0.5f;
	const float HalfLength = BrickSettings->BrickLength * 0.5f;

	if (BrickSettings->Bond == EFractureBrickBond::Stretcher)
	{
		bool OddY = false;
		for (float yy = 0.f; yy <= Extents.Y; yy += BrickSettings->BrickDepth)
		{
			bool Oddline = false;
			for (float zz = HalfHeight; zz <= Extents.Z; zz += BrickSettings->BrickHeight)
			{
				for (float xx = 0.f; xx <= Extents.X; xx += BrickSettings->BrickLength)
				{
					FVector BrickPosition(Min + FVector(Oddline ^ OddY ? xx : xx + HalfLength, yy, zz));
					BrickTransforms.Emplace(FTransform(BrickPosition));
				}
				Oddline = !Oddline;
			}
				OddY = !OddY;
		}
	}
	else if (BrickSettings->Bond == EFractureBrickBond::Stack)
	{
		bool OddY = false;
		for (float yy = 0.f; yy <= Extents.Y; yy += BrickSettings->BrickDepth)
		{
			for (float zz = HalfHeight; zz <= Extents.Z; zz += BrickSettings->BrickHeight)
			{
				for (float xx = 0.f; xx <= Extents.X; xx += BrickSettings->BrickLength)
				{
					FVector BrickPosition(Min + FVector(OddY ? xx : xx + HalfLength, yy, zz));
					BrickTransforms.Emplace(FTransform(BrickPosition));
				}
			}
			OddY = !OddY;
		}
	}
	else if (BrickSettings->Bond == EFractureBrickBond::English)
	{
		float HalfLengthDepthDifference = HalfLength - HalfDepth - HalfDepth;
		bool OddY = false;
		for (float yy = 0.f; yy <= Extents.Y; yy += BrickSettings->BrickDepth)
		{
			bool Oddline = false;
			for (float zz = HalfHeight; zz <= Extents.Z; zz += BrickSettings->BrickHeight)
			{
				if (Oddline && !OddY) // header row
				{
					for (float xx = 0.f; xx <= Extents.X; xx += BrickSettings->BrickDepth)
					{
						FVector BrickPosition(Min + FVector(Oddline ^ OddY ? xx : xx + HalfDepth, yy + HalfDepth, zz));
						BrickTransforms.Emplace(FTransform(HeaderRotation, BrickPosition));
					}
				}
				else if(!Oddline) // stretchers
				{
					for (float xx = 0.f; xx <= Extents.X; xx += BrickSettings->BrickLength)
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
	else if (BrickSettings->Bond == EFractureBrickBond::Header)
	{
		bool OddY = false;
		for (float yy = 0.f; yy <= Extents.Y; yy += BrickSettings->BrickLength)
		{
			bool Oddline = false;
			for (float zz = HalfHeight; zz <= Extents.Z; zz += BrickSettings->BrickHeight)
			{
				for (float xx = 0.f; xx <= Extents.X; xx += BrickSettings->BrickDepth)
				{
					FVector BrickPosition(Min + FVector(Oddline ^ OddY ? xx : xx + HalfDepth, yy, zz));
					BrickTransforms.Emplace(FTransform(HeaderRotation, BrickPosition));
				}
				Oddline = !Oddline;
			}
			OddY = !OddY;
		}
	}
	else if (BrickSettings->Bond == EFractureBrickBond::Flemish)
	{
		float HalfLengthDepthDifference = HalfLength - BrickSettings->BrickDepth  ;
		bool OddY = false;
		int32 RowY = 0;
		for (float yy = 0.f; yy <= Extents.Y; yy += BrickSettings->BrickDepth)
		{
			bool OddZ = false;
			for (float zz = HalfHeight; zz <= Extents.Z; zz += BrickSettings->BrickHeight)
			{
				bool OddX = OddZ;
				for (float xx = 0.f; xx <= Extents.X; xx += HalfLength + HalfDepth)
				{
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
		}
	}

	FVector BrickMax(HalfLength, HalfDepth, HalfHeight);
	FVector BrickMin(-BrickMax);

	for (const auto& Transform : BrickTransforms)
	{
		AddBoxEdges(Transform.TransformPosition(BrickMin), Transform.TransformPosition(BrickMax));
	}
}

void UFractureToolBrick::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	FFractureToolContext FractureContext;

	FractureContext.Bounds = FBox(ForceInitToZero);

	USelection* SelectionSet = GEditor->GetSelectedActors();

	TArray<AActor*> SelectedActors;
	SelectedActors.Reserve(SelectionSet->Num());

	FractureContext.RandomSeed = FMath::Rand();
	if (CutterSettings->RandomSeed > -1)
	{
		FractureContext.RandomSeed = CutterSettings->RandomSeed;
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
			FractureContext.Transform = Actor->GetTransform();
			FVector Origin;
			FVector BoxExtent;
			Actor->GetActorBounds(false, Origin, BoxExtent);
			if (CutterSettings->bGroupFracture)
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

	if (CutterSettings->bGroupFracture)
	{
		GenerateBrickTransforms(FractureContext.Bounds);
	}

}

void UFractureToolBrick::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	//TODO :Plane cutting drawing

	for (const FTransform& Transform : BrickTransforms)
	{
		PDI->DrawPoint(Transform.GetLocation(), FLinearColor::Green, 4.f, SDPG_Foreground);
	}

	if (CutterSettings->bDrawDiagram)
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

void UFractureToolBrick::ExecuteFracture(const FFractureToolContext& FractureContext)
{
	if (FractureContext.FracturedGeometryCollection != nullptr)
	{
 		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = FractureContext.FracturedGeometryCollection->GetGeometryCollection();
 		if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
 		{
			BrickTransforms.Empty();
			GenerateBrickTransforms(FractureContext.Bounds);

			const FQuat HeaderRotation(FVector::UpVector, 1.5708);

			const float HalfHeight = BrickSettings->BrickHeight * 0.5f;
			const float HalfDepth = BrickSettings->BrickDepth * 0.5f;
			const float HalfLength = BrickSettings->BrickLength * 0.5f;

			FVector Max(HalfLength, HalfDepth, HalfHeight);
			TArray<FBox> BricksToCut;

			for (const FTransform& Trans : BrickTransforms)
			{
				BricksToCut.Add(FBox(-Max * 0.95f, Max * 0.95f).TransformBy(Trans));
			}

 			FPlanarCells VoronoiPlanarCells = FPlanarCells(BricksToCut);

			FNoiseSettings NoiseSettings;
			if (CutterSettings->Amplitude > 0.0f)
			{
				NoiseSettings.Amplitude = CutterSettings->Amplitude;
				NoiseSettings.Frequency = CutterSettings->Frequency;
				NoiseSettings.Octaves = CutterSettings->OctaveNumber;
				NoiseSettings.PointSpacing = CutterSettings->SurfaceResolution;
				VoronoiPlanarCells.InternalSurfaceMaterials.NoiseSettings = NoiseSettings;
			}

			CutMultipleWithPlanarCells(VoronoiPlanarCells, *GeometryCollection, FractureContext.SelectedBones);
 		}
	}
}

#undef LOCTEXT_NAMESPACE
