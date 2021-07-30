// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolFixTinyGeo.h"
#include "FractureEditorStyle.h"
#include "PlanarCut.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "FractureEditorModeToolkit.h"
#include "FractureToolContext.h"

#define LOCTEXT_NAMESPACE "FractureFixTinyGeo"


UFractureToolFixTinyGeo::UFractureToolFixTinyGeo(const FObjectInitializer& ObjInit) 
	: Super(ObjInit) 
{
	TinyGeoSettings = NewObject<UFractureTinyGeoSettings>(GetTransientPackage(), UFractureTinyGeoSettings::StaticClass());
	TinyGeoSettings->OwnerTool = this;
}

FText UFractureToolFixTinyGeo::GetDisplayText() const
{
	return FText(NSLOCTEXT("FixTinyGeo", "FractureToolFixTinyGeo", "Fix Too-Small Geometry")); 
}

FText UFractureToolFixTinyGeo::GetTooltipText() const 
{
	return FText(NSLOCTEXT("FixTinyGeo", "FractureToolFixTinyGeoTooltip", "The TinyGeo tool glues \"too small\" pieces of geometry onto their neighbors."));
}

FSlateIcon UFractureToolFixTinyGeo::GetToolIcon() const 
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.FixTinyGeo");
}

void UFractureToolFixTinyGeo::RegisterUICommand( FFractureEditorCommands* BindingContext ) 
{
	UI_COMMAND_EXT( BindingContext, UICommandInfo, "FixTinyGeo", "TinyGeo", "FixTinyGeo", EUserInterfaceActionType::ToggleButton, FInputChord() );
	BindingContext->FixTinyGeo = UICommandInfo;
}

void UFractureToolFixTinyGeo::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	for (FBox& Box : ToRemoveBounds)
	{
		FVector B000 = Box.Min;
		FVector B111 = Box.Max;
		FVector B011(B000.X, B111.Y, B111.Z);
		FVector B101(B111.X, B000.Y, B111.Z);
		FVector B110(B111.X, B111.Y, B000.Z);
		FVector B001(B000.X, B000.Y, B111.Z);
		FVector B010(B000.X, B111.Y, B000.Z);
		FVector B100(B111.X, B000.Y, B000.Z);
		PDI->DrawLine(B000, B100, FLinearColor::Red, SDPG_Foreground, 0.0f, 0.001f);
		PDI->DrawLine(B000, B010, FLinearColor::Red, SDPG_Foreground, 0.0f, 0.001f);
		PDI->DrawLine(B000, B001, FLinearColor::Red, SDPG_Foreground, 0.0f, 0.001f);
		PDI->DrawLine(B111, B011, FLinearColor::Red, SDPG_Foreground, 0.0f, 0.001f);
		PDI->DrawLine(B111, B101, FLinearColor::Red, SDPG_Foreground, 0.0f, 0.001f);
		PDI->DrawLine(B111, B110, FLinearColor::Red, SDPG_Foreground, 0.0f, 0.001f);
		PDI->DrawLine(B001, B101, FLinearColor::Red, SDPG_Foreground, 0.0f, 0.001f);
		PDI->DrawLine(B001, B011, FLinearColor::Red, SDPG_Foreground, 0.0f, 0.001f);
		PDI->DrawLine(B110, B100, FLinearColor::Red, SDPG_Foreground, 0.0f, 0.001f);
		PDI->DrawLine(B110, B010, FLinearColor::Red, SDPG_Foreground, 0.0f, 0.001f);
		PDI->DrawLine(B100, B101, FLinearColor::Red, SDPG_Foreground, 0.0f, 0.001f);
		PDI->DrawLine(B010, B011, FLinearColor::Red, SDPG_Foreground, 0.0f, 0.001f);
	}
	
}

TArray<UObject*> UFractureToolFixTinyGeo::GetSettingsObjects() const
 {
	TArray<UObject*> Settings;
	Settings.Add(TinyGeoSettings);
	Settings.Add(CollisionSettings);
	return Settings;
}


void UFractureToolFixTinyGeo::FractureContextChanged()
{
	UpdateDefaultRandomSeed();
	TArray<FFractureToolContext> FractureContexts = GetFractureToolContexts();

	ToRemoveBounds.Reset();

	for (const FFractureToolContext& FractureContext : FractureContexts)
	{
		FGeometryCollection& Collection = *FractureContext.GetGeometryCollection();

		TArray<double> Volumes;
		FindBoneVolumes(
			Collection,
			TArrayView<int32>(), /*Empty array => use all transforms*/
			Volumes,
			VolDimScale
		);

		double MinVolume = GetMinVolume(Volumes);

		TArray<int32> SmallIndices;
		FindSmallBones(
			Collection,
			TArrayView<int32>(),
			Volumes,
			MinVolume,
			SmallIndices
		);

		FTransform OuterTransform = FractureContext.GetTransform();
		for (int32 TransformIdx : SmallIndices) // small transforms
		{
			FTransform InnerTransform = GeometryCollectionAlgo::GlobalMatrix(Collection.Transform, Collection.Parent, TransformIdx);
			if (Collection.HasAttribute("ExplodedVector", FGeometryCollection::TransformGroup))
			{
				TManagedArray<FVector3f>& ExplodedVectors = Collection.GetAttribute<FVector3f>("ExplodedVector", FGeometryCollection::TransformGroup);
				InnerTransform = InnerTransform * FTransform(ExplodedVectors[TransformIdx]);
			}

			FTransform CombinedTransform = InnerTransform * OuterTransform;

			int32 GeometryIdx = Collection.TransformToGeometryIndex[TransformIdx];
			if (GeometryIdx == INDEX_NONE)
			{
				ensure(false); // already filtered for small geo, shouldn't have any missing-geo transforms in the list
				continue;
			}
			int32 VStart = Collection.VertexStart[GeometryIdx];
			int32 VEnd = VStart + Collection.VertexCount[GeometryIdx];
			FBox Bounds(EForceInit::ForceInit);
			for (int32 VIdx = VStart; VIdx < VEnd; VIdx++)
			{
				Bounds += CombinedTransform.TransformPosition(Collection.Vertex[VIdx]);
			}
			ToRemoveBounds.Add(Bounds);
		}
	}
}

double UFractureToolFixTinyGeo::GetMinVolume(TArray<double>& Volumes)
{
	double MinVolume = 0;
	if (TinyGeoSettings->SelectionMethod == EGeometrySelectionMethod::VolumeCubeRoot)
	{
		MinVolume = TinyGeoSettings->MinVolumeCubeRoot * VolDimScale;
		MinVolume = MinVolume * MinVolume * MinVolume;
	}
	else // EGeometrySelectionMethod::RelativeVolume
	{
		double VolumeSum = 0;
		for (double Volume : Volumes)
		{
			VolumeSum += Volume;
		}
		MinVolume = FMath::Pow(VolumeSum, 1.0 / 3.0) * TinyGeoSettings->RelativeVolume;
		MinVolume = MinVolume * MinVolume * MinVolume;
	}
	return MinVolume;
}

int32 UFractureToolFixTinyGeo::ExecuteFracture(const FFractureToolContext& FractureContext)
{
	int32 UpdateIdx = INDEX_NONE;
	if (FractureContext.GetGeometryCollection().IsValid())
	{
		TArray<int32> TransformIndices; // left empty to refer to all indices

		TArray<double> Volumes;
		FGeometryCollection& Collection = *FractureContext.GetGeometryCollection();
		FindBoneVolumes(
			Collection,
			TransformIndices,
			Volumes,
			VolDimScale
		);

		double MinVolume = GetMinVolume(Volumes);

		TArray<int32> SmallIndices;
		FindSmallBones(
			Collection,
			TransformIndices,
			Volumes,
			MinVolume,
			SmallIndices
		);
		MergeBones(
			Collection,
			TransformIndices,
			Volumes,
			MinVolume,
			SmallIndices,
			false /*bUnionJoinedPieces*/ // Note: Union-ing the pieces is nicer in theory, but can leave cracks and non-manifold garbage
		);
		UpdateIdx = 0;
	}

	return UpdateIdx;
}


#undef LOCTEXT_NAMESPACE