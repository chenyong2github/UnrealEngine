// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolResample.h"
#include "FractureEditorStyle.h"
#include "PlanarCut.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "FractureEditorModeToolkit.h"
#include "FractureToolContext.h"

#define LOCTEXT_NAMESPACE "FractureResample"


UFractureToolResample::UFractureToolResample(const FObjectInitializer& ObjInit) 
	: Super(ObjInit) 
{
}

FText UFractureToolResample::GetDisplayText() const
{
	return FText(NSLOCTEXT("Resample", "FractureToolResample", "Update Collision Samples")); 
}

FText UFractureToolResample::GetTooltipText() const 
{
	return FText(NSLOCTEXT("Resample", "FractureToolResampleTooltip", "The Resample tool can add collision samples in large flat regions that otherwise might have poor collision response."));
}

FSlateIcon UFractureToolResample::GetToolIcon() const 
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.Mesh");
}

void UFractureToolResample::RegisterUICommand( FFractureEditorCommands* BindingContext ) 
{
	UI_COMMAND_EXT( BindingContext, UICommandInfo, "Resample", "Resample", "Resample", EUserInterfaceActionType::ToggleButton, FInputChord() );
	BindingContext->Resample = UICommandInfo;
}

void UFractureToolResample::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	for (FVector Point : GeneratedPoints)
	{
		PDI->DrawPoint(Point, FLinearColor::Green, 2.f, SDPG_Foreground);
	}
	
}

TArray<UObject*> UFractureToolResample::GetSettingsObjects() const
 {
	TArray<UObject*> Settings;
	Settings.Add(CollisionSettings);
	return Settings;
}


void UFractureToolResample::FractureContextChanged()
{
	TArray<FFractureToolContext> FractureContexts = GetFractureToolContexts();

	GeneratedPoints.Reset();
	for (const FFractureToolContext& FractureContext : FractureContexts)
	{
		FGeometryCollection& Collection = *FractureContext.GetGeometryCollection();
		

		FTransform OuterTransform = FractureContext.GetTransform();
		for (int32 TransformIdx : FractureContext.GetSelection())
		{
			FTransform InnerTransform = GeometryCollectionAlgo::GlobalMatrix(Collection.Transform, Collection.Parent, TransformIdx);
			if (Collection.HasAttribute("ExplodedVector", FGeometryCollection::TransformGroup))
			{
				TManagedArray<FVector>& ExplodedVectors = Collection.GetAttribute<FVector>("ExplodedVector", FGeometryCollection::TransformGroup);
				InnerTransform = InnerTransform * FTransform(ExplodedVectors[TransformIdx]);
			}
			
			FTransform CombinedTransform = InnerTransform * OuterTransform;
			int32 GeometryIdx = Collection.TransformToGeometryIndex[TransformIdx];
			int32 VertStart = Collection.VertexStart[GeometryIdx];
			int32 VertCount = Collection.VertexCount[GeometryIdx];
			for (int32 VIdx = VertStart; VIdx < VertStart + VertCount; VIdx++)
			{
				GeneratedPoints.Add(CombinedTransform.TransformPosition(Collection.Vertex[VIdx]));
			}
		}
	}
}

int32 UFractureToolResample::ExecuteFracture(const FFractureToolContext& FractureContext)
{
	if (FractureContext.IsValid())
	{
		const UFractureCollisionSettings* LocalCutSettings = CollisionSettings;

		return AddCollisionSampleVertices(LocalCutSettings->PointSpacing, *FractureContext.GetGeometryCollection(), FractureContext.GetSelection());
	}

	return INDEX_NONE;
}


#undef LOCTEXT_NAMESPACE