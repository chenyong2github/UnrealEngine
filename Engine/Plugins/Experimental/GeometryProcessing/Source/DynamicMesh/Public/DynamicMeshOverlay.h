// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "Util/DynamicVector.h"
#include "Util/RefCountVector.h"
#include "VectorTypes.h"
#include "GeometryTypes.h"
#include "DynamicMesh3.h"


/**
 * TDynamicMeshOverlay is an add-on to a FDynamicMesh3 that allows for per-triangle storage
 * of an "element" (eg like a per-triangle UV or normal). However the elements can be shared
 * between triangles because they are stored in a separate indexable list. As a result,
 * the overlay has it's own topology, IE there may be "seam" boundary edges in the 
 * overlay topology that are not mesh boundary edges in the associated/parent mesh.
 *
 * A "seam" edge is one where at least one of the elements of the triangles on either
 * side of the edge is not shared between the two triangles.
 *
 * The FDynamicMesh3 mesh topology operations (eg split/flip/collapse edge, poke face, etc) 
 * can be mirrored to the overlay via OnSplitEdge(), etc. 
 *
 * Note that although this is a template, many of the functions are defined in the .cpp file.
 * As a result you need to explicitly instantiate and export the instance of the template that 
 * you wish to use in the block at the top of DynamicMeshOverlay.cpp
 */
template<typename RealType, int ElementSize>
class TDynamicMeshOverlay
{

protected:
	/** The parent mesh this overlay belongs to */
	FDynamicMesh3* ParentMesh;

	/** Reference counts of element indices. Iterate over this to find out which elements are valid. */
	FRefCountVector ElementsRefCounts;
	/** List of element values */
	TDynamicVector<RealType> Elements;
	/** List of parent vertex indices, one per element */
	TDynamicVector<int> ParentVertices;

	/** List of triangle element-index triplets [Elem0 Elem1 Elem2]*/
	TDynamicVector<int> ElementTriangles;

	friend class FDynamicMesh3;
	friend class FDynamicMeshAttributeSet;

public:
	/** Create an empty overlay */
	TDynamicMeshOverlay()
	{
		ParentMesh = nullptr;
	}

	/** Create an overlay for the given parent mesh */
	TDynamicMeshOverlay(FDynamicMesh3* ParentMeshIn)
	{
		ParentMesh = ParentMeshIn;
	}

	/** @return the parent mesh for this overlay */
	const FDynamicMesh3* GetParentMesh() const { return ParentMesh; }
	/** @return the parent mesh for this overlay */
	FDynamicMesh3* GetParentMesh() { return ParentMesh; }

	/** Set this overlay to contain the same arrays as the copy overlay */
	void Copy(const TDynamicMeshOverlay<RealType,ElementSize>& Copy)
	{
		ElementsRefCounts = FRefCountVector(Copy.ElementsRefCounts);
		Elements = Copy.Elements;
		ParentVertices = Copy.ParentVertices;
		ElementTriangles = Copy.ElementTriangles;
	}

	/** Discard current set of elements, but keep triangles */
	void ClearElements();

	/** @return the number of in-use Elements in the overlay */
	int ElementCount() const { return (int)ElementsRefCounts.GetCount(); }
	/** @return the maximum element index in the overlay. This may be larger than the count if Elements have been deleted. */
	int MaxElementID() const { return (int)ElementsRefCounts.GetMaxIndex(); }
	/** @return true if this element index is in use */
	inline bool IsElement(int vID) const { return ElementsRefCounts.IsValid(vID); }


	typedef typename FRefCountVector::IndexEnumerable element_iterator;

	/** @return enumerator for valid element indices suitable for use with range-based for */
	element_iterator ElementIndicesItr() const { return ElementsRefCounts.Indices(); }


	/** Allocate a new element with the given constant value and parent-mesh vertex */
	int AppendElement(RealType ConstantValue, int ParentVertex);
	/** Allocate a new element with the given value and parent-mesh vertex */
	int AppendElement(const RealType* Value, int ParentVertex);

	/** Initialize the triangle list to the given size, and set all triangles to InvalidID */
	void InitializeTriangles(int MaxTriangleID);
	/** Set the triangle to the given Element index tuple, and increment element reference counts */
	EMeshResult SetTriangle(int TriangleID, const FIndex3i& TriElements);



	//
	// Support for inserting element at specific ID. This is a bit tricky
	// because we likely will need to update the free list in the RefCountVector, which
	// can be expensive. If you are going to do many inserts (eg inside a loop), wrap in
	// BeginUnsafe / EndUnsafe calls, and pass bUnsafe = true to the InsertElement() calls,
	// to the defer free list rebuild until you are done.
	//

	/** Call this before a set of unsafe InsertVertex() calls */
	void BeginUnsafeElementsInsert()
	{
		// do nothing...
	}

	/** Call after a set of unsafe InsertVertex() calls to rebuild free list */
	void EndUnsafeElementsInsert()
	{
		ElementsRefCounts.RebuildFreeList();
	}

	/**
	 * Insert element at given index, assuming it is unused.
	 * If bUnsafe, we use fast id allocation that does not update free list.
	 * You should only be using this between BeginUnsafeElementsInsert() / EndUnsafeElementsInsert() calls
	 */
	EMeshResult InsertElement(int ElementID, const RealType* Value, int ParentVertex, bool bUnsafe = false);


	//
	// Accessors/Queries
	//  


	/** Get the element at a given index */
	inline void GetElement(int ElementID, RealType* Data) const
	{
		int k = ElementID * ElementSize;
		for (int i = 0; i < ElementSize; ++i) 
		{
			Data[i] = Elements[k + i];
		}
	}

	/** Get the element at a given index */
	template<typename AsType>
	void GetElement(int ElementID, AsType& Data) const
	{
		int k = ElementID * ElementSize;
		for (int i = 0; i < ElementSize; ++i) 
		{
			Data[i] = Elements[k + i];
		}
	}


	/** Get the parent vertex id for the element at a given index */
	inline int GetParentVertex(int ElementID) const
	{
		return ParentVertices[ElementID];
	}


	/** Get the element index tuple for a triangle */
	inline FIndex3i GetTriangle(int TriangleID) const 
	{
		int i = 3 * TriangleID;
		return FIndex3i(ElementTriangles[i], ElementTriangles[i + 1], ElementTriangles[i + 2]);
	}


	/** Set the element at a given index */
	inline void SetElement(int ElementID, const RealType* Data)
	{
		int k = ElementID * ElementSize;
		for (int i = 0; i < ElementSize; ++i)
		{
			Elements[k + i] = Data[i];
		}
	}

	/** Set the element at a given index */
	template<typename AsType>
	void SetElement(int ElementID, const AsType& Data)
	{
		int k = ElementID * ElementSize;
		for (int i = 0; i < ElementSize; ++i)
		{
			Elements[k + i] = Data[i];
		}
	}

	/** @return true if triangle contains element */
	inline bool TriangleHasElement(int TriangleID, int ElementID) const
	{
		int i = 3 * TriangleID;
		return (ElementTriangles[i] == ElementID || ElementTriangles[i+1] == ElementID || ElementTriangles[i+2] == ElementID);
	}


	/** Returns true if the parent-mesh edge is a "Seam" in this overlay */
	bool IsSeamEdge(int EdgeID) const;
	/** Returns true if the parent-mesh vertex is connected to any seam edges */
	bool IsSeamVertex(int VertexID) const;

	/** find the elements associated with a given parent-mesh vertex */
	void GetVertexElements(int VertexID, TArray<int>& OutElements) const;
	/** Count the number of unique elements for a given parent-mesh vertex */
	int CountVertexElements(int VertexID, bool bBruteForce = false) const;

	/** find the triangles connected to an element */
	void GetElementTriangles(int ElementID, TArray<int>& OutTriangles) const;

	/**
	 * Checks that the overlay mesh is well-formed, ie all internal data structures are consistent
	 */
	bool CheckValidity(bool bAllowNonManifoldVertices = true, EValidityCheckFailMode FailMode = EValidityCheckFailMode::Check) const;


public:
	/** Set a triangle's element indices to InvalidID */
	void InitializeNewTriangle(int TriangleID);
	/** Remove a triangle from the overlay */
	void OnRemoveTriangle(int TriangleID, bool bRemoveIsolatedVertices);
	/** Reverse the orientation of a triangle's elements */
	void OnReverseTriOrientation(int TriangleID);
	/** Update the overlay to reflect an edge split in the parent mesh */
	void OnSplitEdge(const FDynamicMesh3::FEdgeSplitInfo& SplitInfo);
	/** Update the overlay to reflect an edge flip in the parent mesh */
	void OnFlipEdge(const FDynamicMesh3::FEdgeFlipInfo& FlipInfo);
	/** Update the overlay to reflect an edge collapse in the parent mesh */
	void OnCollapseEdge(const FDynamicMesh3::FEdgeCollapseInfo& CollapseInfo);
	/** Update the overlay to reflect a face poke in the parent mesh */
	void OnPokeTriangle(const FDynamicMesh3::FPokeTriangleInfo& PokeInfo);
	/** Update the overlay to reflect an edge merge in the parent mesh */
	void OnMergeEdges(const FDynamicMesh3::FMergeEdgesInfo& MergeInfo);

protected:
	/** Set the value at an Element to be a linear interpolation of two other Elements */
	void SetElementFromLerp(int SetElement, int ElementA, int ElementB, RealType Alpha);
	/** Set the value at an Element to be a barycentric interpolation of three other Elements */
	void SetElementFromBary(int SetElement, int ElementA, int ElementB, int ElementC, const FVector3<RealType>& BaryCoords);

	/** updates the triangles array and optionally the element reference counts */
	void InternalSetTriangle(int TriangleID, const FIndex3i& TriElements, bool bIncrementRefCounts);
};






/**
 * TDynamicMeshVectorOverlay is an convenient extension of TDynamicMeshOverlay that adds
 * a specific N-element Vector type to the template, and adds accessor functions
 * that convert between that N-element vector type and the N-element arrays used by TDynamicMeshOverlay.
 */
template<typename RealType, int ElementSize, typename VectorType>
class TDynamicMeshVectorOverlay : public TDynamicMeshOverlay<RealType, ElementSize>
{
public:
	using BaseType = TDynamicMeshOverlay<RealType, ElementSize>;

	TDynamicMeshVectorOverlay()
		: TDynamicMeshOverlay<RealType, ElementSize>()
	{
	}

	TDynamicMeshVectorOverlay(FDynamicMesh3* parentMesh) 
		: TDynamicMeshOverlay<RealType, ElementSize>(parentMesh)
	{
	}

	/**
	 * Append a new Element to the overlay
	 */
	inline int AppendElement(const VectorType& Value, int SourceVertex)
	{
		return BaseType::AppendElement((const RealType*)Value, SourceVertex);
	}

	/**
	 * Get Element at a specific ID
	 */
	inline VectorType GetElement(int ElementID) const
	{
		VectorType V;
		BaseType::GetElement(ElementID, V);
		return V;
	}

	/**
	 * Get Element at a specific ID
	 */
	inline void GetElement(int ElementID, VectorType& V) const
	{
		BaseType::GetElement(ElementID, V);
	}

	/**
	 * Get the three Elements associated with a triangle
	 */
	inline void GetTriElements(int TriangleID, VectorType& A, VectorType& B, VectorType& C) const
	{
		int i = 3 * TriangleID;
		GetElement(BaseType::ElementTriangles[i], A);
		GetElement(BaseType::ElementTriangles[i+1], B);
		GetElement(BaseType::ElementTriangles[i+2], C);
	}


	/**
	 * Set Element at a specific ID
	 */
	inline void SetElement(int ElementID, const VectorType& Value)
	{
		BaseType::SetElement(ElementID, Value);
	}
};


