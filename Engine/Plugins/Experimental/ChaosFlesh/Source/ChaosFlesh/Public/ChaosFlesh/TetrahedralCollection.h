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


	// groups
	static const FName TetrahedralGroup;

	// attributes
	static const FName TetrahedronAttribute;

	/**
	 * Create a GeometryCollection from Vertex and Indices arrays
	 */
	static FTetrahedralCollection* NewTetrahedralCollection(const TArray<FVector3f>& Vertices, const TArray<FIntVector3>& SurfaceElements, const TArray<FIntVector4>& Elements,bool bReverseVertexOrder = true);
	static void Init(FTetrahedralCollection* Collection, const TArray<FVector3f>& Vertices, const TArray<FIntVector3>& SurfaceElements, const TArray<FIntVector4>& Elements, bool bReverseVertexOrder = true);

	void SetDefaults(FName Group, uint32 StartSize, uint32 NumElements);

	// Transform Group
	TManagedArray<FIntVector4>	Tetrahedron;
	
protected:

	void Construct();

};

FORCEINLINE Chaos::FChaosArchive& operator<<(Chaos::FChaosArchive& Ar, FTetrahedralCollection& Value)
{
	Value.Serialize(Ar);
	return Ar;
}

