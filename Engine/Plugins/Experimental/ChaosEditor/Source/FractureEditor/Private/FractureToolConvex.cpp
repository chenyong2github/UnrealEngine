// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolConvex.h"

#include "FractureToolContext.h"

#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionConvexUtility.h"
#include "GeometryCollection/GeometryCollectionProximityUtility.h"

#define LOCTEXT_NAMESPACE "FractureToolConvex"


void UFractureConvexSettings::DeleteFromSelected()
{
	UFractureToolConvex* ConvexTool = Cast<UFractureToolConvex>(OwnerTool.Get());
	ConvexTool->DeleteConvexFromSelected();
}

void UFractureConvexSettings::PromoteChildren()
{
	UFractureToolConvex* ConvexTool = Cast<UFractureToolConvex>(OwnerTool.Get());
	ConvexTool->PromoteChildren();
}

void UFractureConvexSettings::ClearCustomConvex()
{
	UFractureToolConvex* ConvexTool = Cast<UFractureToolConvex>(OwnerTool.Get());
	ConvexTool->ClearCustomConvex();
}

void UFractureToolConvex::DeleteConvexFromSelected()
{
	TArray<FFractureToolContext> FractureContexts = GetFractureToolContexts();

	for (const FFractureToolContext& FractureContext : FractureContexts)
	{
		FGeometryCollection& Collection = *FractureContext.GetGeometryCollection();

		if (!Collection.HasAttribute("ConvexHull", "Convex") ||
			!Collection.HasAttribute("TransformToConvexIndices", FGeometryCollection::TransformGroup))
		{
			continue;
		}

		TManagedArray<int32>& HasCustomConvex = *FGeometryCollectionConvexUtility::GetCustomConvexFlags(&Collection, true);

		TArray<int32> TransformsToClear;
		for (int32 TransformIdx : FractureContext.GetSelection())
		{
			if (Collection.SimulationType[TransformIdx] == FGeometryCollection::ESimulationTypes::FST_Clustered)
			{
				TransformsToClear.Add(TransformIdx);
				HasCustomConvex[TransformIdx] = 1;
			}
		}
		TransformsToClear.Sort();
		FGeometryCollectionConvexUtility::RemoveConvexHulls(&Collection, TransformsToClear);
	}

	FractureContextChanged();
}

void UFractureToolConvex::PromoteChildren()
{
	TArray<FFractureToolContext> FractureContexts = GetFractureToolContexts();

	for (FFractureToolContext& FractureContext : FractureContexts)
	{
		UFractureActionTool::FModifyContextScope ModifyScope(this, &FractureContext);

		FGeometryCollection& Collection = *FractureContext.GetGeometryCollection();

		if (!Collection.HasAttribute("ConvexHull", "Convex") ||
			!Collection.HasAttribute("TransformToConvexIndices", FGeometryCollection::TransformGroup))
		{
			continue;
		}

		TArray<int32> SelectedTransforms = FractureContext.GetSelection();
		FGeometryCollectionConvexUtility::CopyChildConvexes(&Collection, SelectedTransforms, &Collection, SelectedTransforms, false);
	}

	FractureContextChanged();
}

void UFractureToolConvex::ClearCustomConvex()
{
	TArray<FFractureToolContext> FractureContexts = GetFractureToolContexts();

	bool bAnyChanged = false;
	for (FFractureToolContext& FractureContext : FractureContexts)
	{
		bool bHasChanged = false;

		TManagedArray<int32>* HasCustomConvex = FGeometryCollectionConvexUtility::GetCustomConvexFlags(FractureContext.GetGeometryCollection().Get(), false);
		if (!HasCustomConvex)
		{
			continue;
		}

		UFractureActionTool::FModifyContextScope ModifyScope(this, &FractureContext);

		FGeometryCollection& Collection = *FractureContext.GetGeometryCollection();
		for (int32 TransformIdx : FractureContext.GetSelection())
		{
			if ((*HasCustomConvex)[TransformIdx])
			{
				bAnyChanged = bHasChanged = true;
				(*HasCustomConvex)[TransformIdx] = false;
			}
		}

		if (bHasChanged)
		{
			bool bAllFalse = true;
			for (int32 TransformIdx = 0; bAllFalse && TransformIdx < HasCustomConvex->Num(); TransformIdx++)
			{
				if ((*HasCustomConvex)[TransformIdx])
				{
					bAllFalse = false;
				}
			}
			if (bAllFalse)
			{
				Collection.RemoveAttribute("HasCustomConvex", FTransformCollection::TransformGroup);
			}

			AutoComputeConvex(FractureContext);
		}
	}

	if (bAnyChanged)
	{
		FractureContextChanged();
	}
}

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

		TManagedArray<int32>* HasCustomConvex = FGeometryCollectionConvexUtility::GetCustomConvexFlags(&Collection, false);

		int32 CollectionIdx = VisualizedCollections.Add(FractureContext.GetGeometryCollectionComponent());

		FTransform OuterTransform = FractureContext.GetTransform();
		for (int32 TransformIdx : FractureContext.GetSelection())
		{
			FTransform InnerTransform = GeometryCollectionAlgo::GlobalMatrix(Collection.Transform, Collection.Parent, TransformIdx);
			FTransform CombinedTransform = InnerTransform * OuterTransform;
			bool bIsCustom = HasCustomConvex ? bool((*HasCustomConvex)[TransformIdx]) : false;

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
						HullEdges.Add({ V0, V1, bIsCustom });
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
		const FEdgeVisInfo& Edge = HullEdges[Idx];
		FVector P1 = HullPoints[Edge.A] + ExplodedVector;
		FVector P2 = HullPoints[Edge.B] + ExplodedVector;
		PDI->DrawLine(P1, P2, Edge.bIsCustom ? FLinearColor::Red : FLinearColor::Green, SDPG_Foreground, 0.0f, 0.001f);
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

void UFractureToolConvex::AutoComputeConvex(const FFractureToolContext& FractureContext)
{
	if (FractureContext.GetGeometryCollection().IsValid())
	{
		FGeometryCollection& Collection = *FractureContext.GetGeometryCollection();
		FGeometryCollectionProximityUtility ProximityUtility(&Collection);
		ProximityUtility.UpdateProximity();
		FGeometryCollectionConvexUtility::CreateNonOverlappingConvexHullData(&Collection, ConvexSettings->FractionAllowRemove, ConvexSettings->SimplificationDistanceThreshold, ConvexSettings->CanExceedFraction);
	}
}


int32 UFractureToolConvex::ExecuteFracture(const FFractureToolContext& FractureContext)
{
	AutoComputeConvex(FractureContext);

	return INDEX_NONE;
}

#undef LOCTEXT_NAMESPACE
