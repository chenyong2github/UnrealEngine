// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh3.h"
#include "DynamicAttribute.h"


template<typename AttribValueType, int AttribDimension>
class TDynamicMeshVertexAttribute;


template<typename AttribValueType, int AttribDimension>
class DYNAMICMESH_API FDynamicMeshVertexAttributeChange : public FDynamicAttributeChangeBase
{
private:
	struct FChangeVertexAttribute
	{
		AttribValueType Data[AttribDimension];
		int VertexID;
	};

	TArray<FChangeVertexAttribute> OldVertexAttributes, NewVertexAttributes;

public:
	FDynamicMeshVertexAttributeChange()
	{}

	virtual ~FDynamicMeshVertexAttributeChange()
	{}

	inline virtual void SaveInitialVertex(const FDynamicAttributeBase* Attribute, int VertexID) override;

	inline virtual void StoreAllFinalVertices(const FDynamicAttributeBase* Attribute, const TArray<int>& VertexIDs) override;

	inline virtual bool Apply(FDynamicAttributeBase* Attribute, bool bRevert) const override;
};


/**
 * TDynamicMeshVertexAttribute is an add-on to a FDynamicMesh3 that allows for 
 * per-triangle storage of an attribute value.
 *
 * The FDynamicMesh3 mesh topology operations (eg split/flip/collapse edge, poke face, etc)
 * can be mirrored to the overlay via OnSplitEdge(), etc.
 */
template<typename AttribValueType, int AttribDimension>
class DYNAMICMESH_API TDynamicMeshVertexAttribute : public FDynamicAttributeBase
{

protected:
	/** The parent mesh this overlay belongs to */
	FDynamicMesh3* ParentMesh;

	/** List of per-triangle attribute values */
	TDynamicVector<AttribValueType> AttribValues;

	friend class FDynamicMesh3;
	friend class FDynamicMeshAttributeSet;

public:
	/** Create an empty overlay */
	TDynamicMeshVertexAttribute()
	{
		ParentMesh = nullptr;
	}

	/** Create an overlay for the given parent mesh */
	TDynamicMeshVertexAttribute(FDynamicMesh3* ParentMeshIn)
	{
		ParentMesh = ParentMeshIn;
	}

	virtual ~TDynamicMeshVertexAttribute()
	{
	}

	/** @return the parent mesh for this overlay */
	const FDynamicMesh3* GetParentMesh() const { return ParentMesh; }
	/** @return the parent mesh for this overlay */
	FDynamicMesh3* GetParentMesh() { return ParentMesh; }

	virtual FDynamicAttributeBase* MakeCopy(FDynamicMesh3* ParentMeshIn) const override
	{
		TDynamicMeshVertexAttribute<AttribValueType, AttribDimension>* ToFill = new TDynamicMeshVertexAttribute<AttribValueType, AttribDimension>(ParentMeshIn);
		ToFill->Copy(*this);
		return ToFill;
	}

	/** Set this overlay to contain the same arrays as the copy overlay */
	void Copy(const TDynamicMeshVertexAttribute<AttribValueType, AttribDimension>& Copy)
	{
		AttribValues = Copy.AttribValues;
	}

	/** Initialize the attribute values to the given max triangle ID */
	void Initialize(AttribValueType InitialValue = (AttribValueType)0)
	{
		check(ParentMesh != nullptr);
		AttribValues.Resize(0);
		AttribValues.Resize( ParentMesh->MaxVertexID() * AttribDimension, InitialValue );
	}

	void SetNewValue(int NewVertexID, const AttribValueType* Data)
	{
		int k = NewVertexID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			AttribValues.InsertAt(Data[i], k + i);
		}
	}



	//
	// Accessors/Queries
	//  

	/** Get the element at a given index */
	inline void GetValue(int VertexID, AttribValueType* Data) const
	{
		int k = VertexID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			Data[i] = AttribValues[k + i];
		}
	}

	/** Get the element at a given index */
	template<typename AsType>
	void GetValue(int VertexID, AsType& Data) const
	{
		int k = VertexID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			Data[i] = AttribValues[k + i];
		}
	}


	/** Set the element at a given index */
	inline void SetValue(int VertexID, const AttribValueType* Data)
	{
		int k = VertexID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			AttribValues[k + i] = Data[i];
		}
	}

	/** Set the element at a given index */
	template<typename AsType>
	void SetValue(int VertexID, const AsType& Data)
	{
		int k = VertexID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			AttribValues[k + i] = Data[i];
		}
	}

	/**
	 * Copy the attribute value at FromVertexID to ToVertexID
	 */
	inline void CopyValue(int FromVertexID, int ToVertexID)
	{
		int kA = FromVertexID * AttribDimension;
		int kB = ToVertexID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			AttribValues.InsertAt(AttribValues[kA+i], kB+i);
		}
	}


public:

	/** Update the overlay to reflect an edge split in the parent mesh */
	void OnSplitEdge(const FDynamicMesh3::FEdgeSplitInfo& SplitInfo) override
	{
		SetAttributeFromLerp(SplitInfo.NewVertex, SplitInfo.OriginalVertices.A, SplitInfo.OriginalVertices.B, SplitInfo.SplitT);
	}

	/** Update the overlay to reflect an edge flip in the parent mesh */
	void OnFlipEdge(const FDynamicMesh3::FEdgeFlipInfo& FlipInfo) override
	{
		// vertices unchanged
	}

	/** Update the overlay to reflect an edge collapse in the parent mesh */
	void OnCollapseEdge(const FDynamicMesh3::FEdgeCollapseInfo& CollapseInfo) override
	{
		SetAttributeFromLerp(CollapseInfo.KeptVertex, CollapseInfo.KeptVertex, CollapseInfo.RemovedVertex, CollapseInfo.CollapseT);
	}

	virtual AttribValueType GetDefaultAttributeValue()
	{
		return (AttribValueType)0;
	}

	virtual void OnNewVertex(int VertexID, bool bInserted) override
	{
		if (ParentMesh->MaxVertexID() >= AttribValues.Num() * AttribDimension)
		{
			AttribValues.Resize((ParentMesh->MaxVertexID()+1) * AttribDimension, GetDefaultAttributeValue());
		}
	} 

	/** Update the overlay to reflect a face poke in the parent mesh */
	void OnPokeTriangle(const FDynamicMesh3::FPokeTriangleInfo& PokeInfo) override
	{
		FIndex3i Tri = PokeInfo.TriVertices;
		SetAttributeFromBary(PokeInfo.NewVertex, Tri.A, Tri.B, Tri.C, PokeInfo.BaryCoords);
	}

	/** Update the overlay to reflect an edge merge in the parent mesh */
	void OnMergeEdges(const FDynamicMesh3::FMergeEdgesInfo& MergeInfo) override
	{
		// just blend the attributes?
		SetAttributeFromLerp(MergeInfo.KeptVerts.A, MergeInfo.KeptVerts.A, MergeInfo.RemovedVerts.A, .5);
		SetAttributeFromLerp(MergeInfo.KeptVerts.B, MergeInfo.KeptVerts.B, MergeInfo.RemovedVerts.B, .5);
	}

	virtual TUniquePtr<FDynamicAttributeChangeBase> NewBlankChange() override
	{
		return MakeUnique<FDynamicMeshVertexAttributeChange<AttribValueType, AttribDimension>>();
	}

protected:

	// interpolation functions; default implementation assumes your attributes can be interpolated as reals

	/** Set the value at an Attribute to be a linear interpolation of two other Attributes */
	virtual void SetAttributeFromLerp(int SetAttribute, int AttributeA, int AttributeB, double Alpha)
	{
		int IndexSet = AttribDimension * SetAttribute;
		int IndexA = AttribDimension * AttributeA;
		int IndexB = AttribDimension * AttributeB;
		AttribValueType Beta = ((AttribValueType)1 - Alpha);
		for (int i = 0; i < AttribDimension; ++i)
		{
			AttribValues[IndexSet+i] = Beta*AttribValues[IndexA+i] + Alpha*AttribValues[IndexB+i];
		}
	}

	/** Set the value at an Attribute to be a barycentric interpolation of three other Attributes */
	virtual void SetAttributeFromBary(int SetAttribute, int AttributeA, int AttributeB, int AttributeC, const FVector3d& BaryCoords)
	{
		int IndexSet = AttribDimension * SetAttribute;
		int IndexA = AttribDimension * AttributeA;
		int IndexB = AttribDimension * AttributeB;
		int IndexC = AttribDimension * AttributeC;
		for (int i = 0; i < AttribDimension; ++i)
		{
			AttribValues[IndexSet + i] = 
				BaryCoords.X*AttribValues[IndexA+i] + BaryCoords.Y*AttribValues[IndexB+i] + BaryCoords.Z*AttribValues[IndexC+i];
		}
	}

};


template<typename AttribValueType, int AttribDimension>
void FDynamicMeshVertexAttributeChange<AttribValueType, AttribDimension>::SaveInitialVertex(const FDynamicAttributeBase* Attribute, int VertexID)
{
	FChangeVertexAttribute& Change = OldVertexAttributes.Emplace_GetRef();
	Change.VertexID = VertexID;
	const TDynamicMeshVertexAttribute<AttribValueType, AttribDimension>* AttribCast = static_cast<const TDynamicMeshVertexAttribute<AttribValueType, AttribDimension>*>(Attribute);
	AttribCast->GetValue(VertexID, Change.Data);
}

template<typename AttribValueType, int AttribDimension>
void FDynamicMeshVertexAttributeChange<AttribValueType, AttribDimension>::StoreAllFinalVertices(const FDynamicAttributeBase* Attribute, const TArray<int>& VertexIDs)
{
	NewVertexAttributes.Reserve(NewVertexAttributes.Num() + VertexIDs.Num());
	const TDynamicMeshVertexAttribute<AttribValueType, AttribDimension>* AttribCast = static_cast<const TDynamicMeshVertexAttribute<AttribValueType, AttribDimension>*>(Attribute);
	for (int VertexID : VertexIDs)
	{
		FChangeVertexAttribute& Change = NewVertexAttributes.Emplace_GetRef();
		Change.VertexID = VertexID;
		AttribCast->GetValue(VertexID, Change.Data);
	}
}

template<typename AttribValueType, int AttribDimension>
bool FDynamicMeshVertexAttributeChange<AttribValueType, AttribDimension>::Apply(FDynamicAttributeBase* Attribute, bool bRevert) const
{
	const TArray<FChangeVertexAttribute> *Changes = bRevert ? &OldVertexAttributes : &NewVertexAttributes;
	TDynamicMeshVertexAttribute<AttribValueType, AttribDimension>* AttribCast = static_cast<TDynamicMeshVertexAttribute<AttribValueType, AttribDimension>*>(Attribute);
	for (const FChangeVertexAttribute& Change : *Changes)
	{
		check(AttribCast->GetParentMesh()->IsVertex(Change.VertexID));
		AttribCast->SetValue(Change.VertexID, Change.Data);
	}
	return true;
}