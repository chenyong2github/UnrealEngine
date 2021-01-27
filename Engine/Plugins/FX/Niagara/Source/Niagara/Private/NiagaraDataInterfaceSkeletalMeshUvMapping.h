// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterfaceSkeletalMesh.h"
#include "NiagaraUvQuadTree.h"
#include "RenderResource.h"

struct FSkeletalMeshUvMapping;

class FSkeletalMeshUvMappingBufferProxy : public FRenderResource
{
public:
	void Initialize(const FSkeletalMeshUvMapping& UvMappingData);

	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;

	FShaderResourceViewRHIRef GetSrv() const { return UvMappingSrv; }
	uint32 GetBufferSize() const { return UvMappingBuffer ? UvMappingBuffer->GetSize() : 0; }

private:
	TResourceArray<uint8> FrozenQuadTree;

	FVertexBufferRHIRef UvMappingBuffer;
	FShaderResourceViewRHIRef UvMappingSrv;

#if STATS
	int64 GpuMemoryUsage = 0;
#endif
};

struct FSkeletalMeshUvMapping
{
	FSkeletalMeshUvMapping() = delete;
	FSkeletalMeshUvMapping(const FSkeletalMeshUvMapping&) = delete;
	FSkeletalMeshUvMapping(TWeakObjectPtr<USkeletalMesh> InMeshObject, int32 InLodIndex, int32 InUvSetIndex);

	bool IsUsed() const;
	bool CanBeDestroyed() const;
	void RegisterUser(FSkeletalMeshUvMappingUsage Usage, bool bNeedsDataImmediately);
	void UnregisterUser(FSkeletalMeshUvMappingUsage Usage);

	static bool IsValidMeshObject(TWeakObjectPtr<USkeletalMesh>& MeshObject, int32 InLodIndex, int32 InUvSetIndex);

	bool Matches(const TWeakObjectPtr<USkeletalMesh>& InMeshObject, int32 InLodIndex, int32 InUvSetIndex) const;

	void FindOverlappingTriangles(const FVector2D& InUv, float Tolerance, TArray<int32>& TriangleIndices) const;
	int32 FindFirstTriangle(const FVector2D& InUv, float Tolerance, FVector& BarycentricCoord) const;
	int32 FindFirstTriangle(const FBox2D& InUvBox, FVector& BarycentricCoord) const;
	void FreezeQuadTree(TResourceArray<uint8>& OutQuadTree) const;
	const FSkeletalMeshUvMappingBufferProxy* GetQuadTreeProxy() const;
	const FSkeletalMeshLODRenderData* GetLodRenderData() const;

	const int32 LodIndex;
	const int32 UvSetIndex;

private:
	static const FSkeletalMeshLODRenderData* GetLodRenderData(const USkeletalMesh& Mesh, int32 LodIndex, int32 UvSetIndex);

	void BuildQuadTree();
	void ReleaseQuadTree();

	void BuildGpuQuadTree();
	void ReleaseGpuQuadTree();

	TWeakObjectPtr<USkeletalMesh> MeshObject;

	FNiagaraUvQuadTree TriangleIndexQuadTree;
	FSkeletalMeshUvMappingBufferProxy FrozenQuadTreeProxy;

	std::atomic<int32> CpuQuadTreeUserCount;
	std::atomic<int32> GpuQuadTreeUserCount;

	FThreadSafeBool ReleasedByRT;
	bool QueuedForRelease;
};
