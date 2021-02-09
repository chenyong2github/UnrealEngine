// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionConvexUtility.h"
#include "Chaos/Convex.h"
#include "GeometryCollection/GeometryCollection.h"


FGeometryCollectionConvexUtility::FGeometryCollectionConvexData FGeometryCollectionConvexUtility::GetValidConvexHullData(FGeometryCollection* GeometryCollection)
{
	check (GeometryCollection)

	if (!GeometryCollection->HasGroup("Convex"))
	{
		GeometryCollection->AddGroup("Convex");
	}

	if (!GeometryCollection->HasAttribute("TransformToConvexIndex", FTransformCollection::TransformGroup))
	{
		FManagedArrayCollection::FConstructionParameters ConvexDependency("Convex");
		TManagedArray<int32>& IndexArray = GeometryCollection->AddAttribute<int32>("TransformToConvexIndex", FTransformCollection::TransformGroup, ConvexDependency);
		IndexArray.Fill(INDEX_NONE);
	}

	if (!GeometryCollection->HasAttribute("ConvexHull", "Convex"))
	{
		GeometryCollection->AddAttribute<TUniquePtr<Chaos::FConvex>>("ConvexHull", "Convex");
	}

	// Check for correct population. All (and only) rigid nodes should have a convex associated.
	const TManagedArray<int32>& SimulationType = GeometryCollection->GetAttribute<int32>("SimulationType", FTransformCollection::TransformGroup);
	const TManagedArray<int32>& TransformToGeometryIndex = GeometryCollection->GetAttribute<int32>("TransformToGeometryIndex", FTransformCollection::TransformGroup);
	TManagedArray<int32>& TransformToConvexIndex = GeometryCollection->GetAttribute<int32>("TransformToConvexIndex", FTransformCollection::TransformGroup);
	TManagedArray<TUniquePtr<Chaos::FConvex>>& ConvexHull = GeometryCollection->GetAttribute<TUniquePtr<Chaos::FConvex>>("ConvexHull", "Convex");

	TArray<int32> ProduceConvexHulls;
	ProduceConvexHulls.Reserve(SimulationType.Num());
	TArray<int32> EliminateConvexHulls;
	EliminateConvexHulls.Reserve(SimulationType.Num());

	for (int32 Idx = 0; Idx < SimulationType.Num(); ++Idx)
	{
		if ((SimulationType[Idx] == FGeometryCollection::ESimulationTypes::FST_Rigid) && (TransformToConvexIndex[Idx] == INDEX_NONE))
		{
			ProduceConvexHulls.Add(Idx);
			
		}
		else if ((SimulationType[Idx] != FGeometryCollection::ESimulationTypes::FST_Rigid) && (TransformToConvexIndex[Idx] > INDEX_NONE))
		{
			EliminateConvexHulls.Add(Idx);
		}
	}

	if (EliminateConvexHulls.Num())
	{
		GeometryCollection->RemoveElements("Convex", EliminateConvexHulls);
		for (int32 Idx : EliminateConvexHulls)
		{
			TransformToConvexIndex[Idx] = INDEX_NONE;
		}
	}

	if (ProduceConvexHulls.Num())
	{
		int32 NewConvexIndexStart = GeometryCollection->AddElements(ProduceConvexHulls.Num(), "Convex");
		for (int32 Idx = 0; Idx < ProduceConvexHulls.Num(); ++Idx)
		{
			int32 GeometryIdx = TransformToGeometryIndex[ProduceConvexHulls[Idx]];
			ConvexHull[NewConvexIndexStart + Idx] = FindConvexHull(GeometryCollection, GeometryIdx);
			TransformToConvexIndex[ProduceConvexHulls[Idx]] = NewConvexIndexStart + Idx;
		}
	}

	return { TransformToConvexIndex, ConvexHull };
	
}


TUniquePtr<Chaos::FConvex> FGeometryCollectionConvexUtility::FindConvexHull(const FGeometryCollection* GeometryCollection, int32 GeometryIndex)
{
	check(GeometryCollection);

	int32 VertexCount = GeometryCollection->VertexCount[GeometryIndex];
	int32 VertexStart = GeometryCollection->VertexStart[GeometryIndex];

	TArray<Chaos::FVec3> Vertices;
	Vertices.SetNum(VertexCount);
	for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		Vertices[VertexIndex] = GeometryCollection->Vertex[VertexStart+VertexIndex];
	}

	return MakeUnique<Chaos::FConvex>(Vertices, 0.0f);
}


void FGeometryCollectionConvexUtility::RemoveConvexHulls(FGeometryCollection* GeometryCollection, const TArray<int32>& SortedTransformDeletes)
{
	if (GeometryCollection->HasGroup("Convex") && GeometryCollection->HasAttribute("TransformToConvexIndex", FTransformCollection::TransformGroup))
	{
		TManagedArray<int32>& TransformToConvexIndex = GeometryCollection->GetAttribute<int32>("TransformToConvexIndex", FTransformCollection::TransformGroup);
		TArray<int32> ConvexIndices;
		for (int32 TransformIdx : SortedTransformDeletes)
		{
			if (TransformToConvexIndex[TransformIdx] > INDEX_NONE)
			{
				ConvexIndices.Add(TransformToConvexIndex[TransformIdx]);
				TransformToConvexIndex[TransformIdx] = INDEX_NONE;
			}
		}

		if (ConvexIndices.Num())
		{
			ConvexIndices.Sort();
			GeometryCollection->RemoveElements("Convex", ConvexIndices);
		}
	}
}


void FGeometryCollectionConvexUtility::SetDefaults(FGeometryCollection* GeometryCollection, FName Group, uint32 StartSize, uint32 NumElements)
{
	if (Group == FTransformCollection::TransformGroup)
	{
		if (GeometryCollection->HasAttribute("TransformToConvexIndex", FTransformCollection::TransformGroup))
		{
			TManagedArray<int32>& TransformToConvexIndex = GeometryCollection->GetAttribute<int32>("TransformToConvexIndex", FTransformCollection::TransformGroup);

			for (uint32 Idx = StartSize; Idx < StartSize + NumElements; ++Idx)
			{
				TransformToConvexIndex[Idx] = INDEX_NONE;
			}
		}
	}
}