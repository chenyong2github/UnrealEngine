// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryCollection/GeometryCollection.h"
#include "Templates/SharedPointer.h"
#include "Async/ParallelFor.h"

namespace GeometryCollection 
{

	/****
	* MakeCubeElement
	*   Utility to create a triangulated unit cube using the FGeometryCollection format.
	*/
	TSharedPtr<FGeometryCollection> 
	GEOMETRYCOLLECTIONCORE_API 
	MakeCubeElement(const FTransform& center, FVector Scale = FVector(1.f), int NumberOfMaterials = 2);

	/****
	* SetupCubeGridExample
	*   Utility to create a grid (10x10x10) of triangulated unit cube using the FGeometryCollection format.
	*/
	void 
	GEOMETRYCOLLECTIONCORE_API 
	SetupCubeGridExample(TSharedPtr<FGeometryCollection> GeometryCollection);


	/****
	* Setup Nested Hierarchy Example	
	*/
	void
	GEOMETRYCOLLECTIONCORE_API
	SetupNestedBoneCollection(FGeometryCollection * Collection);

	/****
	* Setup Two Clustered Cubes : 
	* ... geometry       { (-9,0,0) && (9,0,0)}
	* ... center of mass { (-10,0,0) && (10,0,0)}
	*/
	void
	GEOMETRYCOLLECTIONCORE_API
	SetupTwoClusteredCubesCollection(FGeometryCollection * Collection);


	/***
	* Add the geometry group to a collection. Mostly for backwards compatibility with older files. 
	*/
	void 
	GEOMETRYCOLLECTIONCORE_API 
	AddGeometryProperties(FGeometryCollection * Collection);

	/***
	* Ensure Material indices are setup correctly. Mostly for backwards compatibility with older files. 
	*/
	void 
	GEOMETRYCOLLECTIONCORE_API 
	MakeMaterialsContiguous(FGeometryCollection * Collection);

	/***
	* Transfers attributes from one collection to another based on the nearest vertex
	* #todo(dmp): We can add a lot of modes here, such as:
	* - transfer between different attribute groups
	* - derive attribute values based on different proximity based kernels
	*/
	template<class T>
	void
	AttributeTransfer(const FGeometryCollection * FromCollection, FGeometryCollection * ToCollection, const FName FromAttributeName, const FName ToAttributeName);
};

// AttributeTransfer implementation
template<class T>
void GeometryCollection::AttributeTransfer(const FGeometryCollection * FromCollection, FGeometryCollection * ToCollection, const FName FromAttributeName, const FName ToAttributeName)
{
	// #todo(dmp): later on we will support different attribute groups for transfer		
	const TManagedArray<T> &FromAttribute = FromCollection->GetAttribute<T>(FromAttributeName, FGeometryCollection::VerticesGroup);
	TManagedArray<T> &ToAttribute = ToCollection->GetAttribute<T>(ToAttributeName, FGeometryCollection::VerticesGroup);

	const TManagedArray<FVector> &FromVertex = FromCollection->Vertex;
	TManagedArray<FVector> &ToVertex = ToCollection->Vertex;

	// for each vertex in ToCollection, find the closest in FromCollection based on vertex position
	// #todo(dmp): should we be evaluating the transform hierarchy here, or just do it in local space?
	// #todo(dmp): use spatial hash rather than n^2 lookup
	ParallelFor(ToCollection->NumElements(FGeometryCollection::VerticesGroup), [&](int32 ToIndex)
	{
		int32 ClosestFromIndex = -1;
		float ClosestDist = MAX_FLT;
		for (int32 FromIndex = 0, ni = FromVertex.Num(); FromIndex < ni ; ++FromIndex)
		{
			float CurrDist = FVector::DistSquared(FromVertex[FromIndex], ToVertex[ToIndex]);
			if (CurrDist < ClosestDist)
			{
				ClosestDist = CurrDist;
				ClosestFromIndex = FromIndex;
			}
		}

		// If there is a valid position in FromCollection, transfer attribute
		if (ClosestFromIndex != -1)
		{
			ToAttribute[ToIndex] = FromAttribute[ClosestFromIndex];
		}
	});
}