// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utilities/MeshUDIMClassifier.h"
#include "Spatial/SparseDynamicOctree3.h"
#include "UVEditorUXSettings.h"
#include "Selections/MeshConnectedComponents.h"

using namespace UE::Geometry;

FDynamicMeshUDIMClassifier::FDynamicMeshUDIMClassifier(const FDynamicMeshUVOverlay* UVOverlayIn, TOptional<TArray<int32>> SelectionIn)
{
	UVOverlay = UVOverlayIn;
	Selection = SelectionIn;
	ClassifyUDIMs();
}

TArray<FVector2i> FDynamicMeshUDIMClassifier::ActiveTiles() const
{
	TArray<FVector2i> Keys;
	UDIMs.GetKeys(Keys);
	return Keys;
}

TArray<int32> FDynamicMeshUDIMClassifier::TidsForTile(FVector2i TileIndexIn) const
{	
	if (ensure(UDIMs.Contains(TileIndexIn)))
	{
		return *(UDIMs.Find(TileIndexIn));
	}
	else
	{
		return TArray<int32>();
	}
}

FVector2i FDynamicMeshUDIMClassifier::ClassifyTrianglesToUDIM(const FDynamicMeshUVOverlay* UVOverlay, TArray<int32> Tids)
{
	auto UVTriangleToUDIM = [UVOverlay](int32 Tid)
	{
		FVector2i UDIM;
		FVector2f Vertex0, Vertex1, Vertex2;
		UVOverlay->GetTriElements(Tid, Vertex0, Vertex1, Vertex2);
		FVector2f BaryCenter = (Vertex0 + Vertex1 + Vertex2) / 3.0f;
		BaryCenter = FUVEditorUXSettings::InternalUVToExternalUV(BaryCenter);
		UDIM.X = FMath::Floor(BaryCenter.X);
		UDIM.Y = FMath::Floor(BaryCenter.Y);
		return UDIM;
	};

	TMap<FVector2i, int32> UDIMCount;
	for (int32 Tid : Tids)
	{
		FVector2i UDIM = UVTriangleToUDIM(Tid);
		UDIMCount.FindOrAdd(UDIM, 0)++;
	}
	FVector2i MaximumUDIM;
	int32 MaximumUDIMCount = 0;
	for (auto UDIMEntry : UDIMCount)
	{
		if (UDIMEntry.Value > MaximumUDIMCount)
		{
			MaximumUDIMCount = UDIMEntry.Value;
			MaximumUDIM = UDIMEntry.Key;
		}
	}
	return MaximumUDIM;
}


void FDynamicMeshUDIMClassifier::ClassifyUDIMs()
{
	int32 CurrentUDIMIndex = 0;
	const FDynamicMesh3* Mesh = UVOverlay->GetParentMesh();

	auto UVIslandPredicate = [this](int32 Triangle0, int32 Triangle1)
	{
		return UVOverlay->AreTrianglesConnected(Triangle0, Triangle1);
	};

	FMeshConnectedComponents UVComponents(Mesh);
	if (Selection.IsSet())
	{
		UVComponents.FindConnectedTriangles(Selection.GetValue(), UVIslandPredicate);
	}
	else
	{
		UVComponents.FindConnectedTriangles(UVIslandPredicate);
	}

	for (int32 Cid = 0; Cid < UVComponents.Num(); ++Cid)
	{
		FVector2i UDIM = ClassifyTrianglesToUDIM(UVOverlay, UVComponents.GetComponent(Cid).Indices);
		UDIMs.FindOrAdd(UDIM).Append(UVComponents.GetComponent(Cid).Indices);
	}

	return;

}
