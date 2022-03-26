// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChaosFlesh/TetrahedralCollection.h"

namespace Chaos
{
	class FChaosArchive;
}

/**
* FFleshCollection (FTetrehidralCollection)
*/
class CHAOSFLESH_API FFleshCollection : public FTetrahedralCollection
{
public:
	typedef FTetrahedralCollection Super;

	FFleshCollection();
	FFleshCollection(FFleshCollection&) = delete;
	FFleshCollection& operator=(const FFleshCollection&) = delete;
	FFleshCollection(FFleshCollection&&) = default;
	FFleshCollection& operator=(FFleshCollection&&) = default;


	static const FName MassAttribute; // (Mass,Vertices)

	/**
	 * Create a GeometryCollection from Vertex and Indices arrays
	 */
	static FFleshCollection* NewFleshCollection(const TArray<FVector3f>& Vertices, const TArray<FIntVector3>& SurfaceElements, const TArray<FIntVector4>& Elements, bool bReverseVertexOrder = true);
	static void Init(FFleshCollection* Collection, const TArray<FVector3f>& Vertices, const TArray<FIntVector3>& SurfaceElements, const TArray<FIntVector4>& Elements,  bool bReverseVertexOrder = true);


	// Simualtion attributes (Vertices Group)
	TManagedArray<float>		Mass;

protected:
	void Construct();

};

FORCEINLINE Chaos::FChaosArchive& operator<<(Chaos::FChaosArchive& Ar, FFleshCollection& Value)
{
	Value.Serialize(Ar);
	return Ar;
}
