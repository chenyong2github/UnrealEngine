// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh3.h"
#include "DynamicMeshOverlay.h"
#include "DynamicMeshAttributeSet.h"


//
// Implementation of mesh change tracking for FDynamicMesh3.
// 
// The top-level class is FDynamicMeshChangeTracker, found at the bottom of this file.
// You create an instance of this and then call BeginChange(), then call SaveVertex()/SaveTriangle()
// before you make modifications to a vertex/triangle, and then call EndChange() at the end.
// This function emits a FDynamicMeshChange instance, and you can call Apply() to apply/revert it.
// So this is the object you would store in a higher-level undo/redo record, for example.
// 
// Attribute overlays make everything more complicated of course. Handling of these
// follows a similar structure - the MeshChangeTracker creates a FDynamicMeshAttributeSetChangeTracker,
// which in turn creates a TDynamicMeshAttributeChange for each UV and Normal overlay
// (grouped together in a FDynamicMeshAttributeChangeSet). However you don't have to explicitly
// do anything to get Attribute support, if the initial Mesh had attributes, then
// this all happens automatically, and the attribute changes get attached to the FDynamicMeshChange.
// 
// @todo Currently the attribute support is hardcoded for UV (2-float) and Normal (3-float) overlays.
// Perhaps it would be possible to make this more flexible to avoid hardcoding these?
// Very little of the code depends on the element size or type.
// 
// 




/**
 * TDynamicMeshAttributeChange represents a change to an attribute overlay of a FDynamicMesh3.
 * @warning This class is meant to be used via FDynamicMeshChange and is not fully functional
 * on its own (see comments in ApplyReplaceChange)
 */
template<typename RealType, int ElementSize>
class DYNAMICMESH_API TDynamicMeshAttributeChange
{
public:
	void SaveInitialElement(const TDynamicMeshOverlay<RealType,ElementSize>* Overlay, int ElementID);
	void SaveInitialTriangle(const TDynamicMeshOverlay<RealType,ElementSize>* Overlay, int TriangleID);

	void StoreFinalElement(const TDynamicMeshOverlay<RealType, ElementSize>* Overlay, int ElementID);
	void StoreFinalTriangle(const TDynamicMeshOverlay<RealType, ElementSize>* Overlay, int TriangleID);

	bool Apply(TDynamicMeshOverlay<RealType, ElementSize>* Overlay, bool bRevert) const;

protected:
	struct FChangeElement
	{
		int ElementID;
		int DataIndex;
		int ParentVertexID;
	};

	struct FChangeTriangle
	{
		int TriangleID;
		FIndex3i Elements;
	};

	TArray<FChangeElement> OldElements;
	TArray<RealType> OldElementData;
	TArray<FChangeTriangle> OldTriangles;

	TArray<FChangeElement> NewElements;
	TArray<RealType> NewElementData;
	TArray<FChangeTriangle> NewTriangles;

	void ApplyReplaceChange(TDynamicMeshOverlay<RealType,ElementSize>* Overlay,
		const TArray<FChangeTriangle>& RemoveTris,
		const TArray<FChangeElement>& InsertElements,
		const TArray<RealType>& InsertElementData,
		const TArray<FChangeTriangle>& InsertTris) const;
};



/** Standard UV overlay change type - 2-element float */
typedef TDynamicMeshAttributeChange<float,2> FDynamicMeshUVChange;
/** Standard Normal overlay change type - 3-element float */
typedef TDynamicMeshAttributeChange<float,3> FDynamicMeshNormalChange;


/**
 * FDynamicMeshAttributeChangeSet stores a set of UV and Normal changes for a FDynamicMesh3
 */
class DYNAMICMESH_API FDynamicMeshAttributeChangeSet
{
public:
	TArray<FDynamicMeshUVChange> UVChanges;
	TArray<FDynamicMeshNormalChange> NormalChanges;

	/** call ::Apply() on all the UV and Normal changes */
	bool Apply(FDynamicMeshAttributeSet* Attributes, bool bRevert) const;
};


/**
 * FDynamicMeshChange stores a "change" in a FDynamicMesh3, which in this context
 * means the replacement of one set of triangles with a second set, that may have
 * different vertices/attributes. The change can be applied and reverted via ::Apply()
 * 
 * Construction of a well-formed FDynamicMeshChange is quite complex and it is strongly
 * suggested that you do so via FDynamicMeshChangeTracker
 */
class DYNAMICMESH_API FDynamicMeshChange
{
public:
	~FDynamicMeshChange();

	/** Store the initial state of a vertex */
	void SaveInitialVertex(const FDynamicMesh3* Mesh, int VertexID);
	/** Store the initial state of a triangle */
	void SaveInitialTriangle(const FDynamicMesh3* Mesh, int TriangleID);

	/** Store the final state of a vertex */
	void StoreFinalVertex(const FDynamicMesh3* Mesh, int VertexID);
	/** Store the final state of a triangle */
	void StoreFinalTriangle(const FDynamicMesh3* Mesh, int TriangleID);

	/** Attach an attribute change set to this mesh change, which will the be applied/reverted autoamtically */
	void AttachAttributeChanges(TUniquePtr<FDynamicMeshAttributeChangeSet> AttribChanges)
	{
		this->AttributeChanges = MoveTemp(AttribChanges);
	}

	/** Apply or Revert this change using the given Mesh */
	bool Apply(FDynamicMesh3* Mesh, bool bRevert) const;

	/** Do (limited) sanity checks on this MeshChange to ensure it is well-formed */
	void VerifySaveState() const;

	/** @return true if this vertex was saved. Uses linear search. */
	bool HasSavedVertex(int VertexID);

	/** store IDs of saved triangles in TrianglesOut. if bInitial=true, old triangles are stored, otherwise new triangles */
	void GetSavedTriangleList(TArray<int>& TrianglesOut, bool bInitial) const;

	/** run self-validity checks on internal data structures to test if change is well-formed */
	void CheckValidity(EValidityCheckFailMode FailMode = EValidityCheckFailMode::Check) const;

protected:

	struct FChangeVertex
	{
		int VertexID;
		FVertexInfo Info;
	};

	struct FChangeTriangle
	{
		int TriangleID;
		FIndex3i Vertices;
		FIndex3i Edges;
		int GroupID;
	};

	TArray<FChangeVertex> OldVertices;
	TArray<FChangeTriangle> OldTriangles;

	TArray<FChangeVertex> NewVertices;
	TArray<FChangeTriangle> NewTriangles;

	TUniquePtr<FDynamicMeshAttributeChangeSet> AttributeChanges;

	void ApplyReplaceChange(FDynamicMesh3* Mesh,
		const TArray<FChangeTriangle>& RemoveTris,
		const TArray<FChangeVertex>& InsertVerts,
		const TArray<FChangeTriangle>& InsertTris) const;

};







/**
 * FDynamicMeshAttributeSetChangeTracker constructs a well-formed set of TDynamicMeshAttributeChange
 * objects (stored in a FDynamicMeshAttributeChangeSet). You should not use this class
 * directly, it is intended to be used via FDynamicMeshChangeTracker
 */
class DYNAMICMESH_API FDynamicMeshAttributeSetChangeTracker
{
public:
	explicit FDynamicMeshAttributeSetChangeTracker(const FDynamicMeshAttributeSet* Attribs);

	/** Start tracking a change */
	void BeginChange();
	/** End the change transaction and get the resulting change object */
	TUniquePtr<FDynamicMeshAttributeChangeSet> EndChange();

	/** Store the initial state of a triangle */
	void SaveInitialTriangle(int TriangleID);

	/** store the final state of a set of triangles */
	void StoreAllFinalTriangles(const TArray<int>& TriangleIDs);


protected:
	const FDynamicMeshAttributeSet* Attribs = nullptr;

	FDynamicMeshAttributeChangeSet* Change = nullptr;

	struct FElementState
	{
		int MaxElementID;
		TBitArray<> StartElements;
		TBitArray<> ChangedElements;
	};
	TArray<FElementState> UVStates;
	TArray<FElementState> NormalStates;

	template<typename AttribOverlayType, typename AttribChangeType>
	void SaveElement(int ElementID, FElementState& State, const AttribOverlayType* Overlay, AttribChangeType& ChangeIn)
	{
		if (ElementID < State.MaxElementID && State.ChangedElements[ElementID] == false && State.StartElements[ElementID] == true)
		{
			ChangeIn.SaveInitialElement(Overlay, ElementID);
			State.ChangedElements[ElementID] = true;
		}
	}

};









/**
 * FDynamicMeshChangeTracker tracks changes to a FDynamicMesh and returns a
 * FDynamicMeshChange instance that represents this change and allows it to be reverted/reapplied.
 * This is the top-level class you likely want to use to track mesh changes.
 * 
 * Call BeginChange() before making any changes to the mesh, then call SaveVertex()
 * and SaveTriangle() before modifying the respective elements. Then call EndChange()
 * to construct a FDynamicMeshChange that represents the mesh delta.
 * 
 */
class DYNAMICMESH_API FDynamicMeshChangeTracker
{
public:
	explicit FDynamicMeshChangeTracker(const FDynamicMesh3* Mesh);
	~FDynamicMeshChangeTracker();

	/** Initialize the change-tracking process */
	void BeginChange();
	/** Construct a change object that represents the delta between the Begin and End states */
	TUniquePtr<FDynamicMeshChange> EndChange();

	/** Save necessary information about a vertex before it is modified */
	void SaveVertex(int VertexID);
	/** Save necessary information about a triangle before it is modified */
	void SaveTriangle(int TriangleID, bool bSaveVertices);

	/** Do (limited) sanity checking to make sure that the change is well-formed*/
	void VerifySaveState();

protected:
	const FDynamicMesh3* Mesh = nullptr;

	/** Active attribute tracker, if Mesh has attribute overlays */
	FDynamicMeshAttributeSetChangeTracker* AttribChangeTracker = nullptr;

	/** Active change that is being constructed */
	FDynamicMeshChange* Change = nullptr;

	int MaxTriangleID;
	TBitArray<> StartTriangles;		// bit is 1 if triangle ID was in initial mesh on BeginChange()
	TBitArray<> ChangedTriangles;	// bit is set to 1 if triangle was in StartTriangles and then had SaveTriangle() called for it

	int MaxVertexID;
	TBitArray<> StartVertices;		// bit is 1 if vertex ID was in initial mesh on BeginChange()
	TBitArray<> ChangedVertices;	// bit is set to 1 if vertex was in StartVertices and then had SaveVertex() called for it
};

