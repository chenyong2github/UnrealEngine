// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshUtilities.h"
#include "kDOP.h"

#if USE_EMBREE
	#include <embree2/rtcore.h>
	#include <embree2/rtcore_ray.h>
#else
	typedef void* RTCDevice;
	typedef void* RTCScene;
#endif

class FSourceMeshDataForDerivedDataTask;

class FMeshBuildDataProvider
{
public:

	/** Initialization constructor. */
	FMeshBuildDataProvider(
		const TkDOPTree<const FMeshBuildDataProvider, uint32>& InkDopTree) :
		kDopTree(InkDopTree)
	{}

	// kDOP data provider interface.

	FORCEINLINE const TkDOPTree<const FMeshBuildDataProvider, uint32>& GetkDOPTree(void) const
	{
		return kDopTree;
	}

	FORCEINLINE const FMatrix& GetLocalToWorld(void) const
	{
		return FMatrix::Identity;
	}

	FORCEINLINE const FMatrix& GetWorldToLocal(void) const
	{
		return FMatrix::Identity;
	}

	FORCEINLINE FMatrix GetLocalToWorldTransposeAdjoint(void) const
	{
		return FMatrix::Identity;
	}

	FORCEINLINE float GetDeterminant(void) const
	{
		return 1.0f;
	}

private:

	const TkDOPTree<const FMeshBuildDataProvider, uint32>& kDopTree;
};

struct FEmbreeTriangleDesc
{
	int16 ElementIndex;
};

// Mapping between Embree Geometry Id and engine Mesh/LOD Id
struct FEmbreeGeometry
{
	TArray<FEmbreeTriangleDesc> TriangleDescs; // The material ID of each triangle.
};

class FEmbreeScene
{
public:
	bool bUseEmbree = false;
	int32 NumIndices = 0;

	// Embree
	RTCDevice EmbreeDevice = nullptr;
	RTCScene EmbreeScene = nullptr;
	FEmbreeGeometry Geometry;

	// DOP tree fallback
	TkDOPTree<const FMeshBuildDataProvider, uint32> kDopTree;
};

#if USE_EMBREE
struct FEmbreeRay : public RTCRay
{
	FEmbreeRay() :
		ElementIndex(-1)
	{
		u = v = 0;
		time = 0;
		mask = 0xFFFFFFFF;
		geomID = -1;
		instID = -1;
		primID = -1;
	}

	FVector GetHitNormal() const
	{
		return FVector(Ng[0], Ng[1], Ng[2]).GetSafeNormal();
	}	

	bool IsHitTwoSided() const
	{
		// MaterialIndex on the build triangles was set to 1 if two-sided, or 0 if one-sided
		return ElementIndex == 1;
	}

	// Additional Outputs.
	int32 ElementIndex; // Material Index
};
#endif

namespace MeshRepresentation
{
	/**
	 *	Generates unit length, stratified and uniformly distributed direction samples in a hemisphere. 
	 */
	void GenerateStratifiedUniformHemisphereSamples(int32 NumSamples, FRandomStream& RandomStream, TArray<FVector4>& Samples);

	/**
	 *	[Frisvad 2012, "Building an Orthonormal Basis from a 3D Unit Vector Without Normalization"]
	 */
	FMatrix GetTangentBasisFrisvad(FVector TangentZ);

	void SetupEmbreeScene(FString MeshName,
		const FSourceMeshDataForDerivedDataTask& SourceMeshData,
		const FStaticMeshLODResources& LODModel,
		const TArray<FSignedDistanceFieldBuildMaterialData>& MaterialBlendModes,
		bool bGenerateAsIfTwoSided,
		FEmbreeScene& EmbreeScene);

	void DeleteEmbreeScene(FEmbreeScene& EmbreeScene);
};