// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DynamicMesh3.h"

#pragma once


class FDynamicAttributeBase;


/**
* Generic base class for change tracking of an attribute layer
*/
class DYNAMICMESH_API FDynamicAttributeChangeBase
{
public:
	virtual ~FDynamicAttributeChangeBase()
	{
	}

	// default do-nothing implementations are provided because many attribute layers will only care about some kinds of elements and won't implement all of these

	virtual void SaveInitialTriangle(const FDynamicAttributeBase* Attribute, int TriangleID)
	{
	}
	virtual void SaveInitialVertex(const FDynamicAttributeBase* Attribute, int VertexID)
	{
	}

	virtual void StoreAllFinalTriangles(const FDynamicAttributeBase* Attribute, const TArray<int>& TriangleIDs)
	{
	}
	virtual void StoreAllFinalVertices(const FDynamicAttributeBase* Attribute, const TArray<int>& VertexIDs)
	{
	}

	virtual bool Apply(FDynamicAttributeBase* Attribute, bool bRevert) const
	{
		return false;
	}
};


/**
 * Base class for attributes that live on a dynamic mesh (or similar dynamic object)
 *
 * Subclasses can override the On* functions to ensure the attribute remains up to date through changes to the dynamic object
 */
class DYNAMICMESH_API FDynamicAttributeBase
{

public:
	virtual ~FDynamicAttributeBase()
	{
	}

public:

	/** Allocate a new copy of the attribute layer, optionally with a different parent mesh */
	virtual FDynamicAttributeBase* MakeCopy(FDynamicMesh3* ParentMeshIn) const = 0;

	/** Update to reflect an edge split in the parent mesh */
	virtual void OnSplitEdge(const FDynamicMesh3::FEdgeSplitInfo& SplitInfo)
	{
	}

	/** Update to reflect an edge flip in the parent mesh */
	virtual void OnFlipEdge(const FDynamicMesh3::FEdgeFlipInfo& FlipInfo)
	{
	}

	/** Update to reflect an edge collapse in the parent mesh */
	virtual void OnCollapseEdge(const FDynamicMesh3::FEdgeCollapseInfo& CollapseInfo)
	{
	}

	virtual void OnNewVertex(int VertexID, bool bInserted)
	{
	}

	virtual void OnRemoveVertex(int VertexID)
	{
	}

	virtual void OnNewTriangle(int TriangleID, bool bInserted)
	{
	}

	virtual void OnRemoveTriangle(int TriangleID)
	{
	}

	/** Update to reflect a face poke in the parent mesh */
	virtual void OnPokeTriangle(const FDynamicMesh3::FPokeTriangleInfo& PokeInfo)
	{
	}

	/** Update to reflect an edge merge in the parent mesh */
	virtual void OnMergeEdges(const FDynamicMesh3::FMergeEdgesInfo& MergeInfo)
	{
	}

	virtual void OnReverseTriOrientation(int TriangleID)
	{
	}

	/**
	* Check validity of attribute
	* 
	* @param bAllowNonmanifold Accept non-manifold topology as valid. Note that this should almost always be true for attributes; non-manifold overlays are generally valid.
	* @param FailMode Desired behavior if mesh is found invalid
	*/
	virtual bool CheckValidity(bool bAllowNonmanifold, EValidityCheckFailMode FailMode) const
	{
		// default impl just doesn't check anything; override with any useful sanity checks
		return true;
	}


	virtual TUniquePtr<FDynamicAttributeChangeBase> NewBlankChange() = 0;

};


/**
* Generic base class for managing a set of registered attributes that must all be kept up to date
*/
class DYNAMICMESH_API FDynamicAttributeSetBase
{
protected:
	// not managed by the base class; we should be able to register any attributes here that we want to be automatically updated
	TArray<FDynamicAttributeBase*> RegisteredAttributes;

	/**
	 * Stores the given attribute pointer in the attribute register, so that it will be updated with mesh changes, but does not take ownership of the attribute memory.
	 */
	void RegisterExternalAttribute(FDynamicAttributeBase* Attribute)
	{
		RegisteredAttributes.Add(Attribute);
	}

	void ResetRegisteredAttributes()
	{
		RegisteredAttributes.Reset();
	}

public:
	virtual ~FDynamicAttributeSetBase()
	{
	}

	int NumRegisteredAttributes() const
	{
		return RegisteredAttributes.Num();
	}

	FDynamicAttributeBase* GetRegisteredAttribute(int Idx) const
	{
		return RegisteredAttributes[Idx];
	}

	// These functions are called by the FDynamicMesh3 to update the various
	// attributes when the parent mesh topology has been modified.
	virtual void OnNewTriangle(int TriangleID, bool bInserted)
	{
		for (FDynamicAttributeBase* A : RegisteredAttributes)
		{
			A->OnNewTriangle(TriangleID, bInserted);
		}
	}
	virtual void OnNewVertex(int VertexID, bool bInserted)
	{
		for (FDynamicAttributeBase* A : RegisteredAttributes)
		{
			A->OnNewVertex(VertexID, bInserted);
		}
	}
	virtual void OnRemoveTriangle(int TriangleID)
	{
		for (FDynamicAttributeBase* A : RegisteredAttributes)
		{
			A->OnRemoveTriangle(TriangleID);
		}
	}
	virtual void OnRemoveVertex(int VertexID)
	{
		for (FDynamicAttributeBase* A : RegisteredAttributes)
		{
			A->OnRemoveTriangle(VertexID);
		}
	}
	virtual void OnReverseTriOrientation(int TriangleID)
	{
		for (FDynamicAttributeBase* A : RegisteredAttributes)
		{
			A->OnReverseTriOrientation(TriangleID);
		}
	}
	virtual void OnSplitEdge(const FDynamicMesh3::FEdgeSplitInfo& SplitInfo)
	{
		for (FDynamicAttributeBase* A : RegisteredAttributes)
		{
			A->OnSplitEdge(SplitInfo);
		}
	}
	virtual void OnFlipEdge(const FDynamicMesh3::FEdgeFlipInfo& FlipInfo)
	{
		for (FDynamicAttributeBase* A : RegisteredAttributes)
		{
			A->OnFlipEdge(FlipInfo);
		}
	}
	virtual void OnCollapseEdge(const FDynamicMesh3::FEdgeCollapseInfo& CollapseInfo)
	{
		for (FDynamicAttributeBase* A : RegisteredAttributes)
		{
			A->OnCollapseEdge(CollapseInfo);
		}
	}
	virtual void OnPokeTriangle(const FDynamicMesh3::FPokeTriangleInfo& PokeInfo)
	{
		for (FDynamicAttributeBase* A : RegisteredAttributes)
		{
			A->OnPokeTriangle(PokeInfo);
		}
	}
	virtual void OnMergeEdges(const FDynamicMesh3::FMergeEdgesInfo& MergeInfo)
	{
		for (FDynamicAttributeBase* A : RegisteredAttributes)
		{
			A->OnMergeEdges(MergeInfo);
		}
	}

	/**
	* Check validity of attributes
	* 
	* @param bAllowNonmanifold Accept non-manifold topology as valid. Note that this should almost always be true for attributes; non-manifold overlays are generally valid.
	* @param FailMode Desired behavior if mesh is found invalid
	*/
	virtual bool CheckValidity(bool bAllowNonmanifold, EValidityCheckFailMode FailMode) const
	{
		bool bValid = true;
		for (FDynamicAttributeBase* A : RegisteredAttributes)
		{
			bValid = A->CheckValidity(bAllowNonmanifold, FailMode) && bValid;
		}
		return bValid;
	}
};
