// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolConvex.h"

#include "FractureToolContext.h"

#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionConvexUtility.h"
#include "GeometryCollection/GeometryCollectionProximityUtility.h"

#define LOCTEXT_NAMESPACE "FractureToolConvex"

UFractureToolConvex::UFractureToolConvex(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	ConvexSettings = NewObject<UFractureConvexSettings>(GetTransientPackage(), UFractureConvexSettings::StaticClass());
	ConvexSettings->OwnerTool = this;
}

bool UFractureToolConvex::CanExecute() const
{
	if (!IsGeometryCollectionSelected())
	{
		return false;
	}

	return true;
}

FText UFractureToolConvex::GetDisplayText() const
{
	return FText(LOCTEXT("FractureToolConvex", "Make Convex Collision Volumes"));
}

FText UFractureToolConvex::GetTooltipText() const
{
	return FText(LOCTEXT("FractureToolConvexTooltip", "This tool creates (non-overlapping) convex volumes for the bones of geometry collections"));
}

FSlateIcon UFractureToolConvex::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.Convex");
}

void UFractureToolConvex::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "Convex", "Convex", "Create (and visualize) a hierarchy of non-overlapping convex collision volumes for the bones of geometry collections.", EUserInterfaceActionType::ToggleButton, FInputChord());
	BindingContext->MakeConvex = UICommandInfo;
}

TArray<UObject*> UFractureToolConvex::GetSettingsObjects() const
{
	TArray<UObject*> Settings;
	Settings.Add(ConvexSettings);
	return Settings;
}

void UFractureToolConvex::FractureContextChanged()
{
	TArray<FFractureToolContext> FractureContexts = GetFractureToolContexts();

	ClearVisualizations();

	for (const FFractureToolContext& FractureContext : FractureContexts)
	{
		FGeometryCollection& Collection = *FractureContext.GetGeometryCollection();

		if (!Collection.HasAttribute("ConvexHull", "Convex") ||
			!Collection.HasAttribute("TransformToConvexIndices", FGeometryCollection::TransformGroup))
		{
			continue;
		}

		int32 CollectionIdx = VisualizedCollections.Add(FractureContext.GetGeometryCollectionComponent());

		FTransform OuterTransform = FractureContext.GetTransform();
		for (int32 TransformIdx : FractureContext.GetSelection())
		{
			FTransform InnerTransform = GeometryCollectionAlgo::GlobalMatrix(Collection.Transform, Collection.Parent, TransformIdx);
			FTransform CombinedTransform = InnerTransform * OuterTransform;

			TManagedArray<TSet<int32>>& TransformToConvexIndices = Collection.GetAttribute<TSet<int32>>("TransformToConvexIndices", FTransformCollection::TransformGroup);
			TManagedArray<TUniquePtr<Chaos::FConvex>>& ConvexHull = Collection.GetAttribute<TUniquePtr<Chaos::FConvex>>("ConvexHull", "Convex");

			EdgesMappings.AddMapping(CollectionIdx, TransformIdx, HullEdges.Num());

			for (int32 ConvexIdx : TransformToConvexIndices[TransformIdx])
			{
				int32 HullPtsStart = HullPoints.Num();
				for (Chaos::FVec3 Pt : ConvexHull[ConvexIdx]->GetVertices())
				{
					HullPoints.Add(CombinedTransform.TransformPosition(Pt));
				}
				int32 NumPlanes = ConvexHull[ConvexIdx]->NumPlanes();
				const Chaos::FConvexStructureData& HullData = ConvexHull[ConvexIdx]->GetStructureData();
				for (int PlaneIdx = 0; PlaneIdx < NumPlanes; PlaneIdx++)
				{
					int32 NumPlaneVerts = HullData.NumPlaneVertices(PlaneIdx);
					for (int32 PlaneVertexIdx = 0; PlaneVertexIdx < NumPlaneVerts; PlaneVertexIdx++)
					{
						int32 V0 = HullPtsStart + HullData.GetPlaneVertex(PlaneIdx, PlaneVertexIdx);
						int32 V1 = HullPtsStart + HullData.GetPlaneVertex(PlaneIdx, (PlaneVertexIdx + 1) % NumPlaneVerts);
						HullEdges.Add(TPair<int32, int32>(V0, V1));
					}
				}
			}
		}
	}
}

void UFractureToolConvex::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	EnumerateVisualizationMapping(EdgesMappings, HullEdges.Num(), [&](int32 Idx, FVector ExplodedVector)
	{
		TPair<int32, int32> Edge = HullEdges[Idx];
		FVector P1 = HullPoints[Edge.Key] + ExplodedVector;
		FVector P2 = HullPoints[Edge.Value] + ExplodedVector;
		PDI->DrawLine(P1, P2, FLinearColor::Green, SDPG_Foreground, 0.0f, 0.001f);
	});
}

void UFractureToolConvex::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	// update any cached data 
}

TArray<FFractureToolContext> UFractureToolConvex::GetFractureToolContexts() const
{
	TArray<FFractureToolContext> Contexts;

	// A context is gathered for each selected GeometryCollection component, or for each individual bone if Group Fracture is not used.
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);

	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		// Generate a context for each selected node
		FFractureToolContext FullSelection(GeometryCollectionComponent);

		Contexts.Add(FullSelection);
	}

	return Contexts;
}


int32 UFractureToolConvex::ExecuteFracture(const FFractureToolContext& FractureContext)
{
	if (FractureContext.GetGeometryCollection().IsValid())
	{
		FGeometryCollection& Collection = *FractureContext.GetGeometryCollection();
		FGeometryCollectionProximityUtility ProximityUtility(&Collection);
		ProximityUtility.UpdateProximity();
		FGeometryCollectionConvexUtility::CreateNonOverlappingConvexHullData(&Collection, ConvexSettings->FractionAllowRemove, ConvexSettings->SimplificationDistanceThreshold);
	}

	return INDEX_NONE;
}

#undef LOCTEXT_NAMESPACE
