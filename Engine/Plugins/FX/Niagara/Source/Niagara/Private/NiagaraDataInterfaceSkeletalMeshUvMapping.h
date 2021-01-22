// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterfaceSkeletalMesh.h"
#include "NiagaraUvQuadTree.h"
#include "RenderResource.h"

struct FSkeletalMeshUvMapping;

class FSkeletalMeshUvMappingBufferProxy : public FRenderResource
{
public:
	FSkeletalMeshUvMappingBufferProxy();
	virtual ~FSkeletalMeshUvMappingBufferProxy();

	void Initialize(const FSkeletalMeshUvMapping& UvMappingData);

	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;

	FShaderResourceViewRHIRef GetSrv() const { return UvMappingSrv; }

private:
	TResourceArray<uint8> FrozenQuadTree;

	FBufferRHIRef UvMappingBuffer;
	FShaderResourceViewRHIRef UvMappingSrv;

#if STATS
	int64 GpuMemoryUsage = 0;
#endif
};

struct FSkeletalMeshUvMapping
{
	FSkeletalMeshUvMapping(TWeakObjectPtr<USkeletalMesh> InMeshObject, int32 InLodIndex, int32 InUvSetIndex);

	bool IsUsed() const;
	void RegisterUser(FSkeletalMeshUvMappingUsage Usage, bool bNeedsDataImmediately);
	void UnregisterUser(FSkeletalMeshUvMappingUsage Usage);

	bool Matches(const TWeakObjectPtr<USkeletalMesh>& InMeshObject, int32 InLodIndex, int32 InUvSetIndex) const;

	void FindOverlappingTriangles(const FVector2D& InUv, TArray<int32>& TriangleIndices, float Tolerance = SMALL_NUMBER) const;
	int32 FindFirstTriangle(const FVector2D& InUv, FVector& BarycentricCoord, float Tolerance = SMALL_NUMBER) const;
	void FreezeQuadTree(TResourceArray<uint8>& OutQuadTree) const;
	const FSkeletalMeshUvMappingBufferProxy* GetQuadTreeProxy() const;

	const int32 LodIndex;
	const int32 UvSetIndex;

private:
	const FSkeletalMeshLODRenderData* GetLodRenderData() const;

	void BuildQuadTree();
	void ReleaseQuadTree();

	void BuildGpuQuadTree();
	void ReleaseGpuQuadTree();

	TWeakObjectPtr<USkeletalMesh> MeshObject;

	FNiagaraUvQuadTree TriangleIndexQuadTree;
	FSkeletalMeshUvMappingBufferProxy FrozenQuadTreeProxy;

	TAtomic<int32> CpuQuadTreeUserCount;
	TAtomic<int32> GpuQuadTreeUserCount;
};
