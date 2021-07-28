// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Containers/DisplayClusterRender_MeshComponentProxy.h"
#include "Render/Containers/DisplayClusterRender_MeshComponentProxyData.h"
#include "Render/Containers/DisplayClusterRender_MeshResources.h"

#include "Misc/DisplayClusterLog.h"

TGlobalResource<FDisplayClusterMeshVertexDeclaration> GDisplayClusterMeshVertexDeclaration;

//*************************************************************************
//* FDisplayClusterRender_MeshComponentProxy
//*************************************************************************
FDisplayClusterRender_MeshComponentProxy::FDisplayClusterRender_MeshComponentProxy()
{ }

FDisplayClusterRender_MeshComponentProxy::~FDisplayClusterRender_MeshComponentProxy()
{
	Release_RenderThread();
}

void FDisplayClusterRender_MeshComponentProxy::Release_RenderThread()
{
	check(IsInRenderingThread());

	VertexBufferRHI.SafeRelease();
	IndexBufferRHI.SafeRelease();

	NumTriangles = 0;
	NumVertices = 0;
}

bool FDisplayClusterRender_MeshComponentProxy::BeginRender_RenderThread(FRHICommandListImmediate& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit) const
{
	check(IsInRenderingThread());

	if (IsValid_RenderThread())
	{
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GDisplayClusterMeshVertexDeclaration.VertexDeclarationRHI;
		return true;
	}

	return false;
}

bool  FDisplayClusterRender_MeshComponentProxy::FinishRender_RenderThread(FRHICommandListImmediate& RHICmdList) const
{
	check(IsInRenderingThread());

	if (IsValid_RenderThread())
	{
		// Support update
		RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);
		RHICmdList.DrawIndexedPrimitive(IndexBufferRHI, 0, 0, NumVertices, 0, NumTriangles, 1);
		return true;
	}

	return false;
}

void FDisplayClusterRender_MeshComponentProxy::UpdateRHI_RenderThread(FRHICommandListImmediate& RHICmdList, FDisplayClusterRender_MeshComponentProxyData* InMeshData)
{
	check(IsInRenderingThread());

	Release_RenderThread();

	if (InMeshData && InMeshData->IsValid())
	{
		NumTriangles = InMeshData->GetNumTriangles();
		NumVertices = InMeshData->GetNumVertices();

		uint32 Usage = BUF_ShaderResource | BUF_Static;

		// Create Vertex buffer RHI:
		{
			FRHIResourceCreateInfo CreateInfo;
			size_t VertexDataSize = sizeof(FDisplayClusterMeshVertex) * NumVertices;
			if (VertexDataSize == 0)
			{
				UE_LOG(LogDisplayClusterRender, Warning, TEXT("MeshComponent has a vertex size of 0, please make sure a mesh is assigned."))
					return;
			}

			VertexBufferRHI = RHICreateVertexBuffer(VertexDataSize, Usage, CreateInfo);
			FDisplayClusterMeshVertex* DestVertexData = reinterpret_cast<FDisplayClusterMeshVertex*>(RHILockVertexBuffer(VertexBufferRHI, 0, VertexDataSize, RLM_WriteOnly));
			if (DestVertexData)
			{
				FPlatformMemory::Memcpy(DestVertexData, InMeshData->GetVertexData().GetData(), VertexDataSize);
				RHIUnlockVertexBuffer(VertexBufferRHI);
			}
		}

		// Create Index buffer RHI:
		{
			FRHIResourceCreateInfo CreateInfo;
			size_t IndexDataSize = sizeof(uint32) * InMeshData->GetIndexData().Num();

			IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint32), IndexDataSize, Usage, CreateInfo);

			uint32* DestIndexData = reinterpret_cast<uint32*>(RHILockIndexBuffer(IndexBufferRHI, 0, IndexDataSize, RLM_WriteOnly));
			if (DestIndexData)
			{
				FPlatformMemory::Memcpy(DestIndexData, InMeshData->GetIndexData().GetData(), IndexDataSize);
				RHIUnlockIndexBuffer(IndexBufferRHI);
			}
		}
	}
}
