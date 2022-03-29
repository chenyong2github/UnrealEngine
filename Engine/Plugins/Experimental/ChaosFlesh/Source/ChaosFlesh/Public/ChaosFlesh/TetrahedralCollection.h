// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Misc/Crc.h"

namespace Chaos
{
	class FChaosArchive;
}


/**
* FTetrahedralCollection (FGeometryCollection)
*/
class CHAOSFLESH_API FTetrahedralCollection : public FGeometryCollection
{
	typedef FGeometryCollection Super;

public:
	FTetrahedralCollection();
	FTetrahedralCollection(FTetrahedralCollection &) = delete;
	FTetrahedralCollection& operator=(const FTetrahedralCollection &) = delete;
	FTetrahedralCollection(FTetrahedralCollection &&) = default;
	FTetrahedralCollection& operator=(FTetrahedralCollection &&) = default;

	/**
	 * Create a GeometryCollection from Vertex and Indices arrays
	 */
	static FTetrahedralCollection* NewTetrahedralCollection(const TArray<FVector>& Vertices, const TArray<FIntVector3>& SurfaceElements, const TArray<FIntVector4>& Elements,bool bReverseVertexOrder = true);
	static void Init(FTetrahedralCollection* Collection, const TArray<FVector>& Vertices, const TArray<FIntVector3>& SurfaceElements, const TArray<FIntVector4>& Elements, bool bReverseVertexOrder = true);

	/*
	*  SetDefaults for new entries on this collection. 
	*/
	virtual void SetDefaults(FName Group, uint32 StartSize, uint32 NumElements);

	/*
	*  Attribute Groupes
	*/
	static const FName TetrahedralGroup;

	/*
	*  Tetrahedron Attribute
	*  TManagedArray<FIntVector4> Tetrahedron = this->FindAttribute<FIntVector4>(FTetrahedralCollection::TetrahedronAttribute,FTetrahedralCollection::TetrahedralGroup);
	*/
	static const FName TetrahedronAttribute;
	TManagedArray<FIntVector4>Tetrahedron;
	
protected:

	void Construct();

};

FORCEINLINE Chaos::FChaosArchive& operator<<(Chaos::FChaosArchive& Ar, FTetrahedralCollection& Value)
{
	Value.Serialize(Ar);
	return Ar;
}

