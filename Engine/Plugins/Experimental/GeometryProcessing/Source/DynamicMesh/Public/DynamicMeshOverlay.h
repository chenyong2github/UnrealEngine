// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "Util/DynamicVector.h"
#include "Util/RefCountVector.h"
#include "Util/CompactMaps.h"
#include "VectorTypes.h"
#include "GeometryTypes.h"
#include "InfoTypes.h"

class FDynamicMesh3;

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
private:
	/** @set the parent mesh for this overlay.  Only safe for use during FDynamicMesh move */
	void Reparent(FDynamicMesh3* ParentMeshIn)
	{
		ParentMesh = ParentMeshIn;
	}
public:
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

	/** Copy the Copy overlay to a compact rep, also updating parent references based on the CompactMaps */
	void CompactCopy(const FCompactMaps& CompactMaps, const TDynamicMeshOverlay<RealType, ElementSize>& Copy)
	{
		ClearElements();

		// map of element IDs
		TArray<int> MapE; MapE.SetNumUninitialized(Copy.MaxElementID());

		// copy elements across
		RealType Data[ElementSize];
		for (int EID = 0; EID < Copy.MaxElementID(); EID++)
		{
			if (Copy.IsElement(EID))
			{
				Copy.GetElement(EID, Data);
				MapE[EID] = AppendElement(Data, CompactMaps.MapV[Copy.ParentVertices[EID]]);
			}
			else
			{
				MapE[EID] = -1;
			}
		}

		// copy triangles across
		check(CompactMaps.bKeepTriangleMap && CompactMaps.MapT.Num() == Copy.GetParentMesh()->MaxTriangleID()); // must have valid triangle map
		for (int FromTID : Copy.GetParentMesh()->TriangleIndicesItr())
		{
			if (!Copy.IsSetTriangle(FromTID))
			{
				continue;
			}
			int ToTID = CompactMaps.MapT[FromTID];
			FIndex3i FromTriElements = Copy.GetTriangle(FromTID);
			SetTriangle(ToTID, FIndex3i(MapE[FromTriElements.A], MapE[FromTriElements.B], MapE[FromTriElements.C]));
		}
	}

	/** Compact overlay and update links to parent based on CompactMaps */
	void CompactInPlace(const FCompactMaps& CompactMaps)
	{
		int iLastE = MaxElementID() - 1, iCurE = 0;
		while (iLastE >= 0 && ElementsRefCounts.IsValidUnsafe(iLastE) == false)
		{
			iLastE--;
		}
		while (iCurE < iLastE && ElementsRefCounts.IsValidUnsafe(iCurE) == false)
		{
			iCurE++;
		}

		// make a map to track element index changes, to use to update element triangles later
		// TODO: it may be faster to not construct this and to do the remapping per element as we go (by iterating the one ring of each parent vertex for each element)
		TArray<int> MapE; MapE.SetNumUninitialized(MaxElementID());
		for (int ID = 0; ID < MapE.Num(); ID++)
		{
			// mapping is 1:1 by default; sparsely re-mapped below
			MapE[ID] = ID;
		}

		TDynamicVector<short>& ERef = ElementsRefCounts.GetRawRefCountsUnsafe();
		RealType Data[ElementSize];
		while (iCurE < iLastE)
		{
			// remap the element data
			GetElement(iLastE, Data);
			SetElement(iCurE, Data);
			int OrigParent = ParentVertices[iLastE];
			ParentVertices[iCurE] = CompactMaps.GetVertex(OrigParent);
			ERef[iCurE] = ERef[iLastE];
			ERef[iLastE] = FRefCountVector::INVALID_REF_COUNT;
			MapE[iLastE] = iCurE;

			// move cur forward one, last back one, and  then search for next valid
			iLastE--; iCurE++;
			while (iLastE >= 0 && ElementsRefCounts.IsValidUnsafe(iLastE) == false)
			{
				iLastE--;
			}
			while (iCurE < iLastE && ElementsRefCounts.IsValidUnsafe(iCurE))
			{
				iCurE++;
			}
		}
		ElementsRefCounts.Trim(ElementCount());
		Elements.Resize(ElementCount() * ElementSize);
		ParentVertices.Resize(ElementCount());

		for (int TID = 0, OldMaxTID = ElementTriangles.Num() / 3; TID < OldMaxTID; TID++)
		{
			int OldStart = TID * 3;
			if (ElementTriangles[OldStart] == -1)
			{
				continue; // triangle was not set; skip it
			}
			int NewStart = CompactMaps.GetTriangle(TID) * 3;
			for (int SubIdx = 0; SubIdx < 3; SubIdx++)
			{
				ElementTriangles[NewStart + SubIdx] = MapE[ElementTriangles[OldStart + SubIdx]];
			}
		}

		checkSlow(IsCompact());
	}

	/** Discard current set of elements, but keep triangles */
	void ClearElements();

	/** @return the number of in-use Elements in the overlay */
	int ElementCount() const { return (int)ElementsRefCounts.GetCount(); }
	/** @return the maximum element index in the overlay. This may be larger than the count if Elements have been deleted. */
	int MaxElementID() const { return (int)ElementsRefCounts.GetMaxIndex(); }
	/** @return true if this element index is in use */
	inline bool IsElement(int vID) const { return ElementsRefCounts.IsValid(vID); }

	/** @return true if the elements are compact */
	bool IsCompact() const { return ElementsRefCounts.IsDense(); }


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

	/** @return true if this triangle was set */
	bool IsSetTriangle(int TID) const { return ElementTriangles[3 * TID] >= 0; }


	/**
	 * Build overlay topology from a predicate function, e.g. to build topology for sharp normals
	 * 
	 * @param TrisCanShareVertexPredicate Indicator function returns true if the given vertex can be shared for the given pair of triangles
	 *									  Note if a vertex can be shared between tris A and B, and B and C, it will be shared between all three
	 * @param InitElementValue Initial element value, copied into all created elements
	 */
	void CreateFromPredicate(TFunctionRef<bool(int ParentVertexIdx, int TriIDA, int TriIDB)> TrisCanShareVertexPredicate, RealType InitElementValue);

	/**
	 * Refine an existing overlay topology.  For any element on a given triangle, if the predicate returns true, it gets topologically split out so it isn't shared by any other triangle.
	 * Used for creating sharp vertices in the normals overlay.
	 *
	 * @param ShouldSplitOutVertex predicate returns true of the element should be split out and not shared w/ any other triangle
	 * @param GetNewElementValue function to assign a new value to any element that is split out
	 */
	void SplitVerticesWithPredicate(TFunctionRef<bool(int ElementIdx, int TriID)> ShouldSplitOutVertex, TFunctionRef<void(int ElementIdx, int TriID, RealType* FillVect)> GetNewElementValue);


	/**
	 * Create a new copy of ElementID, and update connected triangles in the TrianglesToUpdate array to reference the copy of ElementID where they used to reference ElementID
	 * (Note: This just calls "SplitElementWithNewParent" with the existing element's parent id.)
	 *
	 * @param ElementID the element to copy
	 * @param TrianglesToUpdate the triangles that should now reference the new element
	 * @return the ID of the newly created element
	 */
	int SplitElement(int ElementID, const TArrayView<const int>& TrianglesToUpdate);

	/**
	* Create a new copy of ElementID, and update connected triangles in the TrianglesToUpdate array to reference the copy of ElementID where they used to reference ElementID.  The new element will have the given parent vertex ID.
	*
	* @param ElementID the element to copy
	* @param SplitParentVertexID the new parent vertex for copied elements
	* @param TrianglesToUpdate the triangles that should now reference the new element.  Note: this is allowed to include triangles that do not have the element at all; sometimes you may want to do so to avoid creating a new array for each call.
	* @return the ID of the newly created element
	*/
	int SplitElementWithNewParent(int ElementID, int SplitParentVertexID, const TArrayView<const int>& TrianglesToUpdate);


	/**
	* Refine an existing overlay topology by splitting any bow ties
	*/
	void SplitBowties();


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
	bool IsSeamVertex(int VertexID, bool bBoundaryIsSeam = true) const;

	/** @return true if the two triangles are connected, ie shared edge exists and is not a seam edge */
	bool AreTrianglesConnected(int TriangleID0, int TriangleID1) const;

	/** find the elements associated with a given parent-mesh vertex */
	void GetVertexElements(int VertexID, TArray<int>& OutElements) const;
	/** Count the number of unique elements for a given parent-mesh vertex */
	int CountVertexElements(int VertexID, bool bBruteForce = false) const;

	/** find the triangles connected to an element */
	void GetElementTriangles(int ElementID, TArray<int>& OutTriangles) const;

	/** Currently an iteration every time */
	bool HasInteriorSeamEdges() const;

	/**
	 * Checks that the overlay mesh is well-formed, ie all internal data structures are consistent
	 */
	bool CheckValidity(bool bAllowNonManifoldVertices = true, EValidityCheckFailMode FailMode = EValidityCheckFailMode::Check) const;


public:
	/** Set a triangle's element indices to InvalidID */
	void InitializeNewTriangle(int TriangleID);
	/** Remove a triangle from the overlay */
	void OnRemoveTriangle(int TriangleID);
	/** Reverse the orientation of a triangle's elements */
	void OnReverseTriOrientation(int TriangleID);
	/** Update the overlay to reflect an edge split in the parent mesh */
	void OnSplitEdge(const DynamicMeshInfo::FEdgeSplitInfo& SplitInfo);
	/** Update the overlay to reflect an edge flip in the parent mesh */
	void OnFlipEdge(const DynamicMeshInfo::FEdgeFlipInfo& FlipInfo);
	/** Update the overlay to reflect an edge collapse in the parent mesh */
	void OnCollapseEdge(const DynamicMeshInfo::FEdgeCollapseInfo& CollapseInfo);
	/** Update the overlay to reflect a face poke in the parent mesh */
	void OnPokeTriangle(const DynamicMeshInfo::FPokeTriangleInfo& PokeInfo);
	/** Update the overlay to reflect an edge merge in the parent mesh */
	void OnMergeEdges(const DynamicMeshInfo::FMergeEdgesInfo& MergeInfo);
	/** Update the overlay to reflect a vertex split in the parent mesh */
	void OnSplitVertex(const DynamicMeshInfo::FVertexSplitInfo& SplitInfo, const TArrayView<const int>& TrianglesToUpdate);

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


