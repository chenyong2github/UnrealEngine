// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "DynamicMeshOverlay.h"
#include "DynamicMeshTriangleAttribute.h"
#include "DynamicAttribute.h"
#include "GeometryTypes.h"
#include "InfoTypes.h"

/** Standard UV overlay type - 2-element float */
typedef TDynamicMeshVectorOverlay<float, 2, FVector2f> FDynamicMeshUVOverlay;
/** Standard Normal overlay type - 3-element float */
typedef TDynamicMeshVectorOverlay<float, 3, FVector3f> FDynamicMeshNormalOverlay;

/** Standard per-triangle integer material ID */
typedef TDynamicMeshScalarTriangleAttribute<int32> FDynamicMeshMaterialAttribute;

/**
 * FDynamicMeshAttributeSet manages a set of extended attributes for a FDynamicMesh3.
 * This includes UV and Normal overlays, etc.
 * 
 * Currently the default is to always have one UV layer and one Normal layer
 * 
 * @todo current internal structure is a work-in-progress
 */
class DYNAMICMESH_API FDynamicMeshAttributeSet : public FDynamicMeshAttributeSetBase
{
public:

	FDynamicMeshAttributeSet(FDynamicMesh3* Mesh)
		: ParentMesh(Mesh), Normals0(Mesh)
	{
		SetNumUVLayers(1);
		NormalLayers.Add(&Normals0);
	}

	virtual ~FDynamicMeshAttributeSet()
	{
	}

	void Copy(const FDynamicMeshAttributeSet& Copy)
	{
		SetNumUVLayers(Copy.NumUVLayers());
		for (int UVIdx = 0; UVIdx < NumUVLayers(); UVIdx++)
		{
			UVLayers[UVIdx].Copy(Copy.UVLayers[UVIdx]);
		}
		Normals0.Copy(Copy.Normals0);

		if (Copy.MaterialIDAttrib)
		{
			EnableMaterialID();
			MaterialIDAttrib->Copy(*(Copy.MaterialIDAttrib));
		}
		else
		{
			DisableMaterialID();
		}

		GenericAttributes.Reset();
		ResetRegisteredAttributes();
		for (int Idx = 0; Idx < Copy.GenericAttributes.Num(); Idx++)
		{
			const FDynamicMeshAttributeBase* SourceAttrib = Copy.GenericAttributes[Idx].Get();
			AttachAttribute(SourceAttrib->MakeCopy(ParentMesh));
		}

		// parent mesh is *not* copied!
	}

	/** returns true if the attached overlays/attributes are compact */
	bool IsCompact()
	{
		for (int UVIdx = 0; UVIdx < NumUVLayers(); UVIdx++)
		{
			if (!UVLayers[UVIdx].IsCompact())
			{
				return false;
			}
		}
		if (!Normals0.IsCompact())
		{
			return false;
		}
		// material ID and generic per-element attributes currently cannot be non-compact
		return true;
	}

	/**
	 * Performs a CompactCopy of the attached overlays/attributes.
	 * Called by the parent mesh CompactCopy function.
	 *
	 * @param CompactMaps Maps indicating how vertices and triangles were changes in the parent
	 * @param Copy The attribute set to be copied
	 */
	void CompactCopy(const FCompactMaps& CompactMaps, const FDynamicMeshAttributeSet& Copy)
	{
		SetNumUVLayers(Copy.NumUVLayers());
		for (int UVIdx = 0; UVIdx < NumUVLayers(); UVIdx++)
		{
			UVLayers[UVIdx].CompactCopy(CompactMaps, Copy.UVLayers[UVIdx]);
		}
		Normals0.CompactCopy(CompactMaps, Copy.Normals0);

		if (Copy.MaterialIDAttrib)
		{
			EnableMaterialID();
			MaterialIDAttrib->CompactCopy(CompactMaps, *(Copy.MaterialIDAttrib));
		}
		else
		{
			DisableMaterialID();
		}

		GenericAttributes.Reset();
		ResetRegisteredAttributes();
		for (int Idx = 0; Idx < Copy.GenericAttributes.Num(); Idx++)
		{
			const FDynamicMeshAttributeBase* SourceAttrib = Copy.GenericAttributes[Idx].Get();
			AttachAttribute(SourceAttrib->MakeCompactCopy(CompactMaps, ParentMesh));
		}

		// parent mesh is *not* copied!
	}

	/**
	 * Compacts the attribute set in place
	 * Called by the parent mesh CompactInPlace function
	 *
	 * @param CompactMaps Maps of how the vertices and triangles were compacted in the parent
	 */
	void CompactInPlace(const FCompactMaps& CompactMaps)
	{
		for (int UVIdx = 0; UVIdx < NumUVLayers(); UVIdx++)
		{
			UVLayers[UVIdx].CompactInPlace(CompactMaps);
		}
		Normals0.CompactInPlace(CompactMaps);

		if (MaterialIDAttrib.IsValid())
		{
			MaterialIDAttrib->CompactInPlace(CompactMaps);
		}

		for (int Idx = 0; Idx < GenericAttributes.Num(); Idx++)
		{
			GenericAttributes[Idx]->CompactInPlace(CompactMaps);
		}
	}


	/**
	 * Enable the matching attributes and overlay layers as the reference Copy set, but do not copy any data across
	 */
	void EnableMatchingAttributes(const FDynamicMeshAttributeSet& ToMatch)
	{
		SetNumUVLayers(ToMatch.NumUVLayers());
		for (int UVIdx = 0; UVIdx < NumUVLayers(); UVIdx++)
		{
			UVLayers[UVIdx].ClearElements();
		}
		Normals0.ClearElements();

		if (ToMatch.MaterialIDAttrib)
		{
			EnableMaterialID();
		}
		else
		{
			DisableMaterialID();
		}

		GenericAttributes.Reset();
		ResetRegisteredAttributes();
		for (int Idx = 0; Idx < ToMatch.GenericAttributes.Num(); Idx++)
		{
			const FDynamicMeshAttributeBase* SourceAttrib = ToMatch.GenericAttributes[Idx].Get();
			AttachAttribute(SourceAttrib->MakeNew(ParentMesh));
		}
	}

	/** @return the parent mesh for this overlay */
	const FDynamicMesh3* GetParentMesh() const { return ParentMesh; }
	/** @return the parent mesh for this overlay */
	FDynamicMesh3* GetParentMesh() { return ParentMesh; }

private:
	/** @set the parent mesh for this overlay.  Only safe for use during FDynamicMesh move */
	void Reparent(FDynamicMesh3* NewParent)
	{
		ParentMesh = NewParent;

		for (int UVIdx = 0; UVIdx < NumUVLayers(); UVIdx++)
		{
			UVLayers[UVIdx].Reparent( NewParent );
		}
		Normals0.Reparent( NewParent );

		if (MaterialIDAttrib)
		{
			MaterialIDAttrib->Reparent( NewParent );
		}

		for (int Idx = 0; Idx < GenericAttributes.Num(); Idx++)
		{
			GenericAttributes[Idx]->Reparent( NewParent );
		}
	}

public:
	/** @return number of UV layers */
	virtual int NumUVLayers() const
	{
		return UVLayers.Num();
	}

	virtual void SetNumUVLayers(int Num)
	{
		if (UVLayers.Num() == Num)
		{
			return;
		}
		if (Num >= UVLayers.Num())
		{
			for (int i = (int)UVLayers.Num(); i < Num; ++i)
			{
				UVLayers.Add(new FDynamicMeshUVOverlay(ParentMesh));
			}
		}
		else
		{
			UVLayers.RemoveAt(Num, UVLayers.Num() - Num);
		}
		check(UVLayers.Num() == Num);
	}

	/** @return number of Normals layers */
	virtual int NumNormalLayers() const
	{
		checkSlow(NormalLayers.Num() == 1);
		return 1;
	}

	/** @return true if the given edge is a seam edge in any overlay */
	virtual bool IsSeamEdge(int EdgeID) const;

	/** @return true if the given vertex is a seam vertex in any overlay */
	virtual bool IsSeamVertex(int VertexID, bool bBoundaryIsSeam = true) const;


	//
	// UV Layers 
	//

	/** @return the UV layer at the given Index */
	FDynamicMeshUVOverlay* GetUVLayer(int Index)
	{
		return &UVLayers[Index];
	}

	/** @return the UV layer at the given Index */
	const FDynamicMeshUVOverlay* GetUVLayer(int Index) const
	{
		return &UVLayers[Index];
	}

	/** @return the primary UV layer (layer 0) */
	FDynamicMeshUVOverlay* PrimaryUV()
	{
		return &UVLayers[0];
	}
	/** @return the primary UV layer (layer 0) */
	const FDynamicMeshUVOverlay* PrimaryUV() const
	{
		return &UVLayers[0];
	}


	//
	// Normal Layers 
	//

	/** @return the Normal layer at the given Index */
	FDynamicMeshNormalOverlay* GetNormalLayer(int Index)
	{
		return (Index == 0) ? &Normals0 : nullptr;
	}

	/** @return the Normal layer at the given Index */
	const FDynamicMeshNormalOverlay* GetNormalLayer(int Index) const
	{
		return (Index == 0) ? &Normals0 : nullptr;
	}

	/** @return list of all Normal layers */
	const TArray<FDynamicMeshNormalOverlay*>& GetAllNormalLayers() const
	{
		return NormalLayers;
	}

	/** @return the primary Normal layer (layer 0) */
	FDynamicMeshNormalOverlay* PrimaryNormals()
	{
		return &Normals0;
	}
	/** @return the primary Normal layer (layer 0) */
	const FDynamicMeshNormalOverlay* PrimaryNormals() const
	{
		return &Normals0;
	}



	//
	// Per-Triangle Material ID
	//

	bool HasMaterialID() const
	{
		return !!MaterialIDAttrib;
	}


	void EnableMaterialID();

	void DisableMaterialID();

	FDynamicMeshMaterialAttribute* GetMaterialID()
	{
		return MaterialIDAttrib.Get();
	}

	const FDynamicMeshMaterialAttribute* GetMaterialID() const
	{
		return MaterialIDAttrib.Get();
	}

	// Attach a new attribute (and transfer ownership of it to the attribute set)
	int AttachAttribute(FDynamicMeshAttributeBase* Attribute)
	{
		int AttributeID = GenericAttributes.Add(TUniquePtr<FDynamicMeshAttributeBase>(Attribute));
		RegisterExternalAttribute(Attribute);
		return AttributeID;
	}

	FDynamicMeshAttributeBase* GetAttachedAttribute(int AttributeID)
	{
		return GenericAttributes[AttributeID].Get();
	}

	int NumAttachedAttributes()
	{
		return GenericAttributes.Num();
	}

protected:
	/** Parent mesh of this attribute set */
	FDynamicMesh3* ParentMesh;

	/** Default Normals layer */
	FDynamicMeshNormalOverlay Normals0;

	TIndirectArray<FDynamicMeshUVOverlay> UVLayers;
	TArray<FDynamicMeshNormalOverlay*> NormalLayers;

	TUniquePtr<FDynamicMeshMaterialAttribute> MaterialIDAttrib;

	TArray<TUniquePtr<FDynamicMeshAttributeBase>> GenericAttributes;

protected:
	friend class FDynamicMesh3;

	/**
	 * Initialize the existing attribute layers with the given vertex and triangle sizes
	 */
	void Initialize(int MaxVertexID, int MaxTriangleID)
	{
		for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
		{
			UVLayer.InitializeTriangles(MaxTriangleID);
		}
		Normals0.InitializeTriangles(MaxTriangleID);
	}

	// These functions are called by the FDynamicMesh3 to update the various
	// attributes when the parent mesh topology has been modified.
	// TODO: would it be better to register all the overlays and attributes with the base set and not overload these?  maybe!
	virtual void OnNewTriangle(int TriangleID, bool bInserted);
	virtual void OnNewVertex(int VertexID, bool bInserted);
	virtual void OnRemoveTriangle(int TriangleID);
	virtual void OnRemoveVertex(int VertexID);
	virtual void OnReverseTriOrientation(int TriangleID);
	virtual void OnSplitEdge(const DynamicMeshInfo::FEdgeSplitInfo & splitInfo);
	virtual void OnFlipEdge(const DynamicMeshInfo::FEdgeFlipInfo & flipInfo);
	virtual void OnCollapseEdge(const DynamicMeshInfo::FEdgeCollapseInfo & collapseInfo);
	virtual void OnPokeTriangle(const DynamicMeshInfo::FPokeTriangleInfo & pokeInfo);
	virtual void OnMergeEdges(const DynamicMeshInfo::FMergeEdgesInfo & mergeInfo);
	virtual void OnSplitVertex(const DynamicMeshInfo::FVertexSplitInfo& SplitInfo, const TArrayView<const int>& TrianglesToUpdate);

	/**
	 * Check validity of attributes
	 * 
	 * @param bAllowNonmanifold Accept non-manifold topology as valid. Note that this should almost always be true for attributes; non-manifold overlays are generally valid.
	 * @param FailMode Desired behavior if mesh is found invalid
	 */
	virtual bool CheckValidity(bool bAllowNonmanifold, EValidityCheckFailMode FailMode) const
	{
		bool bValid = FDynamicMeshAttributeSetBase::CheckValidity(bAllowNonmanifold, FailMode);
		for (int UVLayerIndex = 0; UVLayerIndex < NumUVLayers(); UVLayerIndex++)
		{
			bValid = GetUVLayer(UVLayerIndex)->CheckValidity(bAllowNonmanifold, FailMode) && bValid;
		}
		bValid = PrimaryNormals()->CheckValidity(bAllowNonmanifold, FailMode) && bValid;
		return bValid;
	}
};

