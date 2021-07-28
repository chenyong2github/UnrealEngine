// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RHI.h"
#include "RHICommandList.h"

class FDisplayClusterRender_MeshComponentProxyData;

class DISPLAYCLUSTER_API FDisplayClusterRender_MeshComponentProxy
{
public:
	FDisplayClusterRender_MeshComponentProxy();
	~FDisplayClusterRender_MeshComponentProxy();

public:
	bool BeginRender_RenderThread(FRHICommandListImmediate& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit) const;
	bool FinishRender_RenderThread(FRHICommandListImmediate& RHICmdList) const;

	void UpdateRHI_RenderThread(FRHICommandListImmediate& RHICmdList, FDisplayClusterRender_MeshComponentProxyData* InMeshData);

	void Release_RenderThread();

	bool IsValid_RenderThread() const
	{
		return NumTriangles > 0 && NumVertices > 0 && VertexBufferRHI.IsValid() && IndexBufferRHI.IsValid();
	}

private:
	/* RenderThread resources */
	FVertexBufferRHIRef VertexBufferRHI;
	FIndexBufferRHIRef  IndexBufferRHI;

	uint32 NumTriangles = 0;
	uint32 NumVertices = 0;
};
