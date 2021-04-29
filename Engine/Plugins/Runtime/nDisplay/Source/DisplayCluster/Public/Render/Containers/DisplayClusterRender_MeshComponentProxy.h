// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RHI.h"
#include "RHICommandList.h"

#include "Render/Containers/DisplayClusterRender_MeshComponentRef.h"

struct FStaticMeshLODResources;
class FDisplayClusterRender_MeshComponentProxyData;

class DISPLAYCLUSTER_API FDisplayClusterRender_MeshComponentProxy
{
public:
	FDisplayClusterRender_MeshComponentProxy();
	FDisplayClusterRender_MeshComponentProxy(const class FDisplayClusterRender_MeshGeometry& InMeshGeometry);
	~FDisplayClusterRender_MeshComponentProxy();

public:
	void AssignMeshRefs(UStaticMeshComponent* MeshComponent, USceneComponent* OriginComponent = nullptr);
	const FStaticMeshLODResources* GetWarpMeshLodResource(int LodIndex = 0) const;

	void UpdateDefferedRef();
	void UpdateDeffered(const class FDisplayClusterRender_MeshGeometry& InMeshGeometry);

	bool BeginRender_RenderThread(FRHICommandListImmediate& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit) const;
	bool FinishRender_RenderThread(FRHICommandListImmediate& RHICmdList) const;

protected:
	void UpdateRHI_RenderThread(FRHICommandListImmediate& RHICmdList, FDisplayClusterRender_MeshComponentProxyData* InMeshData);

public:
	// Reference containers:
	FDisplayClusterRender_MeshComponentRef    MeshComponentRef;
	FDisplayClusterSceneComponentRef          OriginComponentRef;

private:
	/* RenderThread resources */
	FBufferRHIRef VertexBufferRHI;
	FBufferRHIRef IndexBufferRHI;

	uint32 NumTriangles = 0;
	uint32 NumVertices = 0;
};

