// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ManagedArray.h"
#include "GeometryCollection/TransformCollection.h"

class FGeometryCollection;

namespace GeometryCollectionAlgo
{
	struct FFaceEdge
	{
		int32 VertexIdx1;
		int32 VertexIdx2;

		friend inline uint32 GetTypeHash(const FFaceEdge& Other)
		{
			return HashCombine(::GetTypeHash(Other.VertexIdx1), ::GetTypeHash(Other.VertexIdx2));
		}

		friend bool operator==(const FFaceEdge& A, const FFaceEdge& B)
		{
			return A.VertexIdx1 == B.VertexIdx1 && A.VertexIdx2 == B.VertexIdx2;
		}
	};

	/*
	* Print the parent hierarchy of the collection.
	*/
	void 
	GEOMETRYCOLLECTIONCORE_API 
	PrintParentHierarchy(const FGeometryCollection * Collection);

	/**
	* Build a contiguous array of integers
	*/
	void
	GEOMETRYCOLLECTIONCORE_API
	ContiguousArray(TArray<int32> & Array, int32 Length);
		
	/**
	* Offset list for re-incrementing deleted elements.
	*/
	void 
	GEOMETRYCOLLECTIONCORE_API
	BuildIncrementMask(const TArray<int32> & SortedDeletionList, const int32 & Size, TArray<int32> & Mask);

	/**
	*
	*/
	void 
	GEOMETRYCOLLECTIONCORE_API
	BuildLookupMask(const TArray<int32> & SortedDeletionList, const int32 & Size, TArray<bool> & Mask);


	/*
	*
	*/
	void
	GEOMETRYCOLLECTIONCORE_API
	BuildTransformGroupToGeometryGroupMap(const FGeometryCollection& GeometryCollection, TArray<int32> & TransformToGeometry);


	/*
	*
	*/
	void
	GEOMETRYCOLLECTIONCORE_API
	BuildFaceGroupToGeometryGroupMap(const FGeometryCollection& GeometryCollection, const TArray<int32>& TransformToGeometryMap, TArray<int32> & FaceToGeometry);


	/**
	* Make sure the deletion list is correctly formed.
	*/
	void 
	GEOMETRYCOLLECTIONCORE_API
	ValidateSortedList(const TArray<int32>&SortedDeletionList, const int32 & ListSize);

	/*
	*  Check if the Collection has multiple transform roots.
	*/
	bool 
	GEOMETRYCOLLECTIONCORE_API 
	HasMultipleRoots(FGeometryCollection * Collection);

	/*
	*
	*/
	bool 
	GEOMETRYCOLLECTIONCORE_API
	HasCycle(TManagedArray<int32>& Parents, int32 Node);

	/*
	*
	*/
	bool 
	GEOMETRYCOLLECTIONCORE_API
	HasCycle(TManagedArray<int32>& Parents, const TArray<int32>& SelectedBones);

	/*
	* Parent a single transform
	*/
	void
	GEOMETRYCOLLECTIONCORE_API 
	ParentTransform(FTransformCollection* GeometryCollection, const int32 TransformIndex, const int32 ChildIndex);
		
	/*
	*  Parent the list of transforms to the selected index. 
	*/
	void 
	GEOMETRYCOLLECTIONCORE_API 
	ParentTransforms(FTransformCollection* GeometryCollection, const int32 TransformIndex, const TArray<int32>& SelectedBones);

	/*
	*  Find the average position of the transforms.
	*/
	FVector
	GEOMETRYCOLLECTIONCORE_API
	AveragePosition(FGeometryCollection* Collection, const TArray<int32>& Indices);


	/*
	*  Global Matrices of the specified index.
	*/
	FTransform GEOMETRYCOLLECTIONCORE_API GlobalMatrix(const TManagedArray<FTransform>& RelativeTransforms, const TManagedArray<int32>& Parents, int32 Index);


	/*
	*  Global Matrices of the collection based on list of indices
	*/
	void GEOMETRYCOLLECTIONCORE_API GlobalMatrices(const TManagedArray<FTransform>& RelativeTransforms, const TManagedArray<int32>& Parents, const TArray<int32>& Indices, TArray<FTransform>& Transforms);

	/*
	 *  Recursively traverse from a root node down
	 */
	void GEOMETRYCOLLECTIONCORE_API GlobalMatricesFromRoot(const int32 ParentTransformIndex, const TManagedArray<FTransform>& RelativeTransforms, const TManagedArray<TSet<int32>>& Children, TArray<FMatrix>& Transforms);

	/*
	*  Global Matrices of the collection, transforms will be resized to fit
	*/
	template<typename MatrixType>
	void GEOMETRYCOLLECTIONCORE_API GlobalMatrices(const TManagedArray<FTransform>& RelativeTransforms, const TManagedArray<int32>& Parents, TArray<MatrixType>& Transforms);

	/*
	*  Gets pairs of elements whose bounding boxes overlap.
	*/
	void GEOMETRYCOLLECTIONCORE_API GetOverlappedPairs(FGeometryCollection* Collection, int Level, TSet<TTuple<int32, int32>>& OutOverlappedPairs);
	
	/*
	*  Prepare for simulation - placeholder function
	*/
	void 
	GEOMETRYCOLLECTIONCORE_API
	PrepareForSimulation(FGeometryCollection* GeometryCollection, bool CenterAtOrigin=true);

	/*
	*  Moves the geometry to center of mass aligned, with option to re-center bones around origin of actor
	*/
	void
	GEOMETRYCOLLECTIONCORE_API
	ReCenterGeometryAroundCentreOfMass(FGeometryCollection* GeometryCollection, bool CenterAtOrigin = true);

	void
	GEOMETRYCOLLECTIONCORE_API
	FindOpenBoundaries(const FGeometryCollection* GeometryCollection, const float CoincidentVertexTolerance, TArray<TArray<TArray<int32>>> &BoundaryVertexIndices);

	void
	GEOMETRYCOLLECTIONCORE_API
	TriangulateBoundaries(FGeometryCollection* GeometryCollection, const TArray<TArray<TArray<int32>>> &BoundaryVertexIndices, bool bWoundClockwise = true, float MinTriangleAreaSq = 1e-4);

	void
	GEOMETRYCOLLECTIONCORE_API
	AddFaces(FGeometryCollection* GeometryCollection, const TArray<TArray<FIntVector>> &Faces);

	void
	GEOMETRYCOLLECTIONCORE_API
	ComputeCoincidentVertices(const FGeometryCollection* GeometryCollection, const float Tolerance, TMap<int32, int32>& CoincidentVerticesMap, TSet<int32>& VertexToDeleteSet);

	void
	GEOMETRYCOLLECTIONCORE_API
	DeleteCoincidentVertices(FGeometryCollection* GeometryCollection, float Tolerance = 1e-2);

	void
	GEOMETRYCOLLECTIONCORE_API
	ComputeZeroAreaFaces(const FGeometryCollection* GeometryCollection, const float Tolerance, TSet<int32>& FaceToDeleteSet);

	void
	GEOMETRYCOLLECTIONCORE_API
	DeleteZeroAreaFaces(FGeometryCollection* GeometryCollection, float Tolerance = 1e-4);

	void
	GEOMETRYCOLLECTIONCORE_API
	ComputeHiddenFaces(const FGeometryCollection* GeometryCollection, TSet<int32>& FaceToDeleteSet);

	void
	GEOMETRYCOLLECTIONCORE_API
	DeleteHiddenFaces(FGeometryCollection* GeometryCollection);

	void
	GEOMETRYCOLLECTIONCORE_API
	ComputeStaleVertices(const FGeometryCollection* GeometryCollection, TSet<int32>& VertexToDeleteSet);

	void
	GEOMETRYCOLLECTIONCORE_API
	DeleteStaleVertices(FGeometryCollection* GeometryCollection);

	void
	GEOMETRYCOLLECTIONCORE_API
	ComputeEdgeInFaces(const FGeometryCollection* GeometryCollection, TMap<FFaceEdge, int32>& FaceEdgeMap);

	void
	GEOMETRYCOLLECTIONCORE_API
	PrintStatistics(const FGeometryCollection* GeometryCollection);

	/*
	* Geometry validation - Checks if the geometry group faces ranges fall within the size of the faces group
	*/
	bool
	GEOMETRYCOLLECTIONCORE_API
	HasValidFacesFor(const FGeometryCollection* GeometryCollection, int32 GeometryIndex);

	/*
	* Geometry validation - Checks if the geometry group verts ranges fall within the size of the vertices group
	*/
	bool
	GEOMETRYCOLLECTIONCORE_API
	HasValidIndicesFor(const FGeometryCollection* GeometryCollection, int32 GeometryIndex);

	/*
	* Geometry validation - Checks if the geometry group indices appear out of range
	*/
	bool
	GEOMETRYCOLLECTIONCORE_API
	HasInvalidIndicesFor(const FGeometryCollection* GeometryCollection, int32 GeometryIndex);

	/*
	* Geometry validation - Checks if there are any faces that are not referenced by the geometry groups
	*/
	bool
	GEOMETRYCOLLECTIONCORE_API
	HasResidualFaces(const FGeometryCollection* GeometryCollection);

	/*
	* Geometry validation - Checks if there are any vertices that are not referenced by the geometry groups
	*/
	bool
	GEOMETRYCOLLECTIONCORE_API
	HasResidualIndices(const FGeometryCollection* GeometryCollection);

	/*
	* Performs all of the above geometry validation checks
	*/
	bool
	GEOMETRYCOLLECTIONCORE_API
	HasValidGeometryReferences(const FGeometryCollection* GeometryCollection);

}
